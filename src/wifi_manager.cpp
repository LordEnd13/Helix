#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Dichiarato a livello di file: in C++ `extern "C"` dentro una funzione
// non è valido e fa fallire la compilazione. Il simbolo è weak: se questo
// core Arduino non include il NAT, il puntatore resta nullo e non si chiama.
extern "C" {
void ip_napt_enable(uint32_t addr, int enable) __attribute__((weak));
}

namespace {

enum State { WF_JOIN, WF_STA, WF_AP, WF_APSTA, WF_DOWN };

State wf = WF_DOWN;
uint32_t mark = 0;
uint32_t linkGrace = 0;
bool apOn = false;
bool fallback = true;
bool dnsOn = false;
bool hold = false;
bool routed = false;
volatile bool scanAsk = false;
volatile bool scanBusy = false;
volatile bool scanReady = false;
bool scanForJoin = false;
bool scanArm = false;
uint8_t scanTries = 0;
uint32_t scanArmAt = 0;
uint32_t scanMark = 0;
uint8_t apChannel = 1;
char scanJson[1400] = {0};
SemaphoreHandle_t scanMux = nullptr;
char credSsid[33] = {0};
char credPass[65] = {0};
char pendSsid[33] = {0};
char pendPass[65] = {0};
volatile bool pend = false;
SemaphoreHandle_t pendMux = nullptr;
DNSServer dns;

struct Radio {
  char mode[12];
  char ssid[33];
  char ip[16];
  int32_t rssi;
  bool up;
};
Radio radio{};
portMUX_TYPE radioMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t radioMark = 0;

void copyClip(char* dst, size_t n, const char* src);
const char* modeStr();

void publishRadio() {
  Radio next{};
  const char* m = modeStr();
  strncpy(next.mode, m, sizeof(next.mode) - 1);
  next.up = WiFi.status() == WL_CONNECTED;
  if (next.up) {
    copyClip(next.ssid, sizeof(next.ssid), WiFi.SSID().c_str());
    copyClip(next.ip, sizeof(next.ip), WiFi.localIP().toString().c_str());
    next.rssi = WiFi.RSSI();
  } else if (apOn) {
    copyClip(next.ip, sizeof(next.ip), "192.168.4.1");
    if (wf == WF_JOIN && credSsid[0]) copyClip(next.ssid, sizeof(next.ssid), credSsid);
  } else {
    copyClip(next.ip, sizeof(next.ip), "0.0.0.0");
  }
  portENTER_CRITICAL(&radioMux);
  radio = next;
  portEXIT_CRITICAL(&radioMux);
}

void readRadio(Radio& out) {
  portENTER_CRITICAL(&radioMux);
  out = radio;
  portEXIT_CRITICAL(&radioMux);
}

void copyClip(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

void loadNvs() {
  Preferences p;
  p.begin("helix", true);
  String s = p.getString("ssid", "");
  String pw = p.getString("pass", "");
  fallback = p.getBool("apfb", true);
  p.end();
  copyClip(credSsid, sizeof(credSsid), s.c_str());
  copyClip(credPass, sizeof(credPass), pw.c_str());
}

void saveNvs() {
  Preferences p;
  p.begin("helix", false);
  p.putString("ssid", credSsid);
  p.putString("pass", credPass);
  p.putBool("apfb", fallback);
  p.end();
}

const char* modeStr() {
  switch (wf) {
    case WF_JOIN:
      return "connecting";
    case WF_STA:
      return "sta";
    case WF_AP:
      return "ap";
    case WF_APSTA:
      return "ap+sta";
    default:
      return "off";
  }
}

bool raiseAp() {
  IPAddress ip(192, 168, 4, 1);
  if (apChannel < 1 || apChannel > 13) apChannel = 1;
  // Non richiamare softAP se è già su: lo spegnerebbe e farebbe fallire la station.
  if (apOn && WiFi.softAPIP() == ip) return true;
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(HELIX_AP_SSID, nullptr, apChannel)) {
    apOn = false;
    helixLog("warn", "softAP fallito, riprovo");
    return false;
  }
  if (!dnsOn) {
    dns.start(53, "*", ip);
    dnsOn = true;
  }
  apOn = true;
  helixLogf("info", "AP Helix-S3  ch %u", apChannel);
  return true;
}

void retuneAp(uint8_t channel) {
  if (channel < 1 || channel > 13) channel = apChannel ? apChannel : 1;
  if (apOn && channel == apChannel && WiFi.softAPIP()[0] != 0) return;
  apOn = false;
  apChannel = channel;
  raiseAp();
}

void dropAp() {
  if (!apOn && !dnsOn) return;
  if (dnsOn) {
    dns.stop();
    dnsOn = false;
  }
  // true = spegni solo l'AP e resta in station. Non richiamare WiFi.mode: staccherebbe la rete di casa.
  WiFi.softAPdisconnect(true);
  apOn = false;
}

}  // namespace

void wifiHold(bool on) { hold = on; }

bool wifiRouter(bool on, const char* ssid, const char* pass) {
  if (on) {
    if (!ssid || strlen(ssid) < 1 || strlen(ssid) > 32) return false;
    if (!pass || strlen(pass) < 8 || strlen(pass) > 63) return false;
    hold = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, pass);
    apOn = true;
    dns.start(53, "*", WiFi.softAPIP());
    dnsOn = true;
    wf = WF_APSTA;
    routed = true;
    if (ip_napt_enable) {
      ip_napt_enable((uint32_t)WiFi.softAPIP(), 1);
      helixLogf("info", "NAT su %s", WiFi.softAPIP().toString().c_str());
    } else {
      helixLogf("info", "router %s su %s, NAT assente in questo core", ssid, WiFi.softAPIP().toString().c_str());
    }
    return true;
  }
  if (routed && ip_napt_enable) ip_napt_enable((uint32_t)WiFi.softAPIP(), 0);
  routed = false;
  hold = false;
  WiFi.softAPdisconnect(true);
  apOn = false;
  dnsOn = false;
  if (fallback) raiseAp();
  wf = (WiFi.status() == WL_CONNECTED) ? (apOn ? WF_APSTA : WF_STA) : (apOn ? WF_AP : WF_DOWN);
  helixLog("info", "router spento, portale ripristinato");
  return true;
}

void requestScan(bool forJoin) {
  WiFi.scanDelete();
  scanBusy = false;
  scanAsk = false;
  // La scan fallisce se la station è ancora in connessione, anche se l'AP è acceso.
  if (forJoin || WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  scanForJoin = forJoin;
  scanArm = true;
  scanTries = 0;
  scanArmAt = millis();
}

void wifiInit() {
  pendMux = xSemaphoreCreateMutex();
  scanMux = xSemaphoreCreateMutex();
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setHostname(HELIX_HOSTNAME);
  WiFi.mode(WIFI_OFF);
  delay(80);
  WiFi.mode(WIFI_AP_STA);
  delay(80);
  esp_wifi_set_ps(WIFI_PS_NONE);
  loadNvs();
  // Il portale sale prima della station: resta visibile anche mentre la STA tenta il collegamento.
  if (fallback) raiseAp();
  if (credSsid[0]) {
    wf = WF_JOIN;
    mark = millis();
    requestScan(true);
    helixLogf("info", "NVS %s, cerco canale", credSsid);
  } else {
    wf = apOn ? WF_AP : WF_DOWN;
    mark = millis();
  }
  publishRadio();
}

static void publishScan(int n) {
  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray arr = doc["nets"].to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n && arr.size() < 16; i++) {
      String name = WiFi.SSID(i);
      if (!name.length()) continue;
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = name;
      o["rssi"] = WiFi.RSSI(i);
      o["sec"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "aperta" : "WPA2";
    }
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  if (scanMux) xSemaphoreTake(scanMux, portMAX_DELAY);
  strncpy(scanJson, out.c_str(), sizeof(scanJson) - 1);
  scanJson[sizeof(scanJson) - 1] = 0;
  scanReady = true;
  scanBusy = false;
  if (scanMux) xSemaphoreGive(scanMux);
}

void wifiPoll() {
  if (dnsOn) dns.processNextRequest();
  if (millis() - radioMark > (wf == WF_JOIN ? 250 : 1000)) {
    radioMark = millis();
    publishRadio();
  }
  // Lo sniffer tiene la radio: niente scan e niente riconnessione nello stesso giro.
  if (hold) {
    if (scanBusy || scanAsk || scanArm) {
      WiFi.scanDelete();
      scanBusy = false;
      scanAsk = false;
      scanArm = false;
      scanForJoin = false;
    }
    return;
  }

  if (fallback && !routed && !scanBusy && !scanArm && wf != WF_JOIN && wf != WF_STA && wf != WF_APSTA && !apOn) {
    static uint32_t apRetry = 0;
    if (millis() - apRetry > 4000) {
      apRetry = millis();
      raiseAp();
    }
  }

  if (pend) {
    char s[33];
    char p[65];
    if (scanBusy) {
      WiFi.scanDelete();
      scanBusy = false;
      scanAsk = false;
    }
    if (pendMux) xSemaphoreTake(pendMux, portMAX_DELAY);
    memcpy(s, (const char*)pendSsid, sizeof(s));
    memcpy(p, (const char*)pendPass, sizeof(p));
    pend = false;
    if (pendMux) xSemaphoreGive(pendMux);
    copyClip(credSsid, sizeof(credSsid), s);
    copyClip(credPass, sizeof(credPass), p);
    saveNvs();
    if (credSsid[0]) {
      if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
      wf = WF_JOIN;
      mark = millis();
      requestScan(true);
      helixLogf("info", "NVS %s, cerco canale", credSsid);
    }
  }

  if (scanArm && millis() - scanArmAt >= 300) {
    int n = WiFi.scanNetworks(false, true);
    if (n < 0 && scanTries < 3) {
      scanTries++;
      WiFi.disconnect(false, false);
      scanArmAt = millis();
    } else {
      scanArm = false;
      scanTries = 0;
      if (n < 0) {
        helixLog("warn", "scan non parte");
        n = -1;
      }
      if (scanForJoin) {
        int ch = 0;
        if (n > 0) {
          for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i).equalsIgnoreCase(credSsid)) {
              ch = WiFi.channel(i);
              break;
            }
          }
        }
        WiFi.scanDelete();
        scanForJoin = false;
        if (ch >= 1 && ch <= 13) {
          retuneAp((uint8_t)ch);
          WiFi.disconnect(false, false);
          WiFi.begin(credSsid, credPass, ch);
          mark = millis();
          helixLogf("info", "sta %s ch %d", credSsid, ch);
        } else if (n >= 0) {
          helixLog("warn", "solo 2.4GHz");
          wf = apOn ? WF_AP : WF_DOWN;
          mark = millis();
        } else if (credSsid[0]) {
          WiFi.begin(credSsid, credPass);
          mark = millis();
        }
      } else if (n >= 0) {
        helixLogf("info", "scan %d reti", n);
        publishScan(n);
      } else {
        publishScan(-1);
      }
    }
  }

  if (scanAsk && !scanBusy && !scanArm && !scanForJoin && wf != WF_JOIN && millis() - scanMark > 400) {
    scanMark = millis();
    requestScan(false);
  }

  if (wf == WF_JOIN && !scanForJoin && !scanArm) {
    if (WiFi.status() == WL_CONNECTED) {
      if (apOn && !routed) dropAp();
      wf = WF_STA;
      linkGrace = millis();
      helixLogf("info", "wifi ok %s", WiFi.localIP().toString().c_str());
    } else if (millis() - mark > HELIX_STA_TIMEOUT_MS) {
      WiFi.disconnect(false, false);
      helixLogf("warn", "timeout %s", credSsid);
      if (fallback && !apOn) raiseAp();
      wf = apOn ? WF_AP : WF_DOWN;
      mark = millis();
    }
  }

  if ((wf == WF_STA || wf == WF_APSTA) && WiFi.status() != WL_CONNECTED && millis() - linkGrace > 4000) {
    helixLog("warn", "wifi perso, riprovo");
    WiFi.reconnect();
    wf = WF_JOIN;
    mark = millis();
  }

  if (wf == WF_AP && credSsid[0] && !scanBusy && !scanArm && millis() - mark > 20000) {
    wf = WF_JOIN;
    mark = millis();
    requestScan(true);
  }
}

void wifiFill(Telemetry& t) {
  Radio r;
  readRadio(r);
  strncpy(t.mode, r.mode, sizeof(t.mode) - 1);
  t.mode[sizeof(t.mode) - 1] = 0;
  copyClip(t.ssid, sizeof(t.ssid), r.ssid);
  copyClip(t.ip, sizeof(t.ip), r.ip[0] ? r.ip : "0.0.0.0");
  t.rssi = r.rssi;
}

bool wifiApUp() { return apOn; }
bool wifiApFallback() { return fallback; }
bool wifiHostIsSta(const char* host) {
  if (!host || !host[0]) return false;
  Radio r;
  readRadio(r);
  return r.up && strcmp(host, r.ip) == 0;
}

void wifiRequestConnect(const char* ssid, const char* pass) {
  if (pendMux) xSemaphoreTake(pendMux, portMAX_DELAY);
  copyClip(pendSsid, sizeof(pendSsid), ssid);
  copyClip(pendPass, sizeof(pendPass), pass);
  pend = true;
  if (pendMux) xSemaphoreGive(pendMux);
}

void wifiSetApFallback(bool on) {
  fallback = on;
  saveNvs();
  if (on) {
    raiseAp();
    if (wf == WF_STA) wf = WF_APSTA;
    if (wf == WF_DOWN) wf = WF_AP;
  } else {
    dropAp();
    if (wf == WF_APSTA) wf = WF_STA;
    if (wf == WF_AP) wf = credSsid[0] ? WF_JOIN : WF_DOWN;
  }
  helixLogf("info", "ap fallback %s", on ? "on" : "off");
}

String wifiScanJson() {
  if (scanBusy || scanAsk || scanArm || scanForJoin) return "{\"scanning\":true}";
  if (!scanReady) {
    scanAsk = true;
    return "{\"scanning\":true}";
  }
  char tmp[sizeof(scanJson)];
  if (scanMux) xSemaphoreTake(scanMux, portMAX_DELAY);
  memcpy(tmp, scanJson, sizeof(tmp));
  scanReady = false;
  if (scanMux) xSemaphoreGive(scanMux);
  return String(tmp);
}

String wifiStatusText() {
  Radio r;
  readRadio(r);
  String s = "mode ";
  s += r.mode[0] ? r.mode : "off";
  s += "\nssid ";
  s += r.up && r.ssid[0] ? r.ssid : "—";
  s += "\nip ";
  s += r.ip[0] ? r.ip : "0.0.0.0";
  s += "\nnvs ";
  s += credSsid[0] ? credSsid : "—";
  s += "\nap-fallback ";
  s += fallback ? "on" : "off";
  return s;
}

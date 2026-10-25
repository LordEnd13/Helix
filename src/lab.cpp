#include "lab.h"

#include "config.h"
#include "gpio_ctrl.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/ip_addr.h>
#include <lwip/netif.h>
#include <ping/ping_sock.h>

#include <HardwareSerial.h>
#include <string.h>

namespace {

constexpr int kScopePin = 4;
constexpr int kProbePin = 5;
constexpr int kBuzzPin = 6;
constexpr int kUartRx = 18;
constexpr int kUartTx = 17;
constexpr uint8_t kLogic[] = {1, 7, 14, 15, 16, 21};

HardwareSerial UartExt(1);
char pending[1400];
volatile bool hasCmd = false;
SemaphoreHandle_t mux = nullptr;

bool scopeOn = false;
uint32_t scopeUs = 100;
int scopeTrig = 1600;
uint32_t scopeMark = 0;
uint32_t probeMark = 0;

bool probeOn = false;
bool lastShort = false;
uint32_t beepUntil = 0;

bool uartOn = false;
char uartBuf[480];
size_t uartLen = 0;
uint32_t uartMark = 0;

bool scanOn = false;
bool scanPorts = false;
uint8_t scanHost = 1;
IPAddress scanNet;
uint32_t pingWait = 0;
esp_ping_handle_t pingH = nullptr;
volatile int pingRes = 0;
uint16_t portLeft = 0;
IPAddress portIp;
const uint16_t kPorts[] = {80, 443, 22, 8080};
uint8_t portIdx = 0;
char portList[32];

bool radioOn = false;
uint8_t radioCh = 1;
uint32_t radioMark = 0;

struct Hit {
  char ssid[33];
  int8_t rssi;
  uint16_t n;
};
Hit hits[12];
portMUX_TYPE sniffMux = portMUX_INITIALIZER_UNLOCKED;

bool nowOn = false;
uint8_t nowMac[6] = {0};
char nowLast[160];

char script[1024];
int scriptPc = 0;
int scriptSkip = 0;
int loopLeft = 0;
int loopPc = -1;
uint32_t waitUntil = 0;
bool scriptRun = false;

void sendDoc(JsonDocument& doc) {
  char buf[HELIX_MSG];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n && n < sizeof(buf)) helixEnqueueTx(buf);
}

void beep(uint16_t ms) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(kBuzzPin, 2000, 8);
  ledcWrite(kBuzzPin, 140);
#else
  ledcSetup(6, 2000, 8);
  ledcAttachPin(kBuzzPin, 6);
  ledcWrite(6, 140);
#endif
  beepUntil = millis() + ms;
}

void beepPoll() {
  if (!beepUntil || millis() < beepUntil) return;
  beepUntil = 0;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(kBuzzPin, 0);
#else
  ledcWrite(6, 0);
#endif
}

void scopeFrame() {
  uint16_t raw[64];
  analogReadResolution(12);
  analogSetPinAttenuation(kScopePin, ADC_11db);
  pinMode(kScopePin, INPUT);
  uint32_t guard = micros();
  bool armed = false;
  while ((uint32_t)(micros() - guard) < 20000) {
    int v = analogReadMilliVolts(kScopePin);
    if (!armed && v < scopeTrig - 80) armed = true;
    if (armed && v >= scopeTrig) break;
  }
  for (int i = 0; i < 64; i++) {
    raw[i] = (uint16_t)analogReadMilliVolts(kScopePin);
    if (scopeUs > 30) delayMicroseconds(scopeUs);
  }
  int vmin = 4000, vmax = 0, mid = 0;
  for (int i = 0; i < 64; i++) {
    if (raw[i] < vmin) vmin = raw[i];
    if (raw[i] > vmax) vmax = raw[i];
  }
  mid = (vmin + vmax) / 2;
  int crosses = 0;
  bool high = raw[0] > mid;
  for (int i = 1; i < 64; i++) {
    bool now = raw[i] > mid;
    if (!high && now) crosses++;
    high = now;
  }
  float windowMs = (64.f * scopeUs) / 1000.f;
  int hz = windowMs > 0 ? (int)((crosses * 1000.f) / windowMs) : 0;
  JsonDocument doc;
  doc["t"] = "scope";
  doc["us"] = scopeUs;
  doc["vmin"] = vmin;
  doc["vmax"] = vmax;
  doc["hz"] = hz;
  JsonArray arr = doc["v"].to<JsonArray>();
  for (int i = 0; i < 64; i++) arr.add(raw[i]);
  sendDoc(doc);
}

void probeFrame() {
  pinMode(kProbePin, INPUT_PULLUP);
  bool cont = digitalRead(kProbePin) == LOW;
  if (cont && !lastShort) beep(60);
  lastShort = cont;
  JsonDocument doc;
  doc["t"] = "probe";
  doc["cont"] = cont;
  JsonArray arr = doc["pins"].to<JsonArray>();
  for (uint8_t pin : kLogic) {
    pinMode(pin, INPUT);
    JsonObject o = arr.add<JsonObject>();
    o["p"] = pin;
    o["v"] = digitalRead(pin) ? 1 : 0;
  }
  sendDoc(doc);
}

void uartDrain() {
  while (UartExt.available() && uartLen + 1 < sizeof(uartBuf)) {
    char c = (char)UartExt.read();
    if (c == '\r') continue;
    uartBuf[uartLen++] = c;
    if (c == '\n' || uartLen > 180) break;
  }
  if (!uartLen) return;
  if (uartBuf[uartLen - 1] != '\n' && millis() - uartMark < 40 && uartLen < 180) return;
  uartBuf[uartLen] = 0;
  JsonDocument doc;
  doc["t"] = "uart";
  doc["text"] = uartBuf;
  sendDoc(doc);
  uartLen = 0;
  uartMark = millis();
}

bool privateNet(IPAddress ip) {
  if (ip[0] == 10) return true;
  if (ip[0] == 192 && ip[1] == 168) return true;
  if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
  return false;
}

void onPingOk(esp_ping_handle_t, void*) { pingRes = 1; }
void onPingTo(esp_ping_handle_t, void*) { pingRes = -1; }
void onPingEnd(esp_ping_handle_t, void*) { pingRes = pingRes == 0 ? -1 : pingRes; }

void scanStep() {
  if (WiFi.status() != WL_CONNECTED) {
    scanOn = false;
    helixLog("warn", "scansione solo in station, sulla propria LAN");
    return;
  }
  if (pingH) {
    if (pingRes == 0 && millis() - pingWait < 250) return;
    uint32_t addr = (uint32_t)portIp;
    char mac[18] = "";
    if (pingRes > 0 && netif_default) {
      ip4_addr_t ip4;
      ip4.addr = addr;
      struct eth_addr* eth = nullptr;
      const ip4_addr_t* found = nullptr;
      if (etharp_find_addr(netif_default, &ip4, &eth, &found) >= 0 && eth) {
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", eth->addr[0], eth->addr[1], eth->addr[2],
                 eth->addr[3], eth->addr[4], eth->addr[5]);
      }
    }
    if (pingRes > 0 && scanPorts && portIdx < 4) {
      WiFiClient c;
      c.setTimeout(80);
      if (c.connect(portIp, kPorts[portIdx], 120)) {
        if (portLeft) strncat(portList, ",", sizeof(portList) - strlen(portList) - 1);
        char num[8];
        snprintf(num, sizeof(num), "%u", kPorts[portIdx]);
        strncat(portList, num, sizeof(portList) - strlen(portList) - 1);
        portLeft = 1;
        c.stop();
      }
      portIdx++;
      if (portIdx < 4) return;
    }
    if (pingRes > 0) {
      JsonDocument doc;
      doc["t"] = "lan";
      doc["ip"] = portIp.toString();
      doc["mac"] = mac;
      doc["ports"] = portList;
      doc["host"] = scanHost;
      sendDoc(doc);
    }
    esp_ping_delete_session(pingH);
    pingH = nullptr;
    pingRes = 0;
    scanHost++;
    if (scanHost > 254) scanOn = false;
    return;
  }
  if (scanHost == 0 || scanHost > 254) {
    scanOn = false;
    return;
  }
  IPAddress self = WiFi.localIP();
  portIp = scanNet;
  portIp[3] = scanHost;
  if (portIp == self) {
    scanHost++;
    return;
  }
  ip_addr_t target;
  ipaddr_aton(portIp.toString().c_str(), &target);
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = 1;
  cfg.timeout_ms = 180;
  cfg.interval_ms = 200;
  esp_ping_callbacks_t cbs{};
  cbs.on_ping_success = onPingOk;
  cbs.on_ping_timeout = onPingTo;
  cbs.on_ping_end = onPingEnd;
  pingRes = 0;
  portIdx = 0;
  portLeft = 0;
  portList[0] = 0;
  if (esp_ping_new_session(&cfg, &cbs, &pingH) != ESP_OK) {
    scanOn = false;
    return;
  }
  esp_ping_start(pingH);
  pingWait = millis();
}

void onSniff(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!radioOn || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  if ((p[0] & 0xFC) != 0x80) return;
  uint8_t n = p[37];
  if (n > 32) return;
  char name[33];
  if (!n) {
    strncpy(name, "(nascosta)", sizeof(name));
  } else {
    memcpy(name, p + 38, n);
    name[n] = 0;
    for (uint8_t i = 0; i < n; i++)
      if (name[i] < 32 || name[i] == '"') name[i] = ' ';
  }
  int8_t rssi = pkt->rx_ctrl.rssi;
  portENTER_CRITICAL(&sniffMux);
  int slot = -1;
  for (int i = 0; i < 12; i++) {
    if (hits[i].n && !strcmp(hits[i].ssid, name)) {
      slot = i;
      break;
    }
    if (!hits[i].n && slot < 0) slot = i;
  }
  if (slot >= 0) {
    strncpy(hits[slot].ssid, name, sizeof(hits[slot].ssid) - 1);
    if (rssi > hits[slot].rssi || !hits[slot].n) hits[slot].rssi = rssi;
    if (hits[slot].n < 60000) hits[slot].n++;
  }
  portEXIT_CRITICAL(&sniffMux);
}

void radioFrame() {
  JsonDocument doc;
  doc["t"] = "radio";
  doc["ch"] = radioCh;
  JsonArray arr = doc["nets"].to<JsonArray>();
  portENTER_CRITICAL(&sniffMux);
  for (int i = 0; i < 12; i++) {
    if (!hits[i].n) continue;
    JsonObject o = arr.add<JsonObject>();
    o["s"] = hits[i].ssid;
    o["r"] = hits[i].rssi;
    o["n"] = hits[i].n;
  }
  portEXIT_CRITICAL(&sniffMux);
  sendDoc(doc);
}

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info ? info->src_addr : nullptr;
  if (!mac) return;
#else
void onNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len < 0) len = 0;
  if (len > 120) len = 120;
  char text[140];
  int n = snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X ", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  memcpy(text + n, data, len);
  text[n + len] = 0;
  strncpy(nowLast, text, sizeof(nowLast) - 1);
}

bool parseMac(const char* s, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] > 255) return false;
    out[i] = (uint8_t)v[i];
  }
  return true;
}

void scriptLog(const char* msg) {
  JsonDocument doc;
  doc["t"] = "script";
  doc["line"] = msg;
  sendDoc(doc);
}

const char* skipSpace(const char* s) {
  while (*s == ' ') s++;
  return s;
}

void scriptStep() {
  if (millis() < waitUntil) return;
  if (!script[scriptPc]) {
    scriptRun = false;
    scriptLog("fine");
    return;
  }
  char line[120];
  int i = 0;
  while (script[scriptPc] && script[scriptPc] != '\n' && i + 1 < (int)sizeof(line)) line[i++] = script[scriptPc++];
  if (script[scriptPc] == '\n') scriptPc++;
  line[i] = 0;
  const char* s = skipSpace(line);
  if (!s[0] || s[0] == '#') return;
  if (scriptSkip) {
    if (!strncmp(s, "end", 3)) scriptSkip--;
    else if (!strncmp(s, "if ", 3)) scriptSkip++;
    return;
  }
  if (!strncmp(s, "end", 3)) return;
  if (!strncmp(s, "log ", 4)) {
    scriptLog(skipSpace(s + 4));
    return;
  }
  if (!strncmp(s, "wait ", 5)) {
    waitUntil = millis() + (uint32_t)atoi(s + 5);
    return;
  }
  if (!strncmp(s, "beep ", 5)) {
    beep((uint16_t)atoi(s + 5));
    return;
  }
  if (!strncmp(s, "set ", 4)) {
    int pin = 0, val = 0;
    if (sscanf(s + 4, "%d %d", &pin, &val) == 2 && helixPinAllowed(pin)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, val ? HIGH : LOW);
    }
    return;
  }
  if (!strncmp(s, "pulse ", 6)) {
    int pin = 0, ms = 0;
    if (sscanf(s + 6, "%d %d", &pin, &ms) == 2 && helixPinAllowed(pin)) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
      delay(ms > 200 ? 200 : ms);
      digitalWrite(pin, LOW);
    }
    return;
  }
  if (!strncmp(s, "if pin ", 7)) {
    int pin = 0, val = 0;
    if (sscanf(s + 7, "%d == %d", &pin, &val) == 2 && helixPinAllowed(pin)) {
      pinMode(pin, INPUT);
      int cur = digitalRead(pin) ? 1 : 0;
      if (cur != val) scriptSkip = 1;
    }
    return;
  }
  if (!strncmp(s, "now ", 4) && nowOn) {
    const char* body = skipSpace(s + 4);
    const char* sp = strchr(body, ' ');
    char macs[20];
    const char* msg = "";
    if (sp && sp - body < 18) {
      memcpy(macs, body, sp - body);
      macs[sp - body] = 0;
      msg = skipSpace(sp + 1);
    }
    uint8_t mac[6];
    if (parseMac(macs, mac)) {
      esp_now_send(mac, (const uint8_t*)msg, strlen(msg));
    }
    return;
  }
  scriptLog("riga ignorata");
}

void handle(JsonDocument& doc) {
  const char* t = doc["t"] | "";
  const char* op = doc["op"] | "start";
  if (!strcmp(t, "scope")) {
    scopeOn = strcmp(op, "stop") != 0;
    if (doc["us"].is<int>()) {
      int us = doc["us"];
      if (us < 20) us = 20;
      if (us > 800) us = 800;
      scopeUs = (uint32_t)us;
    }
    if (doc["trig"].is<int>()) scopeTrig = doc["trig"];
    return;
  }
  if (!strcmp(t, "probe")) {
    if (!strcmp(op, "pulse")) {
      int pin = doc["pin"] | 2;
      int ms = doc["ms"] | 50;
      if (ms < 1) ms = 1;
      if (ms > 500) ms = 500;
      if (helixPinAllowed(pin)) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        delay(ms);
        digitalWrite(pin, LOW);
      }
      return;
    }
    probeOn = strcmp(op, "stop") != 0;
    if (probeOn) pinMode(kProbePin, INPUT_PULLUP);
    return;
  }
  if (!strcmp(t, "uart")) {
    if (!strcmp(op, "stop")) {
      uartOn = false;
      return;
    }
    if (!strcmp(op, "write")) {
      if (uartOn) UartExt.print(doc["text"] | "");
      return;
    }
    uint32_t baud = doc["baud"] | 115200;
    if (baud < 300 || baud > 921600) baud = 115200;
    UartExt.begin(baud, SERIAL_8N1, kUartRx, kUartTx);
    uartOn = true;
    helixLogf("info", "UART1 %u  RX %d TX %d", (unsigned)baud, kUartRx, kUartTx);
    return;
  }
  if (!strcmp(t, "lan")) {
    if (!strcmp(op, "stop")) {
      scanOn = false;
      return;
    }
    if (WiFi.status() != WL_CONNECTED || !privateNet(WiFi.localIP())) {
      helixLog("warn", "scansione rifiutata: serve la station su una rete privata");
      return;
    }
    scanNet = WiFi.localIP();
    scanNet[3] = 0;
    scanHost = 1;
    scanPorts = doc["ports"] | false;
    scanOn = true;
    helixLogf("info", "scansione %s.0/24", scanNet.toString().c_str());
    return;
  }
  if (!strcmp(t, "route")) {
    if (!strcmp(op, "stop")) {
      wifiRouter(false, "", "");
      return;
    }
    bool ok = wifiRouter(true, doc["ssid"] | "", doc["pass"] | "");
    JsonDocument out;
    out["t"] = "route";
    out["ok"] = ok;
    out["ip"] = WiFi.softAPIP().toString();
    sendDoc(out);
    return;
  }
  if (!strcmp(t, "radio")) {
    if (!strcmp(op, "stop")) {
      radioOn = false;
      esp_wifi_set_promiscuous(false);
      wifiHold(false);
      helixLog("info", "sniffer spento");
      return;
    }
    memset(hits, 0, sizeof(hits));
    radioOn = true;
    radioCh = 1;
    wifiHold(true);
    esp_wifi_set_promiscuous_rx_cb(onSniff);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(radioCh, WIFI_SECOND_CHAN_NONE);
    radioMark = millis();
    helixLog("warn", "sniffer: la station si stacca finché non premi stop");
    return;
  }
  if (!strcmp(t, "now")) {
    if (!nowOn) {
      if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onNowRecv);
        nowOn = true;
      } else {
        helixLog("errore", "ESP-NOW non parte");
        return;
      }
    }
    if (!strcmp(op, "peer")) {
      uint8_t mac[6];
      if (!parseMac(doc["mac"] | "", mac)) return;
      memcpy(nowMac, mac, 6);
      esp_now_peer_info_t peer{};
      memcpy(peer.peer_addr, mac, 6);
      peer.channel = 0;
      peer.encrypt = false;
      if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(&peer);
      helixLog("info", "peer ESP-NOW aggiunto");
      return;
    }
    if (!strcmp(op, "send")) {
      const char* text = doc["text"] | "";
      esp_now_send(nowMac, (const uint8_t*)text, strlen(text));
    }
    return;
  }
  if (!strcmp(t, "script")) {
    if (!strcmp(op, "stop")) {
      scriptRun = false;
      scriptLog("stop");
      return;
    }
    const char* src = doc["src"] | "";
    strncpy(script, src, sizeof(script) - 1);
    script[sizeof(script) - 1] = 0;
    scriptPc = 0;
    scriptSkip = 0;
    waitUntil = 0;
    scriptRun = true;
    scriptLog("avvio");
  }
}

}  // namespace

bool labIsType(const char* t) {
  return t && (!strcmp(t, "scope") || !strcmp(t, "probe") || !strcmp(t, "uart") || !strcmp(t, "lan") ||
               !strcmp(t, "route") || !strcmp(t, "radio") || !strcmp(t, "now") || !strcmp(t, "script"));
}

void labSubmit(const char* json) {
  if (!json || !mux) return;
  if (xSemaphoreTake(mux, pdMS_TO_TICKS(20)) != pdTRUE) return;
  strncpy(pending, json, sizeof(pending) - 1);
  pending[sizeof(pending) - 1] = 0;
  hasCmd = true;
  xSemaphoreGive(mux);
}

void labInit() {
  mux = xSemaphoreCreateMutex();
  pinMode(kBuzzPin, OUTPUT);
  helixLog("info", "lab: scope GPIO4, sonda GPIO5, buzzer GPIO6, UART RX18 TX17");
}

void labPoll() {
  beepPoll();
  if (hasCmd && mux && xSemaphoreTake(mux, 0) == pdTRUE) {
    char local[1400];
    memcpy(local, pending, sizeof(local));
    hasCmd = false;
    xSemaphoreGive(mux);
    JsonDocument doc;
    if (!deserializeJson(doc, local)) handle(doc);
  }
  if (nowLast[0]) {
    JsonDocument doc;
    doc["t"] = "now";
    doc["text"] = nowLast;
    nowLast[0] = 0;
    sendDoc(doc);
  }
  if (scopeOn && millis() - scopeMark > 150) {
    scopeMark = millis();
    scopeFrame();
  }
  if (probeOn && millis() - probeMark > 250) {
    probeMark = millis();
    probeFrame();
  }
  if (uartOn) uartDrain();
  if (scanOn) scanStep();
  if (radioOn && millis() - radioMark > 350) {
    radioMark = millis();
    radioFrame();
    portENTER_CRITICAL(&sniffMux);
    memset(hits, 0, sizeof(hits));
    portEXIT_CRITICAL(&sniffMux);
    radioCh = radioCh >= 13 ? 1 : radioCh + 1;
    esp_wifi_set_channel(radioCh, WIFI_SECOND_CHAN_NONE);
  }
  if (scriptRun) {
    for (int i = 0; i < 8 && scriptRun; i++) scriptStep();
  }
}

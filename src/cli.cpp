#include "cli.h"

#include "config.h"
#include "config_store.h"
#include "fs_api.h"
#include "gpio_ctrl.h"
#include "sd_card.h"
#include "telemetry.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <esp_system.h>

static uint16_t sSeq = 0;
static uint8_t sWr = 0;
static struct {
  uint16_t n;
  char line[140];
} sRows[32];

static void remember(const String& line) {
  sSeq++;
  sRows[sWr].n = sSeq;
  strncpy(sRows[sWr].line, line.c_str(), sizeof(sRows[sWr].line) - 1);
  sRows[sWr].line[sizeof(sRows[sWr].line) - 1] = 0;
  sWr = (sWr + 1) % 32;
}

static void reply(const String& text) {
  // Una riga per messaggio: il client le stampa in ordine.
  int from = 0;
  while (from <= (int)text.length()) {
    int nl = text.indexOf('\n', from);
    String line = nl < 0 ? text.substring(from) : text.substring(from, nl);
    JsonDocument doc;
    doc["t"] = "cli";
    doc["line"] = line;
    remember(line);
    char buf[HELIX_MSG];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n) {
      for (int i = 0; i < 4 && !helixEnqueueTx(buf); i++) vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (nl < 0) break;
    from = nl + 1;
  }
}

static void runLine(char* raw) {
  while (*raw == ' ') raw++;
  if (!*raw) return;

  char* cmd = strtok(raw, " ");
  if (!cmd) return;
  char* a1 = strtok(nullptr, " ");
  char* a2 = strtok(nullptr, " ");
  char* a3 = strtok(nullptr, " ");

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    reply(
        "help\nstatus\nfree\nuptime\ntemp\nls [path]\ncat <file>\nrm <path>\nmkdir <path>\n"
        "sd\nsd-ls [path]\nsd-cat <file>\nsd-rm <path>\nsd-mkdir <path>\napps\n"
        "wifi-scan\nwifi-status\nwifi-connect SSID PASS\nwifi-ap on|off\n"
        "gpio\ngpio N mode in|out|pwm|adc|free\ngpio N write 0|1\ngpio N pwm DUTY [Hz]\ngpio N read\n"
        "reboot\necho ...\nversion\nwhoami");
    return;
  }
  if (!strcmp(cmd, "echo")) {
    String rest;
    if (a1) rest += a1;
    if (a2) rest += String(" ") + a2;
    if (a3) rest += String(" ") + a3;
    reply(rest);
    return;
  }
  if (!strcmp(cmd, "reboot")) {
    reply("riavvio");
    helixLog("warn", "reboot");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP.restart();
    return;
  }
  if (!strcmp(cmd, "uptime")) {
    reply(String(millis() / 1000) + " s");
    return;
  }
  if (!strcmp(cmd, "temp")) {
    Telemetry t;
    telemetryCopy(t);
    reply(String(t.tempC, 1) + " C");
    return;
  }
  if (!strcmp(cmd, "status")) {
    Telemetry t;
    telemetryCopy(t);
    String msg = "helix " + String(HELIX_VERSION) + "  ESP32-S3 @ 240 MHz\nmode " + t.mode + "  ssid " +
                 (t.ssid[0] ? t.ssid : "—") + "  ip " + t.ip + "\nrssi " + String((int)t.rssi) + " dBm\nuptime " +
                 String((unsigned)t.uptimeS) + " s  temp " + String(t.tempC, 1) + " C\ncpu0 " + String(t.cpu0, 0) +
                 "%  cpu1 " + String(t.cpu1, 0) + "%\noled " + (t.oledOk ? "ok" : "assente") + "\n" + sdStatusText();
    reply(msg);
    return;
  }
  if (!strcmp(cmd, "free") || !strcmp(cmd, "heap") || !strcmp(cmd, "psram")) {
    Telemetry t;
    telemetryCopy(t);
    char buf[240];
    snprintf(buf, sizeof(buf), "heap   %u / %u\npsram  %u / %u\nlittlefs %u / %u\nsd %llu / %llu", (unsigned)t.heapFree,
             (unsigned)t.heapTotal, (unsigned)t.psramFree, (unsigned)t.psramTotal, (unsigned)t.fsUsed,
             (unsigned)t.fsTotal, (unsigned long long)t.sdUsed, (unsigned long long)t.sdTotal);
    reply(buf);
    return;
  }
  if (!strcmp(cmd, "ls")) {
    String path = a1 ? a1 : "/";
    if (!path.startsWith("/")) path = "/" + path;
    String json = fsListJson(path);
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["ok"].as<bool>()) {
      reply(String("ls: ") + (doc["err"] | "errore"));
      return;
    }
    JsonArray items = doc["items"].as<JsonArray>();
    if (items.size() == 0) {
      reply("(vuoto)");
      return;
    }
    String out;
    for (JsonObject o : items) {
      if (out.length()) out += "\n";
      out += o["dir"].as<bool>() ? "d  " : "-  ";
      out += String((uint32_t)o["size"].as<uint32_t>());
      out += "  ";
      out += o["name"].as<const char*>();
    }
    reply(out);
    return;
  }
  if (!strcmp(cmd, "cat")) {
    if (!a1) {
      reply("uso: cat <file>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String body, err;
    if (!fsReadText(path, body, err)) reply(String("cat: ") + err);
    else reply(body.length() ? body : String(""));
    return;
  }
  if (!strcmp(cmd, "rm")) {
    if (!a1) {
      reply("uso: rm <percorso>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String err;
    reply(fsDelete(path, err) ? "ok" : String("rm: ") + err);
    return;
  }
  if (!strcmp(cmd, "mkdir")) {
    if (!a1) {
      reply("uso: mkdir <cartella>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String err;
    reply(fsMkdir(path, err) ? "ok" : String("mkdir: ") + err);
    return;
  }
  if (!strcmp(cmd, "sd")) {
    reply(sdStatusText());
    return;
  }
  if (!strcmp(cmd, "apps")) {
    String json = sdAppsJson();
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["ok"].as<bool>()) {
      reply(String("apps: ") + (doc["err"] | "errore"));
      return;
    }
    JsonArray items = doc["apps"].as<JsonArray>();
    if (items.size() == 0) {
      reply("nessuna app in /apps");
      return;
    }
    String out;
    for (JsonObject o : items) {
      if (out.length()) out += "\n";
      out += o["title"].as<const char*>();
      out += "  ";
      out += o["path"].as<const char*>();
    }
    reply(out);
    return;
  }
  if (!strcmp(cmd, "sd-ls")) {
    if (!sdReady()) {
      reply("sd assente");
      return;
    }
    String path = a1 ? a1 : "/";
    if (!path.startsWith("/")) path = "/" + path;
    String json = sdListJson(path);
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["ok"].as<bool>()) {
      reply(String("sd-ls: ") + (doc["err"] | "errore"));
      return;
    }
    JsonArray items = doc["items"].as<JsonArray>();
    if (items.size() == 0) {
      reply("(vuoto)");
      return;
    }
    String out;
    for (JsonObject o : items) {
      if (out.length()) out += "\n";
      out += o["dir"].as<bool>() ? "d  " : "-  ";
      out += String((uint32_t)o["size"].as<uint32_t>());
      out += "  ";
      out += o["name"].as<const char*>();
    }
    reply(out);
    return;
  }
  if (!strcmp(cmd, "sd-cat")) {
    if (!a1) {
      reply("uso: sd-cat <file>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String body, err;
    if (!sdReadText(path, body, err)) reply(String("sd-cat: ") + err);
    else reply(body.length() ? body : String(""));
    return;
  }
  if (!strcmp(cmd, "sd-rm")) {
    if (!a1) {
      reply("uso: sd-rm <percorso>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String err;
    reply(sdDelete(path, err) ? "ok" : String("sd-rm: ") + err);
    return;
  }
  if (!strcmp(cmd, "sd-mkdir")) {
    if (!a1) {
      reply("uso: sd-mkdir <cartella>");
      return;
    }
    String path = a1;
    if (!path.startsWith("/")) path = "/" + path;
    String err;
    reply(sdMkdir(path, err) ? "ok" : String("sd-mkdir: ") + err);
    return;
  }
  if (!strcmp(cmd, "wifi-scan")) {
    reply("scansione…");
    reply(wifiScanJson());
    return;
  }
  if (!strcmp(cmd, "wifi-status")) {
    reply(wifiStatusText());
    return;
  }
  if (!strcmp(cmd, "wifi-connect")) {
    if (!a1) {
      reply("uso: wifi-connect SSID PASSWORD");
      return;
    }
    const char* pass = a2 ? a2 : "";
    if (pass[0] && strlen(pass) < 8) {
      reply("wifi: password WPA troppo corta");
      return;
    }
    wifiRequestConnect(a1, pass);
    reply(String("NVS in aggiornamento, connessione a ") + a1);
    return;
  }
  if (!strcmp(cmd, "wifi-ap")) {
    bool on = !(a1 && !strcmp(a1, "off"));
    wifiSetApFallback(on);
    reply(String("ap fallback ") + (on ? "on" : "off"));
    return;
  }
  if (!strcmp(cmd, "gpio")) {
    if (!a1) {
      String dump;
      gpioDump(dump);
      reply(dump);
      return;
    }
    int pin = atoi(a1);
    if (!helixPinAllowed(pin)) {
      if (pin >= 8 && pin <= 9) reply(String("GPIO") + pin + " riservato all'OLED I2C");
      else if (pin >= 10 && pin <= 13) reply(String("GPIO") + pin + " riservato alla SD SPI");
      else if (pin >= 26 && pin <= 37) reply(String("GPIO") + pin + " riservato a flash e PSRAM");
      else reply(String("GPIO") + pin + " non disponibile (riservato o assente)");
      return;
    }
    const char* op = a2 ? a2 : "read";
    if (!strcmp(op, "read")) {
      String line;
      gpioDescribe(pin, line);
      reply(line);
      return;
    }
    if (!strcmp(op, "mode")) {
      if (!a3 || !gpioSetMode(pin, a3)) reply("mode: in|out|pwm|adc|free");
      else reply(String("GPIO") + pin + " → " + a3);
      return;
    }
    if (!strcmp(op, "write")) {
      if (!gpioWrite(pin, a3 ? atoi(a3) : 0)) reply("pin non in uscita");
      else reply(String("GPIO") + pin + " = " + (atoi(a3) ? "1" : "0"));
      return;
    }
    if (!strcmp(op, "pwm")) {
      int duty = a3 ? atoi(a3) : 0;
      char* a4 = strtok(nullptr, " ");
      int freq = a4 ? atoi(a4) : 5000;
      if (!gpioPwm(pin, duty, freq)) reply("pwm rifiutato");
      else reply(String("GPIO") + pin + " pwm " + duty + "/255");
      return;
    }
    reply("uso: gpio | gpio N read|write|mode|pwm");
    return;
  }
  if (!strcmp(cmd, "log")) {
    String dump;
    helixLogDump(dump);
    reply(dump.length() ? dump : String("—"));
    return;
  }
  if (!strcmp(cmd, "version")) {
    reply(String("helix ") + HELIX_VERSION + " ESP32-S3-N16R8");
    return;
  }
  if (!strcmp(cmd, "whoami")) {
    reply(String(configUser()) + (configLogin("") ? "\nsenza password" : "\npassword attiva"));
    return;
  }
  reply(String("comando sconosciuto: ") + cmd + "  (help)");
}

void cliSubmit(const char* line) {
  if (!gCli || !line) return;
  NetMsg m{};
  strncpy(m.text, line, sizeof(m.text) - 1);
  xQueueSend(gCli, &m, 0);
}

void cliPoll() {
  if (!gCli) return;
  NetMsg m;
  while (xQueueReceive(gCli, &m, 0) == pdTRUE) runLine(m.text);
}

String cliSinceJson(uint16_t since) {
  JsonDocument doc;
  doc["seq"] = sSeq;
  JsonArray arr = doc["lines"].to<JsonArray>();
  for (int i = 0; i < 32; i++) {
    uint16_t n = sRows[(sWr + i) % 32].n;
    if (!n) continue;
    int16_t delta = (int16_t)(n - since);
    if (delta <= 0 || delta > 40) continue;
    JsonObject o = arr.add<JsonObject>();
    o["n"] = n;
    o["line"] = sRows[(sWr + i) % 32].line;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

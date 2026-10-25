#include "sd_card.h"

#include "fs_api.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>

static SemaphoreHandle_t sMux = nullptr;
static bool sSpi = false;
static bool sReady = false;
static bool sBegan = false;
static bool sMissLogged = false;
static uint32_t sRetryAt = 0;
static uint64_t sUsed = 0;
static uint64_t sTotal = 0;
static char sType[8] = {0};
static uint32_t sCacheAt = 0;
static File sUpload;
static bool sUploadOpen = false;

static bool lock(TickType_t ticks = pdMS_TO_TICKS(3000)) {
  if (!sMux) return true;
  return xSemaphoreTake(sMux, ticks) == pdTRUE;
}

static void unlock() {
  if (sMux) xSemaphoreGive(sMux);
}

static const char* typeName(uint8_t t) {
  if (t == CARD_MMC) return "MMC";
  if (t == CARD_SD) return "SD";
  if (t == CARD_SDHC) return "SDHC";
  return "?";
}

static void refreshLocked() {
  if (!sReady) {
    sUsed = 0;
    sTotal = 0;
    sType[0] = 0;
    return;
  }
  const uint32_t now = millis();
  if (sCacheAt && now - sCacheAt < 5000) return;
  sTotal = SD.totalBytes();
  sUsed = SD.usedBytes();
  strncpy(sType, typeName(SD.cardType()), sizeof(sType) - 1);
  sType[sizeof(sType) - 1] = 0;
  sCacheAt = now;
}

static bool mountLocked() {
  if (sReady) return true;
  if (!sSpi) {
    pinMode(HELIX_SD_CS, OUTPUT);
    digitalWrite(HELIX_SD_CS, HIGH);
    SPI.begin(HELIX_SD_SCK, HELIX_SD_MISO, HELIX_SD_MOSI, HELIX_SD_CS);
    sSpi = true;
  } else if (sBegan) {
    SD.end();
  }
  sBegan = true;
  if (!SD.begin(HELIX_SD_CS, SPI, HELIX_SD_HZ)) {
    sReady = false;
    if (!sMissLogged) {
      helixLog("warn", "SD assente: CS 10 SCK 12 MISO 13 MOSI 11");
      sMissLogged = true;
    }
    return false;
  }
  sReady = true;
  sMissLogged = false;
  sCacheAt = 0;
  if (!SD.exists("/apps")) SD.mkdir("/apps");
  refreshLocked();
  helixLogf("info", "SD %s  %llu B", sType, (unsigned long long)sTotal);
  return true;
}

bool sdInit() {
  sMux = xSemaphoreCreateMutex();
  sRetryAt = millis();
  bool ok = false;
  if (lock()) {
    ok = mountLocked();
    unlock();
  }
  return ok;
}

void sdPoll() {
  if (sReady) {
    if (lock(pdMS_TO_TICKS(50))) {
      refreshLocked();
      unlock();
    }
    return;
  }
  if (millis() - sRetryAt < 8000) return;
  sRetryAt = millis();
  if (!lock(pdMS_TO_TICKS(50))) return;
  mountLocked();
  unlock();
}

bool sdReady() { return sReady; }

void sdFill(Telemetry& t) {
  t.sdOk = 0;
  t.sdUsed = 0;
  t.sdTotal = 0;
  t.sdType[0] = 0;
  if (!lock(pdMS_TO_TICKS(40))) return;
  t.sdOk = sReady ? 1 : 0;
  t.sdUsed = sUsed;
  t.sdTotal = sTotal;
  strncpy(t.sdType, sType, sizeof(t.sdType) - 1);
  unlock();
}

String sdStatusText() {
  if (!sReady) return "SD assente\nCS 10  SCK 12  MISO 13  MOSI 11\nritento ogni 8 s";
  char buf[96];
  snprintf(buf, sizeof(buf), "%s\ntotale %llu\nusati  %llu", sType, (unsigned long long)sTotal,
           (unsigned long long)sUsed);
  return String(buf);
}

static String baseName(const String& path) {
  int s = path.lastIndexOf('/');
  return s >= 0 ? path.substring(s + 1) : path;
}

static String prettyTitle(String name) {
  if (name.endsWith(".html")) name = name.substring(0, name.length() - 5);
  else if (name.endsWith(".htm")) name = name.substring(0, name.length() - 4);
  name.replace('_', ' ');
  name.replace('-', ' ');
  if (name.length() && name[0] >= 'a' && name[0] <= 'z') name.setCharAt(0, name[0] - 32);
  return name;
}

static bool isHtmlName(const String& name) { return name.endsWith(".html") || name.endsWith(".htm"); }

String sdAppsJson() {
  if (!sReady) return "{\"ok\":false,\"err\":\"sd assente\",\"apps\":[]}";
  if (!lock()) return "{\"ok\":false,\"err\":\"busy\",\"apps\":[]}";
  if (!SD.exists("/apps")) SD.mkdir("/apps");
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray apps = doc["apps"].to<JsonArray>();
  File dir = SD.open("/apps");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      String name = baseName(String(f.name()));
      bool folder = f.isDirectory();
      f.close();
      if (!name.length() || name[0] == '.' || name.indexOf('/') >= 0) continue;
      if (folder) {
        String idx = "/apps/" + name + "/index.html";
        if (!SD.exists(idx)) continue;
        JsonObject o = apps.add<JsonObject>();
        o["id"] = name;
        o["title"] = prettyTitle(name);
        o["path"] = idx;
        o["dir"] = true;
      } else if (isHtmlName(name)) {
        String stem = name.endsWith(".html") ? name.substring(0, name.length() - 5) : name.substring(0, name.length() - 4);
        if (SD.exists("/apps/" + stem + "/index.html")) continue;
        JsonObject o = apps.add<JsonObject>();
        o["id"] = stem;
        o["title"] = prettyTitle(name);
        o["path"] = String("/apps/") + name;
        o["dir"] = false;
      }
    }
    dir.close();
  } else if (dir) {
    dir.close();
  }
  unlock();
  String out;
  serializeJson(doc, out);
  return out;
}

String sdListJson(const String& path) {
  if (!sReady) return "{\"ok\":false,\"err\":\"sd assente\"}";
  String safe;
  if (!fsSafePath(path.length() ? path : String("/"), safe)) return "{\"ok\":false,\"err\":\"path\"}";
  if (!lock()) return "{\"ok\":false,\"err\":\"busy\"}";
  File dir = SD.open(safe);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    unlock();
    return "{\"ok\":false,\"err\":\"nodir\"}";
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["path"] = safe;
  JsonArray items = doc["items"].to<JsonArray>();
  File f;
  while ((f = dir.openNextFile())) {
    JsonObject o = items.add<JsonObject>();
    o["name"] = baseName(String(f.name()));
    o["dir"] = f.isDirectory();
    o["size"] = (uint32_t)f.size();
    f.close();
  }
  dir.close();
  unlock();
  String out;
  serializeJson(doc, out);
  return out;
}

bool sdDelete(const String& path, String& err) {
  if (!sReady) {
    err = "sd assente";
    return false;
  }
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (!lock()) {
    err = "busy";
    return false;
  }
  File f = SD.open(safe);
  if (!f) {
    unlock();
    err = "non trovato";
    return false;
  }
  bool dir = f.isDirectory();
  f.close();
  bool ok = dir ? SD.rmdir(safe) : SD.remove(safe);
  sCacheAt = 0;
  unlock();
  if (!ok) err = dir ? "cartella non vuota" : "rimozione fallita";
  return ok;
}

bool sdMkdir(const String& path, String& err) {
  if (!sReady) {
    err = "sd assente";
    return false;
  }
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (!lock()) {
    err = "busy";
    return false;
  }
  bool ok = SD.mkdir(safe);
  unlock();
  if (!ok) err = "mkdir fallito";
  return ok;
}

bool sdReadText(const String& path, String& out, String& err) {
  if (!sReady) {
    err = "sd assente";
    return false;
  }
  String safe;
  if (!fsSafePath(path, safe)) {
    err = "percorso rifiutato";
    return false;
  }
  if (!lock()) {
    err = "busy";
    return false;
  }
  File f = SD.open(safe, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    unlock();
    err = "non trovato";
    return false;
  }
  out = f.readString();
  if (out.length() > 4096) out = out.substring(0, 4096) + "\n… troncato";
  f.close();
  unlock();
  return true;
}

bool sdUploadBegin(const String& dir, const String& filename, String& err) {
  err = "";
  if (sUploadOpen && sUpload) sUpload.close();
  sUploadOpen = false;
  if (!sReady) {
    err = "sd assente";
    return false;
  }
  String name = filename;
  int sl = name.lastIndexOf('/');
  if (sl >= 0) name = name.substring(sl + 1);
  name.replace("..", "");
  String folder = dir.length() ? dir : "/";
  if (!folder.endsWith("/")) folder += "/";
  String safe;
  if (!name.length() || !fsSafePath(folder + name, safe)) {
    err = "percorso rifiutato";
    return false;
  }
  if (!lock()) {
    err = "busy";
    return false;
  }
  sUpload = SD.open(safe, FILE_WRITE);
  sUploadOpen = (bool)sUpload;
  unlock();
  if (!sUploadOpen) err = "apertura fallita";
  return sUploadOpen;
}

bool sdUploadWrite(const uint8_t* data, size_t len) {
  if (!sUploadOpen || !sUpload || !len) return sUploadOpen;
  if (!lock()) return false;
  size_t n = sUpload.write(data, len);
  unlock();
  return n == len;
}

void sdUploadEnd() {
  if (!sUploadOpen) return;
  if (lock()) {
    if (sUpload) sUpload.close();
    sUploadOpen = false;
    sCacheAt = 0;
    unlock();
  }
  helixLog("info", "upload su SD");
}

bool sdWriteText(const String& path, const String& text, String& err) {
  if (!sReady) {
    err = "sd assente";
    return false;
  }
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (text.length() > 16000) {
    err = "troppo lungo";
    return false;
  }
  if (!lock()) {
    err = "busy";
    return false;
  }
  if (SD.exists(safe)) SD.remove(safe);
  File f = SD.open(safe, FILE_WRITE);
  if (!f) {
    unlock();
    err = "apertura fallita";
    return false;
  }
  size_t n = f.print(text);
  f.close();
  sCacheAt = 0;
  unlock();
  if (n != text.length()) {
    err = "scrittura corta";
    return false;
  }
  return true;
}

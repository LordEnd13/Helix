#include "fs_api.h"

#include "config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

static SemaphoreHandle_t sMux = nullptr;

bool fsInit() {
  sMux = xSemaphoreCreateMutex();
  // Etichetta = nome della partizione in partitions.csv.
  // Il subtype resta "spiffs" (0x82): è il valore che il core cerca,
  // anche se il contenuto è formattato LittleFS.
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    helixLog("errore", "LittleFS: mount fallito");
    return false;
  }
  helixLogf("info", "LittleFS %u/%u B", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

bool fsLock(TickType_t ticks) {
  if (!sMux) return true;
  return xSemaphoreTake(sMux, ticks) == pdTRUE;
}

void fsUnlock() {
  if (sMux) xSemaphoreGive(sMux);
}

void fsUsage(uint32_t& used, uint32_t& total) {
  if (!fsLock(pdMS_TO_TICKS(200))) {
    used = 0;
    total = 0;
    return;
  }
  used = (uint32_t)LittleFS.usedBytes();
  total = (uint32_t)LittleFS.totalBytes();
  fsUnlock();
}

bool fsSafePath(const String& in, String& out) {
  if (!in.startsWith("/") || in.indexOf("..") >= 0 || in.indexOf("//") >= 0) return false;
  if (in.length() > 120) return false;
  out = in;
  if (out.length() > 1 && out.endsWith("/")) out.remove(out.length() - 1);
  return true;
}

static String baseName(const String& path) {
  int s = path.lastIndexOf('/');
  return s >= 0 ? path.substring(s + 1) : path;
}

String fsListJson(const String& path) {
  String safe;
  if (!fsSafePath(path.length() ? path : String("/"), safe)) return "{\"ok\":false,\"err\":\"path\"}";
  if (!fsLock()) return "{\"ok\":false,\"err\":\"busy\"}";
  File dir = LittleFS.open(safe);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    fsUnlock();
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
  fsUnlock();
  String out;
  serializeJson(doc, out);
  return out;
}

bool fsDelete(const String& path, String& err) {
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (!fsLock()) {
    err = "busy";
    return false;
  }
  File f = LittleFS.open(safe);
  if (!f) {
    fsUnlock();
    err = "non trovato";
    return false;
  }
  bool dir = f.isDirectory();
  f.close();
  bool ok = dir ? LittleFS.rmdir(safe) : LittleFS.remove(safe);
  fsUnlock();
  if (!ok) err = dir ? "cartella non vuota" : "rimozione fallita";
  return ok;
}

bool fsMkdir(const String& path, String& err) {
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (!fsLock()) {
    err = "busy";
    return false;
  }
  bool ok = LittleFS.mkdir(safe);
  fsUnlock();
  if (!ok) err = "mkdir fallito";
  return ok;
}

bool fsReadText(const String& path, String& out, String& err) {
  String safe;
  if (!fsSafePath(path, safe)) {
    err = "percorso rifiutato";
    return false;
  }
  if (!fsLock()) {
    err = "busy";
    return false;
  }
  File f = LittleFS.open(safe, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    fsUnlock();
    err = "non trovato";
    return false;
  }
  const size_t cap = 4096;
  out = f.readString();
  if (out.length() > cap) out = out.substring(0, cap) + "\n… troncato";
  f.close();
  fsUnlock();
  return true;
}

bool fsWriteText(const String& path, const String& text, String& err) {
  String safe;
  if (!fsSafePath(path, safe) || safe == "/") {
    err = "percorso rifiutato";
    return false;
  }
  if (text.length() > 16000) {
    err = "troppo lungo";
    return false;
  }
  if (!fsLock()) {
    err = "busy";
    return false;
  }
  File f = LittleFS.open(safe, "w");
  if (!f) {
    fsUnlock();
    err = "apertura fallita";
    return false;
  }
  size_t n = f.print(text);
  f.close();
  fsUnlock();
  if (n != text.length()) {
    err = "scrittura corta";
    return false;
  }
  return true;
}

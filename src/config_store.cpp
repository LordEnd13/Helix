#include "config_store.h"

#include "config.h"
#include "fs_api.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <mbedtls/sha256.h>

static char sTheme[8] = "dark";
static char sAccent[8] = "brass";
static char sWall[12] = "pcb";
static char sFont[4] = "md";
static char sUser[24] = "helix";
static char sPass[65] = {0};

static bool oneOf(const char* v, const char* a, const char* b = nullptr, const char* c = nullptr,
                  const char* d = nullptr, const char* e = nullptr, const char* f = nullptr) {
  const char* list[] = {a, b, c, d, e, f};
  for (const char* item : list) {
    if (item && !strcmp(v, item)) return true;
  }
  return false;
}

static void sha256hex(const char* in, char out[65]) {
  uint8_t raw[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(in), strlen(in), raw, 0);
  for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", raw[i]);
  out[64] = 0;
}

static bool sameHex(const char* a, const char* b) {
  uint8_t d = 0;
  for (int i = 0; i < 64; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0 && a[64] == 0 && b[64] == 0;
}

static void copyField(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) {
    char c = src[i];
    if (c < 32 || c == '"' || c == '\\') c = ' ';
    dst[i] = c;
  }
  dst[i] = 0;
  while (i && dst[i - 1] == ' ') dst[--i] = 0;
}

static void saveLocked() {
  JsonDocument doc;
  doc["theme"] = sTheme;
  doc["accent"] = sAccent;
  doc["wallpaper"] = sWall;
  doc["font"] = sFont;
  doc["user"] = sUser;
  doc["pass"] = sPass;
  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    helixLog("errore", "config.json non scrivibile");
    return;
  }
  serializeJson(doc, f);
  f.close();
}

void configLoad() {
  if (!fsLock(pdMS_TO_TICKS(1000))) return;
  if (!LittleFS.exists("/config.json")) {
    saveLocked();
    fsUnlock();
    helixLog("info", "config.json creato");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    fsUnlock();
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  fsUnlock();
  if (err) {
    helixLog("warn", "config.json illeggibile, tengo i default");
    return;
  }
  const char* theme = doc["theme"] | "dark";
  const char* accent = doc["accent"] | "brass";
  const char* wall = doc["wallpaper"] | "pcb";
  const char* font = doc["font"] | "md";
  if (oneOf(theme, "dark", "light")) copyField(sTheme, sizeof(sTheme), theme);
  if (oneOf(accent, "brass", "blue", "green", "violet", "orange")) copyField(sAccent, sizeof(sAccent), accent);
  if (oneOf(wall, "pcb", "grid", "dusk", "carbon", "paper", "custom")) copyField(sWall, sizeof(sWall), wall);
  if (oneOf(font, "sm", "md", "lg")) copyField(sFont, sizeof(sFont), font);
  copyField(sUser, sizeof(sUser), doc["user"] | "helix");
  if (!sUser[0]) copyField(sUser, sizeof(sUser), "helix");
  copyField(sPass, sizeof(sPass), doc["pass"] | "");
  if (strlen(sPass) != 64) sPass[0] = 0;
}

String configPublicJson() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["theme"] = sTheme;
  doc["accent"] = sAccent;
  doc["wallpaper"] = sWall;
  doc["font"] = sFont;
  doc["user"] = sUser;
  doc["hasPass"] = sPass[0] != 0;
  bool avatar = false;
  if (fsLock(pdMS_TO_TICKS(200))) {
    avatar = LittleFS.exists("/avatar.jpg");
    if (sWall[0] == 'c' && !LittleFS.exists("/wallpaper.jpg")) {
      copyField(sWall, sizeof(sWall), "pcb");
    }
    fsUnlock();
  }
  doc["wallpaper"] = sWall;
  doc["avatar"] = avatar;
  String out;
  serializeJson(doc, out);
  return out;
}

bool configApplyJson(const String& body, String& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    err = "json";
    return false;
  }
  const char* theme = doc["theme"] | sTheme;
  const char* accent = doc["accent"] | sAccent;
  const char* wall = doc["wallpaper"] | sWall;
  if (!oneOf(theme, "dark", "light") || !oneOf(accent, "brass", "blue", "green", "violet", "orange") ||
      !oneOf(wall, "pcb", "grid", "dusk", "carbon", "paper", "custom")) {
    err = "valore";
    return false;
  }
  copyField(sTheme, sizeof(sTheme), theme);
  copyField(sAccent, sizeof(sAccent), accent);
  copyField(sWall, sizeof(sWall), wall);
  if (doc["font"].is<const char*>()) {
    const char* font = doc["font"];
    if (!oneOf(font, "sm", "md", "lg")) {
      err = "valore";
      return false;
    }
    copyField(sFont, sizeof(sFont), font);
  }
  if (doc["user"].is<const char*>()) {
    copyField(sUser, sizeof(sUser), doc["user"]);
    if (!sUser[0]) copyField(sUser, sizeof(sUser), "helix");
  }
  if (doc["clearPass"].as<bool>()) sPass[0] = 0;
  const char* pass = doc["pass"] | "";
  if (pass[0]) {
    if (strlen(pass) < 4 || strlen(pass) > 40) {
      err = "password";
      return false;
    }
    sha256hex(pass, sPass);
  }
  if (!fsLock()) {
    err = "busy";
    return false;
  }
  saveLocked();
  fsUnlock();
  helixLogf("info", "profilo %s  tema %s/%s", sUser, sTheme, sAccent);
  return true;
}

bool configLogin(const String& pass) {
  if (!sPass[0]) return true;
  char hex[65];
  sha256hex(pass.c_str(), hex);
  return sameHex(sPass, hex);
}

const char* configUser() { return sUser; }

void configUseCustomWall() {
  copyField(sWall, sizeof(sWall), "custom");
  if (!fsLock(pdMS_TO_TICKS(500))) return;
  saveLocked();
  fsUnlock();
}

void configUseAvatar() { helixLog("info", "avatar aggiornato"); }

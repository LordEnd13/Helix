#include "web_server.h"

#include "cli.h"
#include "config.h"
#include "config_store.h"
#include "fs_api.h"
#include "lab.h"
#include "sd_card.h"
#include "telemetry.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static SemaphoreHandle_t sWsMux = nullptr;
static bool sReady = false;

static char sOsBody[768];
static size_t sOsLen = 0;
static char* sWrite = nullptr;
static size_t sWriteCap = 0;
static size_t sWriteLen = 0;
static bool sWriteOk = false;
static File sImg;
static bool sImgOk = false;
static size_t sImgLen = 0;

static void onOsBody(AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) {
  if (index == 0) sOsLen = 0;
  size_t n = len;
  if (sOsLen + n >= sizeof(sOsBody)) n = sizeof(sOsBody) - 1 - sOsLen;
  if (n) memcpy(sOsBody + sOsLen, data, n);
  sOsLen += n;
}

static void onWriteBody(AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) {
  if (index == 0) {
    if (!sWrite) {
      sWrite = static_cast<char*>(heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      sWriteCap = sWrite ? 16384 : 0;
      if (!sWrite) {
        sWrite = static_cast<char*>(malloc(8192));
        sWriteCap = sWrite ? 8192 : 0;
      }
    }
    sWriteLen = 0;
    sWriteOk = sWrite != nullptr;
  }
  if (!sWriteOk || !sWrite || sWriteLen + len >= sWriteCap) {
    sWriteOk = false;
    return;
  }
  memcpy(sWrite + sWriteLen, data, len);
  sWriteLen += len;
}

static void imgBegin(const char* path) {
  if (sImg) sImg.close();
  sImgLen = 0;
  sImgOk = true;
  if (!fsLock(pdMS_TO_TICKS(800))) {
    sImgOk = false;
    return;
  }
  LittleFS.remove(path);
  sImg = LittleFS.open(path, "w");
  fsUnlock();
  if (!sImg) sImgOk = false;
}

static void imgChunk(const char* path, uint8_t* data, size_t len, size_t index, size_t total, bool wall) {
  if (index == 0) imgBegin(path);
  if (!sImgOk || total > 200 * 1024 || sImgLen + len > 200 * 1024) {
    sImgOk = false;
    if (sImg) sImg.close();
    return;
  }
  if (sImg && len) sImg.write(data, len);
  sImgLen += len;
  if (index + len >= total && sImg) {
    sImg.close();
    if (sImgOk) {
      if (wall) configUseCustomWall();
      else configUseAvatar();
    }
  }
}
static char sMkBody[160];
static size_t sMkLen = 0;
static char sSdMk[160];
static size_t sSdMkLen = 0;
static File sUpload;
static bool sUploadOk = false;

static bool capture(AsyncWebServerRequest* r) {
  if (!wifiApUp()) return false;
  if (r->url().startsWith("/api/")) return false;
  String host = r->host();
  int colon = host.indexOf(':');
  if (colon > 0) host = host.substring(0, colon);
  if (host == "192.168.4.1") return false;
  if (wifiHostIsSta(host.c_str())) return false;
  return true;
}

static void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  r->send(code, "application/json", body);
}

static void onWs(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    if (ws.count() > HELIX_WS_MAX_CLIENTS) {
      client->close();
      return;
    }
    Serial.printf("[info] ws #%u\n", (unsigned)client->id());
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[info] ws #%u chiuso\n", (unsigned)client->id());
    return;
  }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
  if (len == 0 || len >= 1400) return;
  char* msg = (char*)malloc(len + 1);
  if (!msg) return;
  memcpy(msg, data, len);
  msg[len] = 0;
  JsonDocument doc;
  if (deserializeJson(doc, msg)) {
    cliSubmit(msg);
    free(msg);
    return;
  }
  const char* t = doc["t"] | "";
  if (labIsType(t)) {
    labSubmit(msg);
    free(msg);
    return;
  }
  free(msg);
  if (!strcmp(t, "gpio")) {
    int pin = doc["pin"] | -1;
    const char* op = doc["op"] | "read";
    char line[80];
    if (!strcmp(op, "mode"))
      snprintf(line, sizeof(line), "gpio %d mode %s", pin, (const char*)(doc["mode"] | "free"));
    else if (!strcmp(op, "write"))
      snprintf(line, sizeof(line), "gpio %d write %d", pin, (int)(doc["value"] | 0));
    else if (!strcmp(op, "pwm"))
      snprintf(line, sizeof(line), "gpio %d pwm %d %d", pin, (int)(doc["duty"] | 0), (int)(doc["freq"] | 5000));
    else
      snprintf(line, sizeof(line), "gpio %d read", pin);
    cliSubmit(line);
    return;
  }
  cliSubmit(doc["line"] | "");
}

static void handleDelete(AsyncWebServerRequest* r) {
  if (!r->hasParam("path")) {
    sendJson(r, 400, "{\"ok\":false}");
    return;
  }
  String err;
  bool ok = fsDelete(r->getParam("path")->value(), err);
  sendJson(r, ok ? 200 : 400, ok ? "{\"ok\":true}" : String("{\"ok\":false,\"err\":\"") + err + "\"}");
}

static void onUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (!fsLock(pdMS_TO_TICKS(3000))) return;
  if (index == 0) {
    String dir = "/";
    if (request->hasParam("dir")) dir = request->getParam("dir")->value();
    String name = filename;
    int sl = name.lastIndexOf('/');
    if (sl >= 0) name = name.substring(sl + 1);
    name.replace("..", "");
    if (!dir.endsWith("/")) dir += "/";
    String safe;
    sUploadOk = name.length() && fsSafePath(dir + name, safe);
    if (sUpload) sUpload.close();
    if (sUploadOk) {
      sUpload = LittleFS.open(safe, "w");
      sUploadOk = (bool)sUpload;
    }
  }
  if (sUploadOk && sUpload && len) sUpload.write(data, len);
  if (final && sUpload) {
    sUpload.close();
    helixLogf("info", "upload %s (%u B)", filename.c_str(), (unsigned)(index + len));
  }
  fsUnlock();
}

static void handleSdDelete(AsyncWebServerRequest* r) {
  if (!r->hasParam("path")) {
    sendJson(r, 400, "{\"ok\":false}");
    return;
  }
  String err;
  bool ok = sdDelete(r->getParam("path")->value(), err);
  sendJson(r, ok ? 200 : 400, ok ? "{\"ok\":true}" : String("{\"ok\":false,\"err\":\"") + err + "\"}");
}

static void onSdUpload(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    String dir = "/";
    if (request->hasParam("dir")) dir = request->getParam("dir")->value();
    String err;
    sdUploadBegin(dir, filename, err);
  }
  if (len) sdUploadWrite(data, len);
  if (final) sdUploadEnd();
}

static const char* sdMime(const String& path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "text/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".webp")) return "image/webp";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".wasm")) return "application/wasm";
  if (path.endsWith(".pdf")) return "application/pdf";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (path.endsWith(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

static void handleSdFile(AsyncWebServerRequest* r) {
  if (!sdReady()) {
    r->send(503, "text/plain", "sd assente");
    return;
  }
  String url = r->url();
  if (!url.startsWith("/sd/")) {
    r->send(404, "text/plain", "non trovato");
    return;
  }
  String rel = url.substring(3);
  String safe;
  if (!fsSafePath(rel, safe)) {
    r->send(400, "text/plain", "percorso rifiutato");
    return;
  }
  if (!SD.exists(safe)) {
    r->send(404, "text/plain", "non trovato");
    return;
  }
  File f = SD.open(safe);
  bool folder = f && f.isDirectory();
  if (f) f.close();
  if (folder) {
    String idx = safe + "/index.html";
    if (SD.exists(idx)) {
      r->redirect(String("/sd") + idx);
      return;
    }
    r->send(404, "text/plain", "cartella senza index.html");
    return;
  }
  AsyncWebServerResponse* res = r->beginResponse(SD, safe, sdMime(safe));
  if (res) {
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  } else {
    r->send(500, "text/plain", "lettura fallita");
  }
}

void webInit() {
  sWsMux = xSemaphoreCreateMutex();
  ws.onEvent(onWs);
  server.addHandler(&ws);

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    Telemetry t;
    telemetryCopy(t);
    char buf[HELIX_MSG];
    snprintf(buf, sizeof(buf),
             "{\"cpu0\":%.1f,\"cpu1\":%.1f,\"heap\":%u,\"heapTotal\":%u,\"psram\":%u,\"psramTotal\":%u,"
             "\"fsUsed\":%u,\"fsTotal\":%u,\"temp\":%.1f,\"rssi\":%d,\"uptime\":%u,\"ssid\":\"%s\","
             "\"ip\":\"%s\",\"mode\":\"%s\",\"g\":\"%s\",\"sd\":%u,\"sdUsed\":%llu,\"sdTotal\":%llu,"
             "\"sdType\":\"%s\",\"oled\":%u}",
             t.cpu0, t.cpu1, (unsigned)t.heapFree, (unsigned)t.heapTotal, (unsigned)t.psramFree,
             (unsigned)t.psramTotal, (unsigned)t.fsUsed, (unsigned)t.fsTotal, t.tempC, (int)t.rssi,
             (unsigned)t.uptimeS, t.ssid, t.ip, t.mode, t.gpio, (unsigned)t.sdOk,
             (unsigned long long)t.sdUsed, (unsigned long long)t.sdTotal, t.sdType, (unsigned)t.oledOk);
    r->send(200, "application/json", buf);
  });

  server.on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest* r) {
    String path = "/";
    if (r->hasParam("path")) path = r->getParam("path")->value();
    r->send(200, "application/json", fsListJson(path));
  });

  server.on("/api/fs/download", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("path")) {
      sendJson(r, 400, "{\"ok\":false}");
      return;
    }
    String safe;
    if (!fsSafePath(r->getParam("path")->value(), safe) || !LittleFS.exists(safe)) {
      sendJson(r, 404, "{\"ok\":false}");
      return;
    }
    r->send(LittleFS, safe, String(), true);
  });

  server.on("/api/fs/delete", HTTP_POST, handleDelete);
  server.on("/api/fs/delete", HTTP_DELETE, handleDelete);

  server.on(
      "/api/fs/mkdir", HTTP_POST, [](AsyncWebServerRequest* r) {
        sMkBody[sMkLen < sizeof(sMkBody) ? sMkLen : sizeof(sMkBody) - 1] = 0;
        JsonDocument doc;
        if (deserializeJson(doc, sMkBody)) {
          sendJson(r, 400, "{\"ok\":false}");
          sMkLen = 0;
          return;
        }
        String err;
        bool ok = fsMkdir(doc["path"] | "/", err);
        sendJson(r, ok ? 200 : 400, ok ? "{\"ok\":true}" : "{\"ok\":false}");
        sMkLen = 0;
      },
      nullptr, [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) {
        if (index == 0) sMkLen = 0;
        size_t n = len;
        if (sMkLen + n >= sizeof(sMkBody)) n = sizeof(sMkBody) - 1 - sMkLen;
        memcpy(sMkBody + sMkLen, data, n);
        sMkLen += n;
      });

  server.on(
      "/api/fs/upload", HTTP_POST,
      [](AsyncWebServerRequest* r) { sendJson(r, sUploadOk ? 200 : 400, sUploadOk ? "{\"ok\":true}" : "{\"ok\":false}"); },
      onUpload);

  server.on("/api/sd/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/plain", sdStatusText());
  });

  server.on("/api/sd/apps", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "application/json", sdAppsJson()); });

  server.on("/api/sd/list", HTTP_GET, [](AsyncWebServerRequest* r) {
    String path = "/";
    if (r->hasParam("path")) path = r->getParam("path")->value();
    r->send(200, "application/json", sdListJson(path));
  });

  server.on("/api/sd/download", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("path") || !sdReady()) {
      sendJson(r, 400, "{\"ok\":false}");
      return;
    }
    String safe;
    if (!fsSafePath(r->getParam("path")->value(), safe) || !SD.exists(safe)) {
      sendJson(r, 404, "{\"ok\":false}");
      return;
    }
    r->send(SD, safe, String(), true);
  });

  server.on("/api/sd/delete", HTTP_POST, handleSdDelete);
  server.on("/api/sd/delete", HTTP_DELETE, handleSdDelete);

  server.on(
      "/api/sd/mkdir", HTTP_POST, [](AsyncWebServerRequest* r) {
        sSdMk[sSdMkLen < sizeof(sSdMk) ? sSdMkLen : sizeof(sSdMk) - 1] = 0;
        JsonDocument doc;
        if (deserializeJson(doc, sSdMk)) {
          sendJson(r, 400, "{\"ok\":false}");
          sSdMkLen = 0;
          return;
        }
        String err;
        bool ok = sdMkdir(doc["path"] | "/", err);
        sendJson(r, ok ? 200 : 400, ok ? "{\"ok\":true}" : "{\"ok\":false}");
        sSdMkLen = 0;
      },
      nullptr, [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) {
        if (index == 0) sSdMkLen = 0;
        size_t n = len;
        if (sSdMkLen + n >= sizeof(sSdMk)) n = sizeof(sSdMk) - 1 - sSdMkLen;
        memcpy(sSdMk + sSdMkLen, data, n);
        sSdMkLen += n;
      });

  server.on(
      "/api/sd/upload", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        sendJson(r, sdReady() ? 200 : 400, sdReady() ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"sd assente\"}");
      },
      onSdUpload);

  server.on("/api/cli", HTTP_GET, [](AsyncWebServerRequest* r) {
    uint16_t since = 0;
    if (r->hasParam("since")) since = (uint16_t)r->getParam("since")->value().toInt();
    r->send(200, "application/json", cliSinceJson(since));
  });
  server.on("/api/cli", HTTP_POST, [](AsyncWebServerRequest* r) {
    String line;
    if (r->hasParam("line", true)) line = r->getParam("line", true)->value();
    line.trim();
    if (!line.length() || line.length() > 180) {
      sendJson(r, 400, "{\"ok\":false}");
      return;
    }
    cliSubmit(line.c_str());
    sendJson(r, 202, "{\"ok\":true}");
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "application/json", wifiScanJson()); });

  server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* r) {
    String ssid;
    String pass;
    if (r->hasParam("ssid", true)) ssid = r->getParam("ssid", true)->value();
    if (r->hasParam("pass", true)) pass = r->getParam("pass", true)->value();
    ssid.trim();
    if (!ssid.length() || ssid.length() > 32) {
      sendJson(r, 400, "{\"ok\":false,\"err\":\"ssid\"}");
      return;
    }
    if (pass.length() && pass.length() < 8) {
      sendJson(r, 400, "{\"ok\":false,\"err\":\"password\"}");
      return;
    }
    wifiRequestConnect(ssid.c_str(), pass.c_str());
    helixLogf("info", "credenziali %s", ssid.c_str());
    sendJson(r, 202, "{\"ok\":true}");
  });

  server.on("/api/gpio", HTTP_POST, [](AsyncWebServerRequest* r) {
    int pin = r->hasParam("pin", true) ? r->getParam("pin", true)->value().toInt() : -1;
    String op = r->hasParam("op", true) ? r->getParam("op", true)->value() : "read";
    char line[80];
    if (op == "mode")
      snprintf(line, sizeof(line), "gpio %d mode %s", pin, r->hasParam("mode", true) ? r->getParam("mode", true)->value().c_str() : "free");
    else if (op == "write")
      snprintf(line, sizeof(line), "gpio %d write %d", pin, r->hasParam("value", true) ? r->getParam("value", true)->value().toInt() : 0);
    else if (op == "pwm")
      snprintf(line, sizeof(line), "gpio %d pwm %d %d", pin,
               r->hasParam("duty", true) ? r->getParam("duty", true)->value().toInt() : 0,
               r->hasParam("freq", true) ? r->getParam("freq", true)->value().toInt() : 5000);
    else
      snprintf(line, sizeof(line), "gpio %d read", pin);
    cliSubmit(line);
    sendJson(r, 202, "{\"ok\":true}");
  });

  server.on("/api/wifi/ap", HTTP_POST, [](AsyncWebServerRequest* r) {
    bool on = true;
    if (r->hasParam("on")) on = r->getParam("on")->value() != "0";
    wifiSetApFallback(on);
    sendJson(r, 200, "{\"ok\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    sendJson(r, 200, "{\"ok\":true}");
    cliSubmit("reboot");
  });

  server.on("/api/os", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "application/json", configPublicJson()); });
  server.on(
      "/api/os", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        sOsBody[sOsLen < sizeof(sOsBody) ? sOsLen : sizeof(sOsBody) - 1] = 0;
        String err;
        if (!configApplyJson(String(sOsBody), err)) {
          sendJson(r, 400, String("{\"ok\":false,\"err\":\"") + err + "\"}");
          return;
        }
        r->send(200, "application/json", configPublicJson());
      },
      nullptr, onOsBody);
  server.on(
      "/api/login", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        sOsBody[sOsLen < sizeof(sOsBody) ? sOsLen : sizeof(sOsBody) - 1] = 0;
        JsonDocument doc;
        const char* pass = "";
        if (!deserializeJson(doc, sOsBody)) pass = doc["pass"] | "";
        sendJson(r, configLogin(pass) ? 200 : 401, configLogin(pass) ? "{\"ok\":true}" : "{\"ok\":false}");
      },
      nullptr, onOsBody);
  server.on(
      "/api/fs/write", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        if (!sWriteOk || !sWrite) {
          sendJson(r, 413, "{\"ok\":false,\"err\":\"troppo grande\"}");
          return;
        }
        sWrite[sWriteLen] = 0;
        JsonDocument doc;
        if (deserializeJson(doc, sWrite)) {
          sendJson(r, 400, "{\"ok\":false,\"err\":\"json\"}");
          return;
        }
        String path = doc["path"] | "/notes.txt";
        String text = doc["text"] | "";
        const char* vol = doc["vol"] | "lfs";
        String err;
        bool ok = !strcmp(vol, "sd") ? sdWriteText(path, text, err) : fsWriteText(path, text, err);
        if (!ok) sendJson(r, 400, String("{\"ok\":false,\"err\":\"") + err + "\"}");
        else sendJson(r, 200, "{\"ok\":true}");
      },
      nullptr, onWriteBody);
  server.on(
      "/api/os/wallpaper", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        if (!sImgOk) sendJson(r, 400, "{\"ok\":false}");
        else sendJson(r, 200, "{\"ok\":true,\"wallpaper\":\"custom\"}");
      },
      nullptr, [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t total) {
        imgChunk("/wallpaper.jpg", data, len, index, total, true);
      });
  server.on(
      "/api/os/avatar", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        if (!sImgOk) sendJson(r, 400, "{\"ok\":false}");
        else sendJson(r, 200, "{\"ok\":true}");
      },
      nullptr, [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t total) {
        imgChunk("/avatar.jpg", data, len, index, total, false);
      });

  auto portal = [](AsyncWebServerRequest* r) { r->redirect("http://192.168.4.1/"); };
  server.on("/generate_204", HTTP_GET, portal);
  server.on("/gen_204", HTTP_GET, portal);
  server.on("/hotspot-detect.html", HTTP_GET, portal);
  server.on("/connecttest.txt", HTTP_GET, portal);
  server.on("/ncsi.txt", HTTP_GET, portal);
  server.on("/fwlink", HTTP_GET, portal);

  auto sendIndex = [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(LittleFS, "/index.html", "text/html");
    if (!res) {
      r->send(404, "text/plain", "index assente");
      return;
    }
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  };
  server.on("/", HTTP_GET, sendIndex);
  server.on("/index.html", HTTP_GET, sendIndex);
  server.serveStatic("/", LittleFS, "/").setCacheControl("public, max-age=604800");

  server.on("/sd/*", HTTP_GET, handleSdFile);

  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->url().startsWith("/sd/")) {
      handleSdFile(r);
      return;
    }
    if (capture(r)) {
      r->redirect("http://192.168.4.1/");
      return;
    }
    r->send(404, "text/plain", "non trovato");
  });

  server.begin();
  sReady = true;
  helixLog("info", "http :80  ws /ws");
}

void webPoll() {
  if (!sReady || !gTx) return;
  if (ws.count() == 0) {
    NetMsg drop;
    while (xQueueReceive(gTx, &drop, 0) == pdTRUE) {
    }
    return;
  }
  NetMsg m;
  int budget = 6;
  while (budget-- && xQueueReceive(gTx, &m, 0) == pdTRUE) {
    if (sWsMux && xSemaphoreTake(sWsMux, pdMS_TO_TICKS(30)) == pdTRUE) {
      ws.textAll(m.text);
      xSemaphoreGive(sWsMux);
    }
  }
  ws.cleanupClients();
}

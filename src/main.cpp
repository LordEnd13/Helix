#include "config.h"

#include "cli.h"
#include "config_store.h"
#include "fs_api.h"
#include "gpio_ctrl.h"
#include "lab.h"
#include "oled_ui.h"
#include "sd_card.h"
#include "telemetry.h"
#include "web_server.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdarg.h>

// Code e snapshot condivisi. Definiti qui, dichiarati in config.h.
QueueHandle_t gTx = nullptr;
QueueHandle_t gCli = nullptr;
SemaphoreHandle_t gTelMux = nullptr;
Telemetry gTel{};

namespace {

struct Slot {
  char lvl[8];
  char msg[140];
};

Slot* ring = nullptr;
uint16_t cap = 0;
uint16_t pos = 0;
uint16_t count = 0;
SemaphoreHandle_t logMux = nullptr;
// Scratch in PSRAM: serializzazione JSON dei log, lontano dall'heap interna.
uint8_t* scratch = nullptr;

}  // namespace

bool helixEnqueueTx(const char* json) {
  if (!gTx || !json) return false;
  NetMsg m{};
  strncpy(m.text, json, sizeof(m.text) - 1);
  m.text[sizeof(m.text) - 1] = 0;
  return xQueueSend(gTx, &m, 0) == pdTRUE;
}

void helixLogDump(String& out) {
  out = "";
  if (logMux) xSemaphoreTake(logMux, portMAX_DELAY);
  if (ring && count) {
    uint16_t start = (pos + cap - count) % cap;
    for (uint16_t i = 0; i < count; i++) {
      Slot& sl = ring[(start + i) % cap];
      if (out.length()) out += '\n';
      out += sl.lvl;
      out += "  ";
      out += sl.msg;
    }
  }
  if (logMux) xSemaphoreGive(logMux);
}

void helixLog(const char* lvl, const char* msg) {
  if (!lvl) lvl = "info";
  if (!msg) msg = "";
  Serial.printf("[%s] %s\n", lvl, msg);

  if (logMux) xSemaphoreTake(logMux, portMAX_DELAY);
  if (ring && cap) {
    Slot& sl = ring[pos];
    strncpy(sl.lvl, lvl, sizeof(sl.lvl) - 1);
    sl.lvl[sizeof(sl.lvl) - 1] = 0;
    strncpy(sl.msg, msg, sizeof(sl.msg) - 1);
    sl.msg[sizeof(sl.msg) - 1] = 0;
    pos = (pos + 1) % cap;
    if (count < cap) count++;
  }

  char stack[HELIX_MSG];
  char* buf = stack;
  size_t nbuf = sizeof(stack);
  if (scratch) {
    buf = (char*)scratch;
    nbuf = 1024;
  }
  JsonDocument doc;
  doc["t"] = "log";
  doc["lvl"] = lvl;
  doc["msg"] = msg;
  size_t n = serializeJson(doc, buf, nbuf);
  char copy[HELIX_MSG];
  if (n && n < sizeof(copy)) {
    memcpy(copy, buf, n + 1);
  } else {
    n = 0;
  }
  if (logMux) xSemaphoreGive(logMux);
  if (n) helixEnqueueTx(copy);
  oledFeed(msg);
}

void helixLogf(const char* lvl, const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  helixLog(lvl, buf);
}

static void netTask(void*) {
  for (;;) {
    wifiPoll();
    webPoll();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void sysTask(void*) {
  for (;;) {
    cliPoll();
    labPoll();
    sdPoll();
    telemetryPoll();
    oledPoll();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);

  logMux = xSemaphoreCreateMutex();
  gTelMux = xSemaphoreCreateMutex();
  // 16 messaggi da 896 B: la coda sta in RAM interna.
  gTx = xQueueCreate(16, sizeof(NetMsg));
  gCli = xQueueCreate(8, sizeof(NetMsg));

  setCpuFrequencyMhz(240);
  helixLog("info", "helix " HELIX_VERSION "  ESP32-S3-N16R8");

  if (psramFound()) {
    scratch = (uint8_t*)heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ring = (Slot*)heap_caps_malloc(sizeof(Slot) * 48, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ring) cap = 48;
    if (scratch) scratch[0] = 0;
    helixLogf("info", "PSRAM %u KB  scratch %s  anello log %s", (unsigned)(ESP.getPsramSize() / 1024),
              scratch ? "32KB" : "fallito", ring ? "48" : "fallito");
  } else {
    helixLog("errore", "PSRAM assente: memory_type qio_opi su un modulo N16R8");
  }
  if (!ring) {
    static Slot fallback[16];
    ring = fallback;
    cap = 16;
  }

  fsInit();
  configLoad();
  gpioInit();
  oledInit();
  sdInit();
  telemetryInit();
  wifiInit();
  labInit();
  webInit();

  // Core 0: radio e server. Core 1: misura, CLI, periferiche.
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(sysTask, "sys", 20480, nullptr, 1, nullptr, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }

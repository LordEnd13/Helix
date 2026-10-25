#include "telemetry.h"

#include "config.h"
#include "fs_api.h"
#include "gpio_ctrl.h"
#include "oled_ui.h"
#include "sd_card.h"
#include "wifi_manager.h"

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include "driver/temperature_sensor.h"
#else
#include "driver/temp_sensor.h"
#endif
#include "esp_freertos_hooks.h"
#include "esp_heap_caps.h"

#include <ArduinoJson.h>
#include <cmath>

// Idle hook: gira nel task idle di ciascun core. Solo un incremento.
// La percentuale è 100 - (conteggio / massimo osservato). Il massimo
// osservato approssima il core scarico, quindi la stima si assesta
// dopo i primi secondi di lavoro leggero.

static volatile uint32_t sIdle[2] = {0, 0};
static uint32_t sLastIdle[2] = {0, 0};
static uint32_t sBase[2] = {0, 0};
static uint32_t sLastMs = 0;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static temperature_sensor_handle_t sTemp = nullptr;
#else
static bool sTemp = false;
#endif

static bool idle0() {
  sIdle[0]++;
  return false;
}
static bool idle1() {
  sIdle[1]++;
  return false;
}

static float usage(uint32_t delta, uint32_t& base) {
  if (delta > base) base = delta;
  if (base == 0) return 0;
  float pct = 100.f - (100.f * (float)delta / (float)base);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

void telemetryInit() {
  esp_register_freertos_idle_hook_for_cpu(idle0, 0);
  esp_register_freertos_idle_hook_for_cpu(idle1, 1);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  if (temperature_sensor_install(&cfg, &sTemp) == ESP_OK) {
    temperature_sensor_enable(sTemp);
    helixLog("info", "sensore temperatura ok");
  } else {
    sTemp = nullptr;
    helixLog("warn", "sensore temperatura non installato");
  }
#else
  temp_sensor_config_t cfg = TSENS_CONFIG_DEFAULT();
  if (temp_sensor_set_config(cfg) == ESP_OK && temp_sensor_start() == ESP_OK) {
    sTemp = true;
    helixLog("info", "sensore temperatura ok");
  } else {
    helixLog("warn", "sensore temperatura non installato");
  }
#endif
  sLastMs = millis();
}

void telemetryCopy(Telemetry& out) {
  if (gTelMux) xSemaphoreTake(gTelMux, portMAX_DELAY);
  out = gTel;
  if (gTelMux) xSemaphoreGive(gTelMux);
}

void telemetryPoll() {
  const uint32_t now = millis();
  if (now - sLastMs < HELIX_TEL_MS) return;

  const uint32_t d0 = sIdle[0] - sLastIdle[0];
  const uint32_t d1 = sIdle[1] - sLastIdle[1];
  sLastIdle[0] = sIdle[0];
  sLastIdle[1] = sIdle[1];
  sLastMs = now;

  float temp = NAN;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (sTemp) temperature_sensor_get_celsius(sTemp, &temp);
#else
  if (sTemp) temp_sensor_read_celsius(&temp);
#endif

  uint32_t used = 0, total = 0;
  fsUsage(used, total);

  Telemetry t{};
  t.cpu0 = usage(d0, sBase[0]);
  t.cpu1 = usage(d1, sBase[1]);
  t.tempC = temp;
  t.heapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  t.heapTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  t.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  t.psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  t.fsUsed = used;
  t.fsTotal = total;
  t.uptimeS = now / 1000;
  wifiFill(t);
  gpioCompact(t.gpio, sizeof(t.gpio));
  sdFill(t);
  t.oledOk = oledReady() ? 1 : 0;

  if (gTelMux) xSemaphoreTake(gTelMux, portMAX_DELAY);
  gTel = t;
  if (gTelMux) xSemaphoreGive(gTelMux);

  JsonDocument doc;
  doc["t"] = "tel";
  doc["cpu0"] = t.cpu0;
  doc["cpu1"] = t.cpu1;
  doc["heap"] = t.heapFree;
  doc["heapTotal"] = t.heapTotal;
  doc["psram"] = t.psramFree;
  doc["psramTotal"] = t.psramTotal;
  doc["fsUsed"] = t.fsUsed;
  doc["fsTotal"] = t.fsTotal;
  doc["temp"] = std::isnan(t.tempC) ? 0.0 : t.tempC;
  doc["rssi"] = t.rssi;
  doc["uptime"] = t.uptimeS;
  doc["ssid"] = t.ssid;
  doc["ip"] = t.ip;
  doc["mode"] = t.mode;
  doc["g"] = t.gpio;
  doc["sd"] = t.sdOk;
  doc["sdUsed"] = t.sdUsed;
  doc["sdTotal"] = t.sdTotal;
  doc["sdType"] = t.sdType;
  doc["oled"] = t.oledOk;
  char buf[HELIX_MSG];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (!n || n >= sizeof(buf)) return;
  helixEnqueueTx(buf);
}

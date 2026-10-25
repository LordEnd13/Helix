#include "oled_ui.h"

#include "config.h"
#include "telemetry.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

// Buffer da 1024 byte nel driver. Il refresh non sta nei callback di rete:
// oledFeed accoda una riga, oledPoll (task sys, core 1) parla con il bus I2C.

static Adafruit_SSD1306 sDisp(128, 64, &Wire, -1);
static bool sOk = false;
static SemaphoreHandle_t sMux = nullptr;
static char sLog[3][22];
static uint8_t sLogN = 0;

static void clip(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  size_t i = 0;
  for (; src[i] && i + 1 < n; i++) {
    char c = src[i];
    dst[i] = (c == '\n' || c == '\r') ? ' ' : c;
  }
  dst[i] = 0;
}

bool oledReady() { return sOk; }

void oledFeed(const char* msg) {
  if (!sMux) sMux = xSemaphoreCreateMutex();
  if (sMux) xSemaphoreTake(sMux, portMAX_DELAY);
  if (sLogN < 3) {
    clip(sLog[sLogN], sizeof(sLog[0]), msg);
    sLogN++;
  } else {
    memmove(sLog[0], sLog[1], sizeof(sLog[0]) * 2);
    clip(sLog[2], sizeof(sLog[0]), msg);
  }
  if (sMux) xSemaphoreGive(sMux);
}

static void row(const char* text) { sDisp.println(text); }

void oledInit() {
  if (!sMux) sMux = xSemaphoreCreateMutex();
  Wire.begin(HELIX_OLED_SDA, HELIX_OLED_SCL);
  Wire.setClock(400000);
  sOk = sDisp.begin(SSD1306_SWITCHCAPVCC, HELIX_OLED_ADDR) || sDisp.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (!sOk) {
    helixLog("warn", "OLED SSD1306 assente (SDA 8, SCL 9)");
    return;
  }
  sDisp.clearDisplay();
  sDisp.setTextSize(1);
  sDisp.setTextColor(SSD1306_WHITE);
  sDisp.setCursor(0, 0);
  row("HELIX " HELIX_VERSION);
  row("ESP32-S3-N16R8");
  row("OLED I2C 128x64");
  row("SDA 8   SCL 9");
  row("SD  CS10 SCK12");
  row("MOSI 11 MISO 13");
  sDisp.display();
  helixLog("info", "OLED SSD1306 128x64  SDA 8 SCL 9");
}

static void paint() {
  Telemetry t;
  telemetryCopy(t);
  char line[24];
  char tail[22] = "";
  if (sMux) xSemaphoreTake(sMux, portMAX_DELAY);
  if (sLogN) clip(tail, sizeof(tail), sLog[sLogN - 1]);
  if (sMux) xSemaphoreGive(sMux);

  sDisp.clearDisplay();
  sDisp.setTextSize(1);
  sDisp.setTextColor(SSD1306_WHITE);
  sDisp.setCursor(0, 0);
  row("HELIX " HELIX_VERSION);

  const char* who = t.ssid[0] ? t.ssid : (t.mode[0] ? t.mode : "—");
  snprintf(line, sizeof(line), "%-4s %s", t.mode[0] ? t.mode : "off", who);
  line[21] = 0;
  row(line);

  snprintf(line, sizeof(line), "%s", t.ip[0] ? t.ip : "0.0.0.0");
  line[21] = 0;
  row(line);

  snprintf(line, sizeof(line), "CPU %2.0f/%2.0f  %4.1fC", t.cpu0, t.cpu1, t.tempC);
  row(line);

  snprintf(line, sizeof(line), "H%3uK P%4.1fM", (unsigned)(t.heapFree / 1024), t.psramFree / 1048576.f);
  row(line);

  if (t.sdOk) {
    double gb = (double)t.sdTotal / 1073741824.0;
    snprintf(line, sizeof(line), "SD %s %4.1fG", t.sdType[0] ? t.sdType : "ok", gb);
  } else {
    snprintf(line, sizeof(line), "SD assente");
  }
  row(line);

  snprintf(line, sizeof(line), ">%s", tail[0] ? tail : "in attesa");
  line[21] = 0;
  row(line);
  sDisp.display();
}

void oledPoll() {
  if (!sOk) return;
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last < 500) return;
  last = now;
  paint();
}

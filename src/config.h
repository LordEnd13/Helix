#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Helix 1.0 — quattro moduli nativi: Wi-Fi, Monitor, Terminale, GPIO.
// Core 0 (net): radio, DNS captive, drain WebSocket. Non tocca GPIO.
// Core 1 (sys): telemetria, CLI, pin. Non chiama WiFi.* : legge lo snapshot.

#define HELIX_VERSION "1.0"
#define HELIX_AP_SSID "Helix-S3"
#define HELIX_HOSTNAME "helix-s3"
#define HELIX_TEL_MS 1000
#define HELIX_STA_TIMEOUT_MS 15000
#define HELIX_WS_MAX_CLIENTS 4
#define HELIX_MSG 896

// OLED 0.96" SSD1306 I2C. SDA/SCL sono i pin Wire di default sull'S3.
// Non sono sul bus octal (26–37) né su USB (19/20).
#define HELIX_OLED_SDA 8
#define HELIX_OLED_SCL 9
#define HELIX_OLED_ADDR 0x3C

// MicroSD su SPI2/FSPI (IO_MUX). Solo 3,3 V sul modulo.
#define HELIX_SD_CS 10
#define HELIX_SD_MOSI 11
#define HELIX_SD_SCK 12
#define HELIX_SD_MISO 13
#define HELIX_SD_HZ 20000000

enum HelixPinMode : uint8_t { PIN_FREE = 0, PIN_IN, PIN_OUT, PIN_PWM, PIN_ADC };

struct HelixGpio {
  uint8_t pin;
  HelixPinMode mode;
  uint8_t value;
  uint8_t duty;
  uint16_t freq;
};

struct Telemetry {
  float cpu0;
  float cpu1;
  float tempC;
  uint32_t heapFree;
  uint32_t heapTotal;
  uint32_t psramFree;
  uint32_t psramTotal;
  uint32_t fsUsed;
  uint32_t fsTotal;
  int32_t rssi;
  uint32_t uptimeS;
  char ip[16];
  char ssid[33];
  char mode[12];
  char gpio[320];
  uint8_t sdOk;
  uint8_t oledOk;
  uint64_t sdUsed;
  uint64_t sdTotal;
  char sdType[8];
};

struct NetMsg {
  char text[HELIX_MSG];
};

// Code create in setup(), prima di ogni task.
extern QueueHandle_t gTx;
extern QueueHandle_t gCli;
extern SemaphoreHandle_t gTelMux;
extern Telemetry gTel;

void helixLog(const char* lvl, const char* msg);
void helixLogf(const char* lvl, const char* fmt, ...);
bool helixEnqueueTx(const char* json);
void helixLogDump(String& out);

// Pin usabili sul WROOM-1 N16R8. 26–37 sono flash + PSRAM octal.
bool helixPinAllowed(int pin);

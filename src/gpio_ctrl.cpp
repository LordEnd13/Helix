#include "gpio_ctrl.h"

#include "config.h"

#include <Preferences.h>
#include <string.h>

// Sul N16R8 i GPIO 26–37 sono flash e PSRAM, 19–20 sono USB.
// 8/9 I2C OLED, 10–13 SPI SD: non sono selezionabili.
static const uint8_t kPins[] = {1, 2, 4, 5, 6, 7, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48};
static const size_t kCount = sizeof(kPins) / sizeof(kPins[0]);

static HelixGpio sPins[sizeof(kPins) / sizeof(kPins[0])];

bool helixPinAllowed(int pin) {
  for (size_t i = 0; i < kCount; i++)
    if (kPins[i] == pin) return true;
  return false;
}

static int indexOf(int pin) {
  for (size_t i = 0; i < kCount; i++)
    if (kPins[i] == pin) return (int)i;
  return -1;
}

static const char* modeName(HelixPinMode m) {
  switch (m) {
    case PIN_IN:
      return "in";
    case PIN_OUT:
      return "out";
    case PIN_PWM:
      return "pwm";
    case PIN_ADC:
      return "adc";
    default:
      return "free";
  }
}

static void persist() {
  Preferences p;
  p.begin("helix", false);
  p.putBytes("gpio", sPins, sizeof(sPins));
  p.end();
}

static void applyHardware(HelixGpio& g) {
  switch (g.mode) {
    case PIN_IN:
      pinMode(g.pin, INPUT);
      break;
    case PIN_OUT:
      pinMode(g.pin, OUTPUT);
      digitalWrite(g.pin, g.value ? HIGH : LOW);
      break;
    case PIN_PWM: {
      const uint32_t freq = g.freq ? g.freq : 5000;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcAttach(g.pin, freq, 8);
      ledcWrite(g.pin, g.duty);
#else
      const uint8_t ch = (uint8_t)(indexOf(g.pin) & 7);
      ledcSetup(ch, freq, 8);
      ledcAttachPin(g.pin, ch);
      ledcWrite(ch, g.duty);
#endif
      break;
    }
    case PIN_ADC:
      pinMode(g.pin, INPUT);
      analogReadResolution(12);
      analogSetPinAttenuation(g.pin, ADC_11db);
      break;
    default:
      pinMode(g.pin, INPUT);
      break;
  }
}

void gpioInit() {
  for (size_t i = 0; i < kCount; i++) {
    sPins[i] = {};
    sPins[i].pin = kPins[i];
    sPins[i].mode = PIN_FREE;
    sPins[i].freq = 5000;
  }
  Preferences p;
  p.begin("helix", true);
  size_t got = p.getBytes("gpio", sPins, sizeof(sPins));
  p.end();
  if (got == sizeof(sPins)) {
    for (size_t i = 0; i < kCount; i++) {
      sPins[i].pin = kPins[i];
      if (sPins[i].mode != PIN_FREE) applyHardware(sPins[i]);
    }
    helixLog("info", "gpio ripristinati da NVS");
  } else if (got > 0) {
    helixLog("warn", "gpio NVS scartata: mappa pin cambiata");
  }
}

void gpioCompact(char* out, size_t n) {
  if (!n) return;
  out[0] = 0;
  size_t used = 0;
  for (size_t i = 0; i < kCount; i++) {
    HelixGpio& g = sPins[i];
    if (g.mode == PIN_FREE) continue;
    int metric = g.value;
    if (g.mode == PIN_PWM) metric = g.duty;
    if (g.mode == PIN_ADC) metric = analogReadMilliVolts(g.pin);
    if (g.mode == PIN_IN) metric = digitalRead(g.pin);
    char piece[24];
    snprintf(piece, sizeof(piece), "%s%u:%s:%d", used ? "," : "", g.pin, modeName(g.mode), metric);
    size_t len = strlen(piece);
    if (used + len + 1 >= n) break;
    memcpy(out + used, piece, len + 1);
    used += len;
  }
}

bool gpioSetMode(int pin, const char* mode) {
  int i = indexOf(pin);
  if (i < 0 || !mode) return false;
  HelixPinMode m = PIN_FREE;
  if (!strcmp(mode, "in")) m = PIN_IN;
  else if (!strcmp(mode, "out")) m = PIN_OUT;
  else if (!strcmp(mode, "pwm")) m = PIN_PWM;
  else if (!strcmp(mode, "adc")) m = PIN_ADC;
  else if (!strcmp(mode, "free")) m = PIN_FREE;
  else return false;
  if (sPins[i].mode == PIN_PWM && m != PIN_PWM) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcDetach(pin);
#else
    ledcDetachPin(pin);
#endif
  }
  sPins[i].mode = m;
  if (m == PIN_PWM && sPins[i].freq == 0) sPins[i].freq = 5000;
  applyHardware(sPins[i]);
  persist();
  return true;
}

bool gpioWrite(int pin, int value) {
  int i = indexOf(pin);
  if (i < 0 || sPins[i].mode != PIN_OUT) return false;
  sPins[i].value = value ? 1 : 0;
  digitalWrite(pin, sPins[i].value ? HIGH : LOW);
  persist();
  return true;
}

bool gpioPwm(int pin, int duty, int freq) {
  int i = indexOf(pin);
  if (i < 0) return false;
  if (duty < 0) duty = 0;
  if (duty > 255) duty = 255;
  if (freq < 1) freq = 1;
  if (freq > 40000) freq = 40000;
  sPins[i].mode = PIN_PWM;
  sPins[i].duty = (uint8_t)duty;
  sPins[i].freq = (uint16_t)freq;
  sPins[i].value = duty > 0 ? 1 : 0;
  applyHardware(sPins[i]);
  persist();
  return true;
}

bool gpioDescribe(int pin, String& line) {
  int i = indexOf(pin);
  if (i < 0) return false;
  HelixGpio& g = sPins[i];
  if (g.mode == PIN_ADC) line = "GPIO" + String(pin) + " adc " + String(analogReadMilliVolts(pin)) + " mV";
  else if (g.mode == PIN_PWM) line = "GPIO" + String(pin) + " pwm " + String(g.duty) + "/255 @ " + String(g.freq) + " Hz";
  else if (g.mode == PIN_IN) line = "GPIO" + String(pin) + " in " + String(digitalRead(pin));
  else if (g.mode == PIN_OUT) line = "GPIO" + String(pin) + " out " + String(g.value);
  else line = "GPIO" + String(pin) + " free";
  return true;
}

void gpioDump(String& out) {
  out = "";
  for (size_t i = 0; i < kCount; i++) {
    String line;
    gpioDescribe(sPins[i].pin, line);
    if (out.length()) out += "\n";
    out += line;
  }
}

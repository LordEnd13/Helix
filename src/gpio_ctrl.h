#pragma once

#include <Arduino.h>

void gpioInit();
void gpioCompact(char* out, size_t n);
bool gpioSetMode(int pin, const char* mode);
bool gpioWrite(int pin, int value);
bool gpioPwm(int pin, int duty, int freq);
bool gpioDescribe(int pin, String& line);
void gpioDump(String& out);

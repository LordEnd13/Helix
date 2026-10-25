#pragma once

#include <Arduino.h>

void labInit();
void labPoll();
void labSubmit(const char* json);
bool labIsType(const char* t);

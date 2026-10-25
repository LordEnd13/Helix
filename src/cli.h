#pragma once

#include <Arduino.h>

void cliSubmit(const char* line);
void cliPoll();
String cliSinceJson(uint16_t since);

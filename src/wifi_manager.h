#pragma once

#include "config.h"

void wifiInit();
void wifiPoll();
void wifiFill(Telemetry& t);
bool wifiApUp();
void wifiRequestConnect(const char* ssid, const char* pass);
void wifiSetApFallback(bool on);
bool wifiApFallback();
void wifiHold(bool on);
bool wifiRouter(bool on, const char* ssid, const char* pass);
String wifiScanJson();
String wifiStatusText();
bool wifiHostIsSta(const char* host);

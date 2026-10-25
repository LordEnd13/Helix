#pragma once

#include "config.h"

#include <Arduino.h>

bool sdInit();
void sdPoll();
bool sdReady();
void sdFill(Telemetry& t);
String sdStatusText();
String sdListJson(const String& path);
String sdAppsJson();
bool sdDelete(const String& path, String& err);
bool sdMkdir(const String& path, String& err);
bool sdReadText(const String& path, String& out, String& err);
bool sdWriteText(const String& path, const String& text, String& err);
bool sdUploadBegin(const String& dir, const String& filename, String& err);
bool sdUploadWrite(const uint8_t* data, size_t len);
void sdUploadEnd();

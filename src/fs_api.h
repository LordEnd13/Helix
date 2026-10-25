#pragma once

#include <Arduino.h>

bool fsInit();
bool fsLock(TickType_t ticks = pdMS_TO_TICKS(2000));
void fsUnlock();
void fsUsage(uint32_t& used, uint32_t& total);
bool fsSafePath(const String& in, String& out);
String fsListJson(const String& path);
bool fsDelete(const String& path, String& err);
bool fsMkdir(const String& path, String& err);
bool fsReadText(const String& path, String& out, String& err);
bool fsWriteText(const String& path, const String& text, String& err);

#pragma once

#include <Arduino.h>

void configLoad();
String configPublicJson();
bool configApplyJson(const String& body, String& err);
bool configLogin(const String& pass);
const char* configUser();
void configUseCustomWall();
void configUseAvatar();

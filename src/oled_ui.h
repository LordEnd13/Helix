#pragma once

void oledInit();
void oledPoll();
bool oledReady();
// Solo copia la riga: il disegno I2C avviene in oledPoll, sul core 1.
void oledFeed(const char* msg);

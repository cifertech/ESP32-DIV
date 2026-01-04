#ifndef UTILS_H
#define UTILS_H

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>

extern TFT_eSPI tft;

#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25
#define XPT2046_CS    33

void updateStatusBar();
float readBatteryVoltage();
float readInternalTemperature();
bool isSDCardAvailable();
void drawStatusBar(float batteryVoltage, bool forceUpdate = false);

void initDisplay();
void showNotification(const char* title, const char* message);
void hideNotification();
void printWrappedText(int x, int y, int maxWidth, const char* text);
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center);
void displayLogo(uint16_t color, int displayTime);


namespace Terminal {
  void terminalSetup();
  void terminalLoop();
}

#endif

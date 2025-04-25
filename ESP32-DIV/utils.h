#ifndef UTILS_H
#define UTILS_H

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>

// Function prototypes
void updateStatusBar();
float readBatteryVoltage();
float readInternalTemperature();
bool isSDCardAvailable();
void drawStatusBar(float batteryVoltage, bool forceUpdate = false);

void initDisplay();
void showNotification(const char* title, const char* message);
void hideNotification();
void printWrappedText(int x, int y, int maxWidth, const char* text);

#endif

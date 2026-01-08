#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "SettingsStore.h"
#include "shared.h"

extern SPIClass touchscreenSPI;
extern XPT2046_Touchscreen ts;

#define  TOUCH_X_MIN 300
#define  TOUCH_X_MAX 3800
#define  TOUCH_Y_MIN 300
#define  TOUCH_Y_MAX 3800
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

extern bool feature_active;

void setupTouchscreen();

#endif

bool readTouchXY(int& x, int& y);

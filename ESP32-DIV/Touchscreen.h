#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "SettingsStore.h"
#include "shared.h"

extern SPIClass touchscreenSPI;
extern XPT2046_Touchscreen ts;

#ifndef TOUCH_X_MIN
#define TOUCH_X_MIN 300
#endif
#ifndef TOUCH_X_MAX
#define TOUCH_X_MAX 3800
#endif
#ifndef TOUCH_Y_MIN
#define TOUCH_Y_MIN 300
#endif
#ifndef TOUCH_Y_MAX
#define TOUCH_Y_MAX 3800
#endif
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 240
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 320
#endif

extern bool feature_active;

void setupTouchscreen();

/** True when the panel is pressed. Use this instead of ts.touched() directly. */
bool isTouchDown(uint16_t zThresh = 500);
/** Lower pressure threshold for release detection and light taps. */
bool isTouchDownDismiss(uint16_t zThresh = 120);
/** Raw ADC coords for calibration (before screen mapping). */
bool readTouchRawXY(int16_t& x, int16_t& y, uint16_t zThresh = 500);

bool readTouchXY(int& x, int& y);
/** Lower pressure threshold — use for "tap anywhere to dismiss" so light taps register. */
bool readTouchXYDismiss(int& x, int& y);

#endif

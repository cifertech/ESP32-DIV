#include "SettingsStore.h"
#include "Touchscreen.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

#if defined(BOARD_CYD)
SPIClass touchscreenSPI = SPIClass(VSPI);
#else
SPIClass touchscreenSPI = SPIClass(HSPI);
#endif

#ifndef XPT2046_IRQ
#define XPT2046_IRQ 255
#endif

XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool feature_active = false;

static bool s_touchInitialized = false;

#ifndef TOUCH_ROTATION
#if defined(BOARD_ESP32_DIV_V2)
#define TOUCH_ROTATION 0
#else
#define TOUCH_ROTATION TFT_ROTATION
#endif
#endif

#if TOUCH_SHARES_TFT_SPI
static void applyTouchRotation(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) {
  switch (TOUCH_ROTATION) {
    case 0: x = 4095 - rawY; y = rawX; break;
    case 1: x = rawX; y = rawY; break;
    case 2: x = rawY; y = 4095 - rawX; break;
    default: x = 4095 - rawX; y = 4095 - rawY; break;
  }
}

static bool readSharedTouchSample(int16_t& x, int16_t& y, int16_t& z, uint16_t zThresh) {
  if (!s_touchInitialized) {
    return false;
  }

  tft.endWrite();
  z = (int16_t)tft.getTouchRawZ();
  if (z < (int16_t)zThresh) {
    x = 0;
    y = 0;
    return false;
  }

  uint16_t rawX = 0;
  uint16_t rawY = 0;
  tft.getTouchRaw(&rawX, &rawY);
  applyTouchRotation((int16_t)rawX, (int16_t)rawY, x, y);
  return true;
}
#endif

static void ensureTouchSpiReady() {
#if !TOUCH_SHARES_TFT_SPI
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
#endif
}

static bool touchSampleOk(uint16_t zThresh, int16_t& rawX, int16_t& rawY) {
#if TOUCH_SHARES_TFT_SPI
  int16_t z = 0;
  return readSharedTouchSample(rawX, rawY, z, zThresh);
#else
  ensureTouchSpiReady();
#if defined(XPT2046_IRQ) && (XPT2046_IRQ < 255)
  if (!ts.tirqTouched()) {
    return false;
  }
#endif
  if (!ts.touched()) {
    return false;
  }
  TS_Point p = ts.getPoint();
  if (p.z < (int16_t)zThresh) {
    return false;
  }
  rawX = p.x;
  rawY = p.y;
  return true;
#endif
}

void setupTouchscreen() {
  if (s_touchInitialized) {
    return;
  }

#if TOUCH_SHARES_TFT_SPI
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);
#else
  ensureTouchSpiReady();
  ts.begin(touchscreenSPI);
  ts.setRotation(TOUCH_ROTATION);
#endif

  s_touchInitialized = true;
}

extern XPT2046_Touchscreen ts;

bool isTouchDown(uint16_t zThresh) {
  int16_t x = 0;
  int16_t y = 0;
  return touchSampleOk(zThresh, x, y);
}

bool isTouchDownDismiss(uint16_t zThresh) {
  return isTouchDown(zThresh);
}

bool readTouchRawXY(int16_t& x, int16_t& y, uint16_t zThresh) {
  return touchSampleOk(zThresh, x, y);
}

static void mapTouchToScreen(int16_t rawX, int16_t rawY, int& x, int& y) {
  auto& s = settings();
#if defined(BOARD_CYD)
  // Same axis order as the RNT CYD touch test (no inverted Y).
  x = ::map(rawX, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
  y = ::map(rawY, s.touchYMin, s.touchYMax, 0, TFT_HEIGHT - 1);
#else
  x = ::map(rawX, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
  y = ::map(rawY, s.touchYMax, s.touchYMin, 0, TFT_HEIGHT - 1);
#endif
}

bool readTouchXY(int& x, int& y) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(200, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}

bool readTouchXYDismiss(int& x, int& y) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(120, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}

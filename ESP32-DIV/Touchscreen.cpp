#include "SettingsStore.h"
#include "Touchscreen.h"


SPIClass touchscreenSPI = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS);
bool feature_active = false;

void setupTouchscreen() {
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}

extern XPT2046_Touchscreen ts;

bool readTouchXY(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  auto& s = settings();

  const uint16_t zThresh = 500;
  if (p.z < zThresh) return false;

  x = ::map(p.x, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
  y = ::map(p.y, s.touchYMax, s.touchYMin, 0, TFT_HEIGHT - 1);
  return true;
}

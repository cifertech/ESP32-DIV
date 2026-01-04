#include "Touchscreen.h"

SPIClass touchscreenSPI = SPIClass(VSPI); 
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ); 
bool feature_active = false; 

void setupTouchscreen() {
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); 
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}

// Touchscreen.cpp
#include "Touchscreen.h"

SPIClass touchscreenSPI = SPIClass(VSPI); // Define the SPI instance globally
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ); // Define the touchscreen object globally
bool feature_active = false; // Define the global feature_active flag

void setupTouchscreen() {
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS); // Initialize SPI with your pins
    ts.begin(touchscreenSPI); // Initialize touchscreen
    ts.setRotation(0); // Set rotation as specified (adjust if needed)
}

// Touchscreen.h
#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// Define touchscreen pins (updated with your values)
#define XPT2046_CS    33
#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25

// Declare the SPI instance and touchscreen object globally
extern SPIClass touchscreenSPI;
extern XPT2046_Touchscreen ts;

// Constants for touch calibration (updated with your mapping)
#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#define DISPLAY_WIDTH 240  // Your TFT screen width
#define DISPLAY_HEIGHT 320 // Your TFT screen height


// Global state flag for touch exclusivity
extern bool feature_active;

void setupTouchscreen(); // Function prototype for initialization

#endif

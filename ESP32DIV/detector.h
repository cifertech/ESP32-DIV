/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#ifndef detector_h
#define detector_h

#include <Arduino.h>

#include <Adafruit_GFX.h>   
#include <Adafruit_ST7735.h> 
#include <SPI.h>

void detectorSetup();
void detectorLoop();

#endif

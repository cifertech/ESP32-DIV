/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#ifndef beaconspam_h
#define beaconspam_h

#include <Arduino.h>

#include <Adafruit_GFX.h>   
#include <Adafruit_ST7735.h> 
#include <SPI.h>

void beaconspamSetup();
void beaconspamLoop();

#endif

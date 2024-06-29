/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#ifndef packetmonitor_H
#define packetmonitor_H

#include "arduinoFFT.h"
#include <Adafruit_GFX.h>   
#include <Adafruit_ST7735.h> 
#include <Adafruit_NeoPixel.h>
#include <SPI.h>

#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>

void packetmonitorSetup();
void packetmonitorLoop();

#endif

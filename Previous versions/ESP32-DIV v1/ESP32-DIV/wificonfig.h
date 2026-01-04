#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include "arduinoFFT.h"
#include "utils.h"

#include <WiFi.h>
#include <TFT_eSPI.h> 
#include <PCF8574.h>
#include <XPT2046_Touchscreen.h>

extern TFT_eSPI tft;
extern PCF8574 pcf;

#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25
#define XPT2046_CS    33

#include "WiFi.h"
#include <esp_wifi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include <nvs_flash.h>
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <SD.h>
#include <Update.h>
#include <ESPmDNS.h>
using namespace std;


namespace PacketMonitor {
  void ptmSetup();
  void ptmLoop();
}

namespace BeaconSpammer {
  void beaconSpamSetup();
  void beaconSpamLoop();
}

namespace DeauthDetect {
  void deauthdetectSetup();
  void deauthdetectLoop();
}

namespace WifiScan {
  void wifiscanSetup();
  void wifiscanLoop();
}


namespace CaptivePortal {
  void cportalSetup();
  void cportalLoop();
}

namespace Deauther {
  void deautherSetup();
  void deautherLoop();
}

namespace FirmwareUpdate {
  void updateSetup();
  void updateLoop();
}


#endif // WIFICONFIG_H

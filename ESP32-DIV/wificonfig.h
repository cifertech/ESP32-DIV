#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <PCF8574.h>
#include <Preferences.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPT2046_Touchscreen.h>
#include <cstddef>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string>
#include "WiFi.h"
#include "arduinoFFT.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"
#include "shared.h"
#include "utils.h"

extern TFT_eSPI tft;
extern PCF8574 pcf;

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
  int  getLastCount();
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

#endif

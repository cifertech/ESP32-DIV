#ifndef BLECONFIG_H
#define BLECONFIG_H

#include "utils.h"

#include <TFT_eSPI.h> 
#include <PCF8574.h>
#include <XPT2046_Touchscreen.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include <Wire.h>


#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25
#define XPT2046_CS    33

extern TFT_eSPI tft;
extern PCF8574 pcf;


namespace BleJammer {
void blejamSetup();
void blejamLoop();
}

namespace BleSpoofer {
  void spooferSetup();
  void spooferLoop();
}

namespace SourApple {
  void sourappleSetup();
  void sourappleLoop();
}

namespace BleScan {
  void bleScanSetup();
  void bleScanLoop();
}

namespace Scanner {
  void scannerSetup();
  void scannerLoop();  
}

namespace ProtoKill {
  void prokillLoop();
  void prokillSetup();
}

namespace BleSniffer {
  void blesnifferLoop();
  void blesnifferSetup();
}

#endif // CONFIG_H

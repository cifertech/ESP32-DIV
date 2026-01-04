#ifndef BLECONFIG_H
#define BLECONFIG_H

#include <Arduino.h>
#include <PCF8574.h>
#include <RF24.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <XPT2046_Touchscreen.h>
#include <nRF24L01.h>
#include "BleCompat.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_wifi.h"
#include "shared.h"
#include "utils.h"

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

  void startBackgroundScanner();
  int  getLastCount();
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

#endif

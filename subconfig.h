#ifndef SUBCONFIG_H
#define SUBCONFIG_H

#include "utils.h"
#include "arduinoFFT.h"
#include <TFT_eSPI.h> 
#include <PCF8574.h>
#include <ELECHOUSE_CC1101_ESP32DIV.h>

#include <RCSwitch.h>
#include <EEPROM.h>
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>

extern TFT_eSPI tft;
extern PCF8574 pcf;

// ═══════════════════════════════════════════════════════════════════════════
// Radio Switching Support - Pin 16 shared between CC1101 GDO0 and NRF24 CE
// ═══════════════════════════════════════════════════════════════════════════
namespace replayat {
  extern RCSwitch mySwitch;           // RCSwitch instance for SubGHz receive
  extern bool subghz_receive_active;  // Flag: true when CC1101 has pin 16 interrupt
}

// Cleanup function - call BEFORE switching FROM SubGHz TO 2.4GHz modes
void cleanupSubGHz();

#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25
#define XPT2046_CS    33


namespace replayat {
  void ReplayAttackSetup();
  void ReplayAttackLoop();
}

namespace SavedProfile {
  void saveSetup();
  void saveLoop();
}

namespace subjammer {
  void subjammerSetup();
  void subjammerLoop();
}

namespace subbrute {
  void subBruteSetup();
  void subBruteLoop();
}


#endif // SUBCONFIG_H

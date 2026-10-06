#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <HardwareSerial.h>

#include "shared.h"
#include "utils.h"
#include "rfid.h"
#include "hwdetect.h"

HwPresence g_hwPresence;

// nRF24 (2.4GHz): shares the SD SPI bus (SCK=12/MISO=13/MOSI=11 on S3, the
// default SPI pins), CSN=4, CE=15. RF24::begin() uses the default SPI, so no
// explicit SPI.begin() is needed (mirrors BleJammer). Restore SD afterwards.
static bool detectNrf24() {
  RF24 radio(CE_PIN_1, CSN_PIN_1, 16000000);
  bool present = radio.begin() && radio.isChipConnected();
  radio.powerDown();
  restoreSdAfterSharedSpi();
  return present;
}

// CC1101 (Sub-GHz): SPI 18/19/23, CS on GPIO5. The SmartRC getCC1101() spins
// forever on `while(digitalRead(MISO))` if the chip does not answer, so we read
// the VERSION status register ourselves with a bounded CHIP_RDY wait. Mirrors
// SubGHz's bus bring-up (reclaim bus + deselect SD + CS high) so the chip
// actually responds.
static bool detectCC1101() {
  reclaimSharedSpiBus();
  // CC1101 shares the bus (12/13/11) with SD and nRF24 on V2. Deselect the
  // others so they don't hold MISO (a stuck-low MISO reads VERSION as 0x00).
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CSN_PIN_1)
  pinMode(CSN_PIN_1, OUTPUT);
  digitalWrite(CSN_PIN_1, HIGH);      // deselect nRF24
#endif
#if defined(CE_PIN_1)
  pinMode(CE_PIN_1, OUTPUT);
  digitalWrite(CE_PIN_1, LOW);        // nRF24 standby
#endif
  const int SCK = CC1101_SCK, MISO = CC1101_MISO, MOSI = CC1101_MOSI, CS = CC1101_CS;
  pinMode(SCK, OUTPUT);
  pinMode(MOSI, OUTPUT);
  pinMode(MISO, INPUT);
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);
  SPI.begin(SCK, MISO, MOSI, CS);

  // Bounded CHIP_RDY pre-check: assert CS and wait for MISO to go low. If the
  // module is absent MISO never drops, so we bail out here instead of letting
  // the SmartRC library spin on its infinite `while(digitalRead(MISO))`.
  // CHIP_RDY probe: MISO idles HIGH (board pull-up); a present CC1101 pulls it
  // LOW while CS is asserted. getCC1101() is unreliable here because VERSION
  // (0x31) reads 0 on this module even though PARTNUM/status read valid, so
  // CHIP_RDY is the dependable present/absent signal.
  const int idleMiso = digitalRead(MISO);
  digitalWrite(CS, LOW);
  bool ready = false;
  const uint32_t t0 = micros();
  while ((micros() - t0) < 5000) {
    if (digitalRead(MISO) == LOW) { ready = true; break; }
  }
  digitalWrite(CS, HIGH);
  restoreSdAfterSharedSpi();

  // Present only if MISO was free (idle HIGH) and the chip pulled it LOW on CS.
  return (idleMiso == HIGH) && ready;
}

// GPS (NEO-6M): UART2, RX on GPIO5. The module streams NMEA at ~1 Hz once
// powered, so any byte within ~1.2 s means it is installed.
static bool detectGps() {
  HardwareSerial gpsDet(2);
  gpsDet.begin(9600, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
  const uint32_t t0 = millis();
  bool seen = false;
  while (millis() - t0 < 1200) {
    if (gpsDet.available() > 0) { seen = true; break; }
    delay(5);
  }
  gpsDet.end();
  restoreSdAfterSharedSpi();
  return seen;
}

void hwDetectAll() {
  if (g_hwPresence.done) return;

  // CC1101, PN532 and GPS all share GPIO5, so they must be probed one at a
  // time. nRF24 is on the independent SD bus. Each probe restores SD after.
  g_hwPresence.nrf24  = detectNrf24();
  g_hwPresence.cc1101 = detectCC1101();
  g_hwPresence.pn532  = RfidNfc::begin();   // reuses the proven PN532 probe
  g_hwPresence.gps    = detectGps();

  restoreSdAfterSharedSpi();
  g_hwPresence.done = true;
}

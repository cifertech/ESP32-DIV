// hardware_detect.h — Module presence detection for ESP32-DIV
// IMPROVEMENT: Check for NRF24, CC1101, GPS, and IR hardware before launching features

#ifndef HARDWARE_DETECT_H
#define HARDWARE_DETECT_H

#include <Arduino.h>
#include <SPI.h>
#include "shared.h"

struct HardwareStatus {
  bool nrf24_present   = false;
  bool cc1101_present  = false;
  bool gps_present     = false;
  bool ir_present      = false;
  bool pn532_present   = false;
  bool sd_present      = false;
  bool pcf8574_present = false;
};

extern HardwareStatus hwStatus;

inline bool probeNRF24() {
  pinMode(CSN_PIN_1, OUTPUT);
  digitalWrite(CSN_PIN_1, LOW);
  uint8_t status = SPI.transfer(0xFF);
  digitalWrite(CSN_PIN_1, HIGH);
  bool present = (status != 0x00 && status != 0xFF);
  Serial.printf("[HW] NRF24 probe: STATUS=0x%02X -> %s\n", status, present ? "FOUND" : "NOT FOUND");
  return present;
}

inline bool probeCC1101() {
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(0x31 | 0xC0);
  uint8_t version = SPI.transfer(0x00);
  digitalWrite(CC1101_CS, HIGH);
  bool present = (version == 0x14 || version == 0x04);
  Serial.printf("[HW] CC1101 probe: VERSION=0x%02X -> %s\n", version, present ? "FOUND" : "NOT FOUND");
  return present;
}

inline bool probeGPS(Stream& gpsSerial, unsigned long timeoutMs = 2000) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (gpsSerial.available()) {
      if (gpsSerial.read() == '$') {
        Serial.println("[HW] GPS probe: NMEA data received -> FOUND");
        return true;
      }
    }
    delay(10);
  }
  Serial.println("[HW] GPS probe: No NMEA data within timeout -> NOT FOUND");
  return false;
}

inline bool probePCF8574(uint8_t addr) {
  Wire.beginTransmission(addr);
  uint8_t err = Wire.endTransmission();
  bool present = (err == 0);
  Serial.printf("[HW] PCF8574 probe: addr=0x%02X -> %s\n", addr, present ? "FOUND" : "NOT FOUND");
  return present;
}

inline void showHardwareStatus(TFT_eSPI& tft, int startY = 200) {
  tft.setTextSize(1);
  tft.setTextFont(1);
  struct ModuleInfo { const char* name; bool present; };
  ModuleInfo modules[] = {
    { "NRF24",   hwStatus.nrf24_present },
    { "CC1101",  hwStatus.cc1101_present },
    { "GPS",     hwStatus.gps_present },
    { "PN532",   hwStatus.pn532_present },
    { "SD Card", hwStatus.sd_present },
  };
  int y = startY;
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Hardware:", 8, y);
  y += 12;
  for (auto& m : modules) {
    tft.setTextColor(m.present ? TFT_GREEN : TFT_DARKGREY);
    tft.drawString(m.present ? "+" : "-", 8, y);
    tft.setTextColor(m.present ? TFT_WHITE : TFT_DARKGREY);
    tft.drawString(m.name, 18, y);
    y += 10;
  }
}

inline bool checkHardwareForFeature(TFT_eSPI& tft, const char* featureName, bool requiredHw) {
  if (requiredHw) return true;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.setTextSize(1);
  tft.setTextFont(2);
  tft.drawString("Hardware Not Found", 20, 80);
  tft.setTextColor(TFT_WHITE);
  tft.setTextFont(1);
  tft.drawString(featureName, 20, 110);
  tft.drawString("requires hardware that was not", 20, 125);
  tft.drawString("detected during startup.", 20, 138);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("Check wiring and restart.", 20, 160);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Press any button to go back", 20, 190);
  delay(3000);
  return false;
}

#endif // HARDWARE_DETECT_H

#pragma once

// One-shot hardware presence detection, run once at boot (before the background
// scanners and before the touchscreen is set up, since several radios share
// GPIO5 / the SPI buses). Results are cached in g_hwPresence and shown on the
// About screen.
struct HwPresence {
  bool nrf24  = false;   // 2.4GHz radio (nRF24L01)
  bool cc1101 = false;   // Sub-GHz radio (CC1101)
  bool pn532  = false;   // RFID/NFC (PN532)
  bool gps    = false;   // GPS (u-blox NEO-6M)
  bool done   = false;   // detection has run
};

extern HwPresence g_hwPresence;

void hwDetectAll();

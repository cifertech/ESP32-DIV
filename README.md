# ESP32-DIV v2.0 — FENRIR Edition

![ESP32](https://img.shields.io/badge/ESP32-Dev%20Module-blue?logo=espressif)
![Version](https://img.shields.io/badge/Version-2.0-green)
![License](https://img.shields.io/badge/License-Educational-orange)
![Status](https://img.shields.io/badge/Status-Ready%20to%20Flash-brightgreen)

> Multi-radio offensive security platform with WiFi, BLE, SubGHz (CC1101), and 2.4GHz (NRF24L01+) capabilities.

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32 Dev Module |
| **Display** | 2.4" TFT LCD (240x320) ILI9341 |
| **Touch** | XPT2046 Touch Controller |
| **SubGHz** | CC1101 Transceiver (300-928 MHz) |
| **2.4GHz** | NRF24L01+ Transceiver |
| **Buttons** | PCF8574 I2C GPIO Expander |
| **Storage** | SD Card + EEPROM |

---

## Features

### 2.4GHz Radio (NRF24L01+)
- **Channel Scanner** — Scan all 126 channels (2.400-2.525 GHz)
- **Spectrum Analyzer** — FFT-based signal visualization
- **WLAN Jammer** — Targeted WiFi disruption
- **Proto Kill** — Multi-protocol 2.4GHz disruption

### SubGHz Radio (CC1101)
- **Replay Attack** — Capture and replay RF signals (garages, gates, car fobs)
- **Brute Force** — Automated code transmission (Linear, CAME, Nice, Chamberlain, DoorHan, Gate TX)
- **Jammer** — Broadband SubGHz jamming

### WiFi (ESP32)
- **Packet Monitor** — Real-time capture with channel hopping
- **Beacon Spammer** — Fake AP generation (Rickroll mode included)
- **Deauther** — Targeted deauthentication attacks
- **Deauth Detector** — Passive attack monitoring
- **WiFi Scanner** — Network enumeration
- **Captive Portal** — Evil twin credential harvesting

### Bluetooth (ESP32 BLE)
- **BLE Jammer** — Bluetooth Low Energy disruption
- **BLE Spoofer** — Device address cloning
- **Sour Apple** — iOS popup flooding
- **BLE Sniffer** — Passive packet capture
- **BLE Scanner** — Device discovery

### Tools & Utilities
- Serial Terminal
- OTA Firmware Update
- Brightness Control
- Screen Timeout
- Device Info

---

## Quick Flash

### macOS
```bash
chmod +x flash_mac.sh
./flash_mac.sh
```

### Linux
```bash
chmod +x flash_linux.sh
./flash_linux.sh
```

### Windows
```batch
flash_windows.bat
```

> **Note:** Requires `esptool.py` — Install with `pip install esptool`

---

## Pin Configuration

### Touch Controller (XPT2046)
| Pin | GPIO |
|-----|------|
| IRQ | 34 |
| MOSI | 32 |
| MISO | 35 |
| CLK | 25 |
| CS | 33 |

### NRF24L01+
| Pin | GPIO |
|-----|------|
| CE | 4 |
| CSN | 5 |

### PCF8574 Buttons (I2C 0x20)
| Button | Pin |
|--------|-----|
| UP | 6 |
| DOWN | 3 |
| LEFT | 4 |
| RIGHT | 5 |
| SELECT | 7 |

---

## Bug Fixes (17 Total)

This release fixes critical issues from the original firmware:

| Priority | Fix |
|----------|-----|
| HIGH | Buffer overflow in deauth frame |
| HIGH | Wrong modulation for garage/Tesla replay |
| HIGH | Integer underflow on profile delete |
| MEDIUM | SCREEN_HEIGHT 64→320 (was OLED value) |
| MEDIUM | Double scanNetworks() call removed |
| LOW | Unused variables cleaned up |

---

## Build From Source

```bash
# Board: ESP32 Dev Module
# Partition: Huge APP (3MB No OTA/1MB SPIFFS)
# Flash: 4MB @ 240MHz

arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
```

### Required Libraries
- TFT_eSPI (ILI9341)
- XPT2046_Touchscreen
- PCF8574
- RF24
- ELECHOUSE_CC1101_SRC_DRV
- RCSwitch
- arduinoFFT
- ESP32 BLE Arduino

---

## Credits

| | |
|---|---|
| **Original Firmware** | [CiferTech](https://github.com/cifertech) |
| **FENRIR Upgrades** | JMFH |
| **Release Date** | January 2026 |

---

<p align="center">
<b>For authorized security research only.</b>
</p>

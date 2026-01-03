================================================================================
                         ESP32-DIV FIRMWARE v2.0
                      FENRIR UPGRADES EDITION
                         Prepared for CiferTech
                           January 3, 2026
================================================================================

This is a fully upgraded and audited version of the ESP32-DIV firmware with
major new features, hardware support additions, and comprehensive bug fixes.

================================================================================
                         HARDWARE CONFIGURATION
================================================================================

DISPLAY:
  - 2.4" TFT LCD (240x320) with XPT2046 Touch Controller
  - Full touchscreen support for all menus and features

RADIOS:
  - ESP32 WiFi/BLE (built-in)
  - CC1101 SubGHz Transceiver (300-928 MHz)
  - NRF24L01+ 2.4GHz Transceiver

CONTROLS:
  - PCF8574 I2C GPIO Expander for physical buttons
  - 5-way navigation (UP/DOWN/LEFT/RIGHT/SELECT)
  - Touchscreen input on all screens

STORAGE:
  - SD Card support for saving/loading profiles
  - EEPROM for persistent settings

================================================================================
                    NEW FEATURES - 2.4GHz RADIO (NRF24L01)
================================================================================

1. NRF24 CHANNEL SCANNER
   - Scans all 126 channels (2.400 - 2.525 GHz)
   - Real-time signal strength visualization
   - Identifies active frequencies and interference
   - Touch-enabled exit

2. 2.4GHz SPECTRUM ANALYZER  [NEW]
   - Full spectrum visualization across 2.4GHz band
   - FFT-based signal analysis
   - Graphical waterfall display
   - Identifies WiFi channels, Bluetooth, and other 2.4GHz devices
   - Touch-enabled navigation

3. WLAN JAMMER  [NEW]
   - Targeted 2.4GHz WiFi disruption via NRF24
   - Channel hopping mode for broad coverage
   - Single channel focus mode
   - Carrier wave transmission
   - Visual feedback on jamming status

4. PROTO KILL  [NEW]
   - Multi-protocol 2.4GHz disruption
   - Targets common IoT protocols
   - Configurable power levels
   - Channel sweep functionality

================================================================================
                    NEW FEATURES - SUBGHZ RADIO (CC1101)
================================================================================

1. REPLAY ATTACK
   - Capture RF signals (garage doors, car fobs, gates)
   - Automatic frequency detection
   - Signal storage and replay
   - Supports ASK/OOK modulation (fixed from original)
   - Visual waveform display

2. SUBGHZ BRUTE FORCE  [NEW]
   - Automated code transmission
   - Multiple protocol support:
     * Linear Fixed (10-bit)
     * CAME (12-bit)
     * Nice (12-bit)
     * Chamberlain (9-bit, 10-bit)
     * DoorHan (24-bit)
     * Gate TX (24-bit)
   - Progress tracking with time estimates
   - Pause/resume functionality
   - RMT hardware acceleration for precise timing

3. SUBGHZ JAMMER
   - Broadband RF jamming
   - Configurable frequency targeting
   - ASK/OOK modulation for maximum effectiveness
   - Visual jamming indicator

4. SAVED PROFILES
   - Save captured signals to EEPROM
   - Up to 5 profile slots
   - Profile naming support
   - Load and replay saved signals
   - Delete unwanted profiles
   - Integer underflow protection (fixed)

================================================================================
                        WIFI FEATURES (ESP32)
================================================================================

1. PACKET MONITOR
   - Real-time WiFi packet capture
   - Channel hopping
   - Packet type filtering
   - Statistics display

2. BEACON SPAMMER
   - Generate fake access points
   - Custom SSID lists
   - Rickroll mode
   - Random SSID generation

3. WIFI DEAUTHER
   - Target specific access points
   - Deauthentication frame injection
   - Client disconnection
   - Buffer overflow fixed (was writing out of bounds)

4. DEAUTH DETECTOR
   - Passive monitoring for deauth attacks
   - Alert notification
   - Attack source identification

5. WIFI SCANNER
   - Scan all available networks
   - Signal strength display
   - Encryption type detection
   - Channel information
   - Optimized scanning (removed duplicate scan call)

6. CAPTIVE PORTAL
   - Evil twin access point
   - Customizable login pages
   - Credential harvesting
   - DNS spoofing

================================================================================
                      BLUETOOTH FEATURES (ESP32 BLE)
================================================================================

1. BLE JAMMER
   - Bluetooth Low Energy disruption
   - Channel flooding
   - Advertisement spam

2. BLE SPOOFER
   - Clone BLE device addresses
   - Custom advertisement data
   - Device impersonation

3. SOUR APPLE
   - Apple device BLE spam attack
   - Popup flooding on iOS devices
   - AirPod/AirTag notification spam

4. BLE SNIFFER  [NEW]
   - Passive BLE packet capture
   - Advertisement monitoring
   - Device tracking
   - RSSI-based proximity detection

5. BLE SCANNER
   - Discover nearby BLE devices
   - Device type identification
   - MAC address logging
   - Service enumeration

================================================================================
                           TOOLS & UTILITIES
================================================================================

1. SERIAL TERMINAL
   - Built-in serial monitor
   - Debug output display
   - Command input

2. OTA FIRMWARE UPDATE
   - Web-based firmware upload
   - Creates WiFi hotspot for updates
   - Progress indication
   - No USB required for updates

3. BRIGHTNESS CONTROL  [NEW]
   - Adjustable screen brightness
   - Touch slider interface
   - Persistent setting

4. SCREEN TIMEOUT  [NEW]
   - Configurable auto-sleep
   - Power saving mode
   - Touch to wake

5. DEVICE INFO
   - System information display
   - Memory usage
   - Battery voltage
   - Temperature monitoring

================================================================================
                    UI/UX IMPROVEMENTS
================================================================================

- Full touchscreen support on ALL menus and features
- Added touch handlers for NRF Spectrum Analyzer
- Added touch handlers for Settings menu (Brightness, Timeout, Info)
- Consistent Back button placement
- Status bar with battery and temperature
- Smooth menu animations
- Icon-based navigation

================================================================================
                         BUG FIXES (17 Total)
================================================================================

HIGH PRIORITY (7):
  1. wifi.cpp:2559      - Buffer overflow in deauth frame (was [26], now [24])
  2. subghz.cpp:1839    - Wrong modulation (2-FSK to ASK/OOK for garage/Tesla)
  3. ESP32-DIV.ino:240  - Global init before hardware ready
  4. icon.h             - Missing header guards added
  5. ESP32-DIV.ino      - Missing NRF Spectrum Analyzer touch handler
  6. ESP32-DIV.ino      - Missing Settings menu touch handler
  7. subghz.cpp:1323    - Integer underflow on profile delete

MEDIUM PRIORITY (5):
  1. subghz.cpp:1531    - SCREEN_HEIGHT 64->320 (was OLED value)
  2. wifi.cpp:3040      - Double scanNetworks() call removed
  3. utils.cpp:43-46    - Redundant extern declarations removed
  4. utils.cpp:144      - Unused sdAvailable global removed
  5. utils.h:23         - Orphaned initDisplay() declaration removed

LOW PRIORITY (4):
  1. ESP32-DIV.ino      - Unused variable 'z' removed (8 locations)
  2. subghz.cpp         - Duplicate MAX_PROFILES macros removed
  3. wificonfig.h       - Duplicate includes removed (3)
  4. bleconfig.h        - Duplicate esp_bt.h include removed

================================================================================
                          PIN CONFIGURATION
================================================================================

TFT Display:
  - Uses TFT_eSPI library configuration (User_Setup.h)

Touch Controller (XPT2046):
  - IRQ:  GPIO 34
  - MOSI: GPIO 32
  - MISO: GPIO 35
  - CLK:  GPIO 25
  - CS:   GPIO 33

CC1101 SubGHz:
  - Standard SPI pins
  - GDO0 for signal detection

NRF24L01:
  - CE:  GPIO 4
  - CSN: GPIO 5

PCF8574 (Buttons):
  - I2C Address: 0x20
  - BTN_UP:     Pin 6
  - BTN_DOWN:   Pin 3
  - BTN_LEFT:   Pin 4
  - BTN_RIGHT:  Pin 5
  - BTN_SELECT: Pin 7

================================================================================
                          COMPILATION NOTES
================================================================================

Board Configuration:
  - Board: ESP32 Dev Module
  - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
  - Flash Size: 4MB
  - CPU Frequency: 240MHz

Arduino-CLI Command:
  arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .

Required Libraries:
  - TFT_eSPI (configured for ILI9341)
  - XPT2046_Touchscreen
  - PCF8574
  - RF24
  - ELECHOUSE_CC1101_SRC_DRV
  - RCSwitch
  - arduinoFFT
  - ESP32 BLE Arduino

Build Statistics:
  - Storage: 1,845,325 bytes (58% of 3,145,728 bytes)
  - RAM: 89,672 bytes (27% of 327,680 bytes)

================================================================================
                           FILE STRUCTURE
================================================================================

  ESP32-DIV.ino     - Main firmware, menu system, touch handling
  wifi.cpp          - All WiFi attack modules
  wificonfig.h      - WiFi module declarations
  bluetooth.cpp     - BLE attacks + NRF24 features (Scanner, Jammer, etc.)
  bleconfig.h       - Bluetooth/NRF module declarations
  subghz.cpp        - CC1101 SubGHz features (Replay, Brute, Jammer)
  subconfig.h       - SubGHz module declarations
  utils.cpp         - Utilities (notifications, terminal, display helpers)
  utils.h           - Utility declarations
  icon.h            - Menu icons (16x16 bitmaps)
  shared.h          - Shared definitions
  Touchscreen.cpp   - Touch calibration and handling
  Touchscreen.h     - Touch declarations

================================================================================
                             CREDITS
================================================================================

Original Firmware: CiferTech
Upgrades & Audit:  HaleHound - JMFH (FENRIR)
Date:              January 3, 2026

New Features Added:
  - 2.4GHz Spectrum Analyzer
  - WLAN Jammer (NRF24)
  - Proto Kill
  - SubGHz Brute Force
  - BLE Sniffer
  - Brightness Control
  - Screen Timeout
  - Full Touch Support

================================================================================
                   For authorized security research only.
================================================================================

# Changelog

## Unreleased

### Build e TFT

- Fixed the ESP32-S3 link conflict by making `ieee80211_raw_frame_sanity_check` weak in `wifi.cpp`.
- Restored the TFT display on ESP32-DIV V2/V2.0 by applying the recommended local TFT_eSPI V2 `User_Setup.h` configuration.
- Added boot diagnostics around battery read, menu draw, status bar draw, and touchscreen startup.

### Buzzer

- Added non-blocking `BuzzerService` with boot success and SubGHz capture feedback.
- Confirmed the integrated buzzer on GPIO 2 and enabled `BUZZER_PIN`.
- Tuned the boot/capture beep volume and duration for a quieter startup sound.

### WS2812B status LEDs

- Added non-blocking `StatusLedService` for the 4 onboard WS2812B LEDs.
- Confirmed the WS2812B data input on GPIO 1 from the V2 schematic and enabled `STATUS_LED_PIN`.
- Gated LED output behind `settings().neopixelEnabled`, so the LEDs remain off when NeoPixel is disabled in settings.
- Added WS2812 feedback for boot, idle, Wi-Fi scan, BLE scan, and SubGHz capture events.
- Kept WS2812 scan animations alive during blocking Wi-Fi/BLE scans.

### Status bar and icons

- Added real Wi-Fi/BLE status bar state handling for `off`, `scanning`, `active`, and `error`.
- Updated foreground Wi-Fi/BLE scans to force status bar redraws when scanning starts and finishes.
- Preserved manual Wi-Fi/BLE scan counts in the status bar even when auto-scan is disabled.
- Replaced the unknown battery text with a USB-powered battery icon with a lightning bolt when battery voltage is unavailable.

### Battery safety

- Made battery reads safe when `BATTERY_ADC_PIN` is not configured, avoiding `analogRead(-1)`.
- Fixed the status bar task so unknown battery voltage is not converted into a fake percentage.

### Hardware map

- Added initial `BoardPins_ESP32DIV_V2.h` documentation for validated pins and known future-module conflicts.

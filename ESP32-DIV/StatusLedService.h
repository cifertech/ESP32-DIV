#pragma once

#include <Arduino.h>

namespace StatusLedService {
enum class Mode : uint8_t {
  Off,
  Boot,
  Idle,
  WifiScan,
  BleScan,
  SubGhzActivity,
  Nrf24Activity,
  SdError,
  BatteryLow,
  GpsSearching,
  GpsFix
};

enum class Event : uint8_t {
  BootOk,
  CaptureSuccess,
  SdError,
  Warning
};

void begin();
void loop();
void setMode(Mode mode);
void startActivity(Mode mode);
void stopActivity(Mode nextMode = Mode::Idle);
void event(Event event);
void off();
}

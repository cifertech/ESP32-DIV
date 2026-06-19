#include "BuzzerService.h"

#include "shared.h"

namespace BuzzerService {
namespace {
constexpr uint8_t kLedcChannel = 7;
constexpr uint8_t kLedcResolution = 8;
constexpr uint16_t kBaseFrequency = 4000;

bool s_available = false;
bool s_active = false;
uint32_t s_offAtMs = 0;

bool pinIsValid() {
#if defined(BUZZER_PIN)
  return BUZZER_PIN >= 0;
#else
  return false;
#endif
}

void startTone(uint16_t hz, uint16_t durationMs) {
  if (!s_available || hz == 0 || durationMs == 0) {
    return;
  }

  ledcWriteTone(kLedcChannel, hz);
  s_active = true;
  s_offAtMs = millis() + durationMs;
}
}

void begin() {
  s_available = pinIsValid();
  s_active = false;

  if (!s_available) {
    return;
  }

  ledcSetup(kLedcChannel, kBaseFrequency, kLedcResolution);
  ledcAttachPin(BUZZER_PIN, kLedcChannel);
  ledcWriteTone(kLedcChannel, 0);
}

void loop() {
  if (!s_available || !s_active) {
    return;
  }

  if ((int32_t)(millis() - s_offAtMs) < 0) {
    return;
  }

  ledcWriteTone(kLedcChannel, 0);
  s_active = false;
}

void beepClick() {
  startTone(1800, 25);
}

void beepSuccess() {
  startTone(1800, 35);
}

void beepError() {
  startTone(500, 120);
}

void beepCapture() {
  startTone(2600, 70);
}

void beepWarning() {
  startTone(1200, 90);
}
}

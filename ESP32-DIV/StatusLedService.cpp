#include "StatusLedService.h"

#include <Adafruit_NeoPixel.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "SettingsStore.h"
#include "shared.h"

namespace StatusLedService {
namespace {
constexpr uint8_t kBrightness = 32;
constexpr uint32_t kFrameMs = 120;
constexpr uint32_t kEventMs = 650;

Adafruit_NeoPixel s_pixels(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
Mode s_mode = Mode::Off;
Event s_event = Event::BootOk;
bool s_available = false;
bool s_started = false;
bool s_eventActive = false;
volatile bool s_activityTaskRun = false;
TaskHandle_t s_activityTask = nullptr;
uint32_t s_lastFrameMs = 0;
uint32_t s_eventUntilMs = 0;
uint8_t s_phase = 0;

bool pinIsValid() {
#if defined(STATUS_LED_PIN)
  return STATUS_LED_PIN >= 0;
#else
  return false;
#endif
}

bool enabled() {
  return s_available && settings().neopixelEnabled;
}

uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return s_pixels.Color(r, g, b);
}

void clearPixels() {
  for (uint16_t i = 0; i < STATUS_LED_COUNT; ++i) {
    s_pixels.setPixelColor(i, 0);
  }
}

void showSolid(uint32_t c) {
  for (uint16_t i = 0; i < STATUS_LED_COUNT; ++i) {
    s_pixels.setPixelColor(i, c);
  }
  s_pixels.show();
}

void showChase(uint32_t c) {
  clearPixels();
  if (STATUS_LED_COUNT > 0) {
    s_pixels.setPixelColor(s_phase % STATUS_LED_COUNT, c);
  }
  s_pixels.show();
}

void renderMode() {
  switch (s_mode) {
    case Mode::Off:
      clearPixels();
      s_pixels.show();
      break;
    case Mode::Boot:
      showChase(color(24, 24, 24));
      break;
    case Mode::Idle:
      showSolid(color(0, 0, 4));
      break;
    case Mode::WifiScan:
      showChase(color(0, 18, 0));
      break;
    case Mode::BleScan:
      showChase(color(0, 8, 18));
      break;
    case Mode::SubGhzActivity:
      showChase(color(22, 10, 0));
      break;
    case Mode::Nrf24Activity:
      showChase(color(12, 0, 20));
      break;
    case Mode::SdError:
      showSolid(color(24, 0, 0));
      break;
    case Mode::BatteryLow:
      showSolid((s_phase & 1) ? color(24, 0, 0) : 0);
      break;
    case Mode::GpsSearching:
      showChase(color(0, 12, 10));
      break;
    case Mode::GpsFix:
      showSolid(color(0, 18, 4));
      break;
  }
}

void renderEvent() {
  switch (s_event) {
    case Event::BootOk:
      showSolid(color(0, 18, 0));
      break;
    case Event::CaptureSuccess:
      showSolid(color(24, 12, 0));
      break;
    case Event::SdError:
      showSolid(color(24, 0, 0));
      break;
    case Event::Warning:
      showSolid(color(24, 16, 0));
      break;
  }
}

void activityTask(void*) {
  while (s_activityTaskRun) {
    loop();
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  s_activityTask = nullptr;
  vTaskDelete(nullptr);
}

void startActivityTask() {
  if (s_activityTask != nullptr) {
    return;
  }
  s_activityTaskRun = true;
  xTaskCreatePinnedToCore(
    activityTask,
    "statusLed",
    2048,
    nullptr,
    1,
    &s_activityTask,
    0
  );
}
}

void begin() {
  s_available = pinIsValid() && STATUS_LED_COUNT > 0;
  s_started = false;
  s_eventActive = false;
  s_mode = Mode::Off;

  if (!s_available) {
    return;
  }

  s_pixels.begin();
  s_pixels.setBrightness(kBrightness);
  clearPixels();
  s_pixels.show();
  s_started = true;
}

void loop() {
  if (!s_started) {
    return;
  }

  if (!enabled()) {
    clearPixels();
    s_pixels.show();
    return;
  }

  const uint32_t now = millis();
  if (s_eventActive) {
    if ((int32_t)(now - s_eventUntilMs) >= 0) {
      s_eventActive = false;
    } else {
      renderEvent();
      return;
    }
  }

  if ((uint32_t)(now - s_lastFrameMs) < kFrameMs) {
    return;
  }

  s_lastFrameMs = now;
  ++s_phase;
  renderMode();
}

void setMode(Mode mode) {
  s_mode = mode;
  s_phase = 0;
  s_lastFrameMs = 0;
  if (s_started && enabled()) {
    renderMode();
  }
}

void startActivity(Mode mode) {
  setMode(mode);
  if (s_started && enabled()) {
    startActivityTask();
  }
}

void stopActivity(Mode nextMode) {
  s_activityTaskRun = false;
  setMode(nextMode);
}

void event(Event event) {
  if (!s_started || !enabled()) {
    return;
  }

  s_event = event;
  s_eventActive = true;
  s_eventUntilMs = millis() + kEventMs;
  renderEvent();
}

void off() {
  s_mode = Mode::Off;
  s_eventActive = false;
  if (s_started) {
    clearPixels();
    s_pixels.show();
  }
}
}

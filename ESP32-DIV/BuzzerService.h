#pragma once

#include <Arduino.h>

namespace BuzzerService {
void begin();
void loop();

void beepClick();
void beepSuccess();
void beepError();
void beepCapture();
void beepWarning();
}

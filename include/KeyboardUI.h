#ifndef KEYBOARD_UI_H
#define KEYBOARD_UI_H

#include <Arduino.h>

struct OnScreenKeyboardConfig {

  const char* titleLine1;
  const char* titleLine2;

  const char* const* rows;
  uint8_t rowCount;

  uint8_t maxLen;

  const char* const* shuffleNames;
  uint8_t shuffleCount;

  int16_t buttonsY;

  const char* backLabel;
  const char* middleLabel;
  const char* okLabel;

  bool enableShuffle;
  bool requireNonEmpty;
  const char* emptyErrorMsg;
};

struct OnScreenKeyboardResult {
  String text;
  bool accepted;
  bool cancelled;
};

OnScreenKeyboardResult showOnScreenKeyboard(const OnScreenKeyboardConfig& cfg,
                                            const String& initial);

#endif

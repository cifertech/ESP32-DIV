#include "KeyboardUI.h"
#include "Touchscreen.h"
#include "utils.h"


namespace {

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif

static inline uint16_t KB_BG()     { return FEATURE_BG; }
static inline uint16_t KB_SURF()   { return UI_FG; }
static inline uint16_t KB_BORDER() { return FEATURE_TEXT; }
static inline uint16_t KB_TEXT()   { return WHITE; }
static inline uint16_t KB_WARN()   { return UI_WARN; }

constexpr int INPUT_X      = 10;
constexpr int INPUT_Y      = 55;
constexpr int INPUT_W      = 220;
constexpr int INPUT_H      = 25;

constexpr int KEY_W        = 22;
constexpr int KEY_H        = 22;
constexpr int KEY_SPACING  = 2;
constexpr int KEY_START_X  = 1;
constexpr int KEY_START_Y  = 95;

constexpr unsigned long CURSOR_BLINK_MS = 500;

/* Row 3 ends with ^ (caps); row 4 ends with # (symbols) and < (backspace). */
static const char* const kStdRowsLower[OS_KEYBOARD_ROW_COUNT] = {
  "1234567890",
  "qwertyuiop",
  "asdfghjk ^",
  "zxcvbnm#<",
};

static const char* const kStdRowsUpper[OS_KEYBOARD_ROW_COUNT] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJK ^",
  "ZXCVBNM#<",
};

static const char* const kStdRowsSymbol[OS_KEYBOARD_ROW_COUNT] = {
  "1234567890",
  "!@#$%^&*()",
  "[]\\;':, ^",
  "(){}[]=+#<",
};

static const char* const* kbStdRowsFor(bool upperCase, bool symbolMode) {
  if (symbolMode) return kStdRowsSymbol;
  if (upperCase)  return kStdRowsUpper;
  return kStdRowsLower;
}

static bool kbIsCapsKey(uint8_t row, size_t col, size_t rowLen, char c) {
  return c == '^' && row == 2 && col + 1 == rowLen;
}

static bool kbIsSymbolKey(uint8_t row, size_t col, size_t rowLen, char c) {
  return c == '#' && row == 3 && col + 2 == rowLen;
}

static bool kbIsBackspaceKey(uint8_t row, size_t col, size_t rowLen, char c) {
  return c == '<' && row == 3 && col + 1 == rowLen;
}

static bool kbIsModifierKeyAt(uint8_t row, size_t col, size_t rowLen, char c) {
  return kbIsCapsKey(row, col, rowLen, c) || kbIsSymbolKey(row, col, rowLen, c);
}

static uint16_t kbKeyFill(char c, bool upperCase, bool symbolMode,
                          uint8_t row, size_t col, size_t rowLen) {
  if (kbIsCapsKey(row, col, rowLen, c) && upperCase && !symbolMode) return KB_BORDER();
  if (kbIsSymbolKey(row, col, rowLen, c) && symbolMode) return KB_BORDER();
  return KB_SURF();
}

static void kbDrawKeyLabel(char c, int x, int y, bool upperCase, bool symbolMode,
                           uint8_t row, size_t col, size_t rowLen) {
  if (kbIsCapsKey(row, col, rowLen, c)) {
    tft.print('^');
    return;
  }
  if (kbIsSymbolKey(row, col, rowLen, c)) {
    tft.print('#');
    return;
  }
  if (c == ' ') {
    tft.drawRect(x + 4, y + KEY_H - 6, KEY_W - 8, 2, KB_TEXT());
    return;
  }
  (void)upperCase;
  (void)symbolMode;
  tft.print(c);
}

static uint16_t kbKeyTextColor(uint16_t fill) {
  return (fill == KB_BORDER()) ? KB_BG() : KB_TEXT();
}

static bool readKeyboardTouch(int& x, int& y) {
#if TOUCH_SHARES_TFT_SPI
  tft.endWrite();
#endif
  return readTouchXY(x, y);
}

/** Wardriver / FeatureUI expect font 1 + size 1; keyboard uses size 2 for input. */
static void restoreTftAfterOsKeyboard() {
  tft.setTextSize(1);
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
}

static void dismissKeyboardTouch() {
  delay(100);
  for (int i = 0; i < 80; ++i) {
    int tx = 0;
    int ty = 0;
    if (!readTouchXY(tx, ty)) {
      break;
    }
    delay(10);
  }
  resetTouchNavHeldState();
}

static String kbInputVisibleText(const String& value, bool cursorOn, int maxPixelW) {
  String tail = value;
  if (cursorOn) {
    tail += "|";
  }
  tft.setTextFont(1);
  tft.setTextSize(2);
  while (tail.length() > 1 && (int)tft.textWidth(tail.c_str()) > maxPixelW) {
    tail.remove(0, 1);
  }
  return tail;
}

void drawInputField(const String& value, bool cursorOn) {
  constexpr int padX = 5;
  constexpr int padY = 5;
  const int innerW = INPUT_W - padX * 2;

  tft.fillRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, KB_SURF());
  tft.drawRect(INPUT_X - 1, INPUT_Y - 1, INPUT_W + 2, INPUT_H + 2, KB_BORDER());
  tft.setTextColor(KB_TEXT(), KB_SURF());
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextWrap(false);
  tft.setCursor(INPUT_X + padX, INPUT_Y + padY);
  tft.print(kbInputVisibleText(value, cursorOn, innerW));
}

void drawTitles(const OnScreenKeyboardConfig& cfg) {
  tft.setTextColor(KB_BORDER(), KB_BG());
  tft.setTextSize(1);
  if (cfg.titleLine1 && cfg.titleLine1[0]) {
    tft.setCursor(1, 230);
    tft.println(cfg.titleLine1);
  }
  if (cfg.titleLine2 && cfg.titleLine2[0]) {
    tft.setCursor(20, 245);
    tft.println(cfg.titleLine2);
  }
}

void drawKeyboardKeys(const char* const* rows, uint8_t rowCount,
                      bool upperCase, bool symbolMode) {
  int y = KEY_START_Y;
  for (uint8_t row = 0; row < rowCount; ++row) {
    int x = KEY_START_X;
    const char* rowStr = rows[row];
    size_t len = strlen(rowStr);
    for (size_t col = 0; col < len; ++col) {
      char c = rowStr[col];
      const uint16_t fill = kbKeyFill(c, upperCase, symbolMode, row, col, len);
      const uint16_t text = kbKeyTextColor(fill);

      tft.fillRect(x, y, KEY_W, KEY_H, fill);
      tft.setTextColor(text, fill);
      tft.setTextSize(1);
      tft.setCursor(x + 6, y + 5);
      kbDrawKeyLabel(c, x, y, upperCase, symbolMode, row, col, len);
      x += KEY_W + KEY_SPACING;
    }
    y += KEY_H + KEY_SPACING;
  }
}

void drawButtons(const OnScreenKeyboardConfig& cfg) {
  const int btnY = cfg.buttonsY;

  tft.setTextColor(KB_BORDER(), KB_SURF());
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);

  tft.fillRoundRect(5, btnY, 70, 25, 4, KB_SURF());
  tft.drawRoundRect(5, btnY, 70, 25, 4, KB_BORDER());
  tft.drawString(cfg.backLabel ? cfg.backLabel : "Back", 40, btnY + 12);

  tft.fillRoundRect(85, btnY, 70, 25, 4, KB_SURF());
  tft.drawRoundRect(85, btnY, 70, 25, 4, KB_BORDER());
  tft.drawString(cfg.middleLabel ? cfg.middleLabel : "", 120, btnY + 12);

  tft.fillRoundRect(165, btnY, 70, 25, 4, KB_SURF());
  tft.drawRoundRect(165, btnY, 70, 25, 4, KB_BORDER());
  tft.drawString(cfg.okLabel ? cfg.okLabel : "OK", 200, btnY + 12);

  tft.setTextDatum(TL_DATUM);
}

void showEmptyError(const OnScreenKeyboardConfig& cfg) {
  if (!cfg.emptyErrorMsg || !cfg.emptyErrorMsg[0]) return;
  tft.fillRect(INPUT_X, INPUT_Y + INPUT_H + 5, INPUT_W, 12, KB_BG());
  tft.setTextColor(KB_WARN(), KB_BG());
  tft.setTextSize(1);
  tft.setCursor(INPUT_X, INPUT_Y + INPUT_H + 7);
  tft.print(cfg.emptyErrorMsg);
}

bool handleKeyPress(char c, String& text, uint8_t maxLen,
                    bool& upperCase, bool& symbolMode,
                    uint8_t row, size_t col, size_t rowLen) {
  if (kbIsCapsKey(row, col, rowLen, c)) {
    if (symbolMode) {
      symbolMode = false;
    }
    upperCase = !upperCase;
    return true;
  }
  if (kbIsSymbolKey(row, col, rowLen, c)) {
    symbolMode = !symbolMode;
    return true;
  }
  if (kbIsBackspaceKey(row, col, rowLen, c)) {
    if (text.length() > 0) {
      text.remove(text.length() - 1);
    }
    return true;
  }
  if (c == '-') {
    text = "";
    return true;
  }
  if (c == ' ') {
    if (text.length() < maxLen) {
      text += ' ';
    }
    return true;
  }
  if (text.length() < maxLen) {
    text += c;
  }
  return true;
}

}

void osKeyboardUseStandardLayout(OnScreenKeyboardConfig& cfg) {
  cfg.rows     = nullptr;
  cfg.rowCount = OS_KEYBOARD_ROW_COUNT;
}

OnScreenKeyboardResult showOnScreenKeyboard(const OnScreenKeyboardConfig& cfg,
                                            const String& initial) {
  OnScreenKeyboardResult res;
  res.text      = initial;
  res.accepted  = false;
  res.cancelled = false;

  const bool useStandard = (cfg.rows == nullptr);
  bool upperCase = false;
  bool symbolMode = false;
  const char* const* activeRows =
      useStandard ? kbStdRowsFor(upperCase, symbolMode) : cfg.rows;
  const uint8_t activeRowCount =
      useStandard ? OS_KEYBOARD_ROW_COUNT : cfg.rowCount;

  tft.fillRect(0, 37, tft.width(), tft.height() - 37, KB_BG());

  bool cursorOn = true;
  unsigned long lastBlink = millis();

  drawTitles(cfg);
  drawInputField(res.text, cursorOn);
  drawKeyboardKeys(activeRows, activeRowCount, upperCase, symbolMode);
  drawButtons(cfg);

  uint8_t shuffleIndex = 0;

  while (true) {

    unsigned long now = millis();
    if (now - lastBlink >= CURSOR_BLINK_MS) {
      cursorOn = !cursorOn;
      drawInputField(res.text, cursorOn);
      lastBlink = now;
    }

    int tx, ty;
    if (!readKeyboardTouch(tx, ty)) {
      delay(10);
      continue;
    }

    delay(80);

    int y = KEY_START_Y;
    bool hitSomething = false;
    for (uint8_t row = 0; row < activeRowCount; ++row) {
      int x = KEY_START_X;
      const char* rowStr = activeRows[row];
      size_t len = strlen(rowStr);
      for (size_t col = 0; col < len; ++col) {
        if (tx >= x && tx <= x + KEY_W && ty >= y && ty <= y + KEY_H) {
          char c = rowStr[col];

          tft.fillRect(x, y, KEY_W, KEY_H, KB_BORDER());
          tft.setTextColor(KB_TEXT(), KB_BORDER());
          tft.setTextSize(1);
          tft.setCursor(x + 6, y + 5);
          if (useStandard && kbIsModifierKeyAt(row, col, len, c)) {
            kbDrawKeyLabel(c, x, y, upperCase, symbolMode, row, col, len);
          } else if (c == ' ') {
            tft.drawRect(x + 4, y + KEY_H - 6, KEY_W - 8, 2, KB_TEXT());
          } else {
            tft.print(c);
          }
          delay(100);

          if (useStandard) {
            handleKeyPress(c, res.text, cfg.maxLen, upperCase, symbolMode, row, col, len);
            activeRows = kbStdRowsFor(upperCase, symbolMode);
          } else if (c == '<') {
            if (res.text.length() > 0) {
              res.text.remove(res.text.length() - 1);
            }
          } else if (c == '-') {
            res.text = "";
          } else if (c != ' ') {
            if (res.text.length() < cfg.maxLen) {
              res.text += c;
            }
          } else if (res.text.length() < cfg.maxLen) {
            res.text += ' ';
          }

          drawKeyboardKeys(activeRows, activeRowCount, upperCase, symbolMode);
          drawInputField(res.text, cursorOn);
          hitSomething = true;
          break;
        }
        x += KEY_W + KEY_SPACING;
      }
      if (hitSomething) break;
      y += KEY_H + KEY_SPACING;
    }

    if (hitSomething) continue;

    const int btnY = cfg.buttonsY;

    if (tx >= 5 && tx <= 75 && ty >= btnY && ty <= btnY + 25) {
      res.cancelled = true;
      res.accepted  = false;
      dismissKeyboardTouch();
      restoreTftAfterOsKeyboard();
      return res;
    }

    if (tx >= 85 && tx <= 155 && ty >= btnY && ty <= btnY + 25) {
      if (cfg.enableShuffle && cfg.shuffleNames && cfg.shuffleCount > 0) {
        res.text = cfg.shuffleNames[shuffleIndex];
        shuffleIndex = (shuffleIndex + 1) % cfg.shuffleCount;
        drawInputField(res.text, cursorOn);
      } else {
        if (res.text.length() > 0) {
          res.text.remove(res.text.length() - 1);
          drawInputField(res.text, cursorOn);
        }
      }
      continue;
    }

    if (tx >= 165 && tx <= 235 && ty >= btnY && ty <= btnY + 25) {
      if (cfg.requireNonEmpty && res.text.length() == 0) {
        showEmptyError(cfg);
        continue;
      }
      res.accepted  = true;
      res.cancelled = false;
      dismissKeyboardTouch();
      restoreTftAfterOsKeyboard();
      return res;
    }
  }
}

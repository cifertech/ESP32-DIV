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

void drawInputField(const String& value, bool cursorOn) {
  tft.fillRect(INPUT_X, INPUT_Y, INPUT_W, INPUT_H, KB_SURF());
  tft.drawRect(INPUT_X - 1, INPUT_Y - 1, INPUT_W + 2, INPUT_H + 2, KB_BORDER());
  tft.setTextColor(KB_TEXT(), KB_SURF());
  tft.setTextSize(2);
  tft.setCursor(INPUT_X + 5, INPUT_Y + 5);
  String display = value;
  if (cursorOn) display += "|";
  tft.print(display);
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

void drawKeyboardKeys(const OnScreenKeyboardConfig& cfg) {
  int y = KEY_START_Y;
  for (uint8_t row = 0; row < cfg.rowCount; ++row) {
    int x = KEY_START_X;
    const char* rowStr = cfg.rows[row];
    size_t len = strlen(rowStr);
    for (size_t col = 0; col < len; ++col) {
      tft.fillRect(x, y, KEY_W, KEY_H, KB_SURF());
      tft.setTextColor(KB_TEXT(), KB_SURF());
      tft.setTextSize(1);
      tft.setCursor(x + 6, y + 5);
      tft.print(rowStr[col]);
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

}

OnScreenKeyboardResult showOnScreenKeyboard(const OnScreenKeyboardConfig& cfg,
                                            const String& initial) {
  OnScreenKeyboardResult res;
  res.text      = initial;
  res.accepted  = false;
  res.cancelled = false;

  tft.fillRect(0, 37, tft.width(), tft.height() - 37, KB_BG());

  bool cursorOn = true;
  unsigned long lastBlink = millis();

  drawTitles(cfg);
  drawInputField(res.text, cursorOn);
  drawKeyboardKeys(cfg);
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
    if (!readTouchXY(tx, ty)) {
      delay(10);
      continue;
    }

    delay(80);

    int y = KEY_START_Y;
    bool hitSomething = false;
    for (uint8_t row = 0; row < cfg.rowCount; ++row) {
      int x = KEY_START_X;
      const char* rowStr = cfg.rows[row];
      size_t len = strlen(rowStr);
      for (size_t col = 0; col < len; ++col) {
        if (tx >= x && tx <= x + KEY_W && ty >= y && ty <= y + KEY_H) {
          char c = rowStr[col];

          tft.fillRect(x, y, KEY_W, KEY_H, KB_BORDER());
          tft.setTextColor(KB_TEXT(), KB_BORDER());
          tft.setTextSize(1);
          tft.setCursor(x + 6, y + 5);
          tft.print(c);
          delay(100);
          tft.fillRect(x, y, KEY_W, KEY_H, KB_SURF());
          tft.setTextColor(KB_TEXT(), KB_SURF());
          tft.setCursor(x + 6, y + 5);
          tft.print(c);

          if (c == '<') {
            if (res.text.length() > 0) {
              res.text.remove(res.text.length() - 1);
            }
          } else if (c == '-') {
            res.text = "";
          } else if (c != ' ') {
            if (res.text.length() < cfg.maxLen) {
              res.text += c;
            }
          }

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
      return res;
    }
  }
}

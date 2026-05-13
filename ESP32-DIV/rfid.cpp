#include "rfid.h"

#include <Adafruit_PN532.h>
#include <SPI.h>
#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include "SettingsStore.h"
#include "Touchscreen.h"
#include "utils.h"
#include "shared.h"

#ifndef RFID_UID_CLONE
#define RFID_UID_CLONE 1
#endif

#ifndef RFID_TAG_LISTEN_TIMEOUT_MS
#define RFID_TAG_LISTEN_TIMEOUT_MS 20000
#endif

#ifndef RFID_CLONE_PHASE_TIMEOUT_MS
#define RFID_CLONE_PHASE_TIMEOUT_MS 45000
#endif

static Adafruit_PN532 s_nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
static bool s_hwOk = false;
static char s_pn532VerStr[28] = "";
static FeatureUI::Button s_rfFoot[2];
static FeatureUI::Button s_rfPageFoot[3];

enum CardType { UNKNOWN, MIFARE_CLASSIC, MIFARE_ULTRALIGHT, NTAG, MIFARE_DESFIRE };

enum class RfidUiEvt { None, Back, Primary };
enum class RfidPageEvt { None, Back, Next, Prev };

static void rfidAttachBus() {
  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  if (s_hwOk) {
    s_nfc.begin();
    s_nfc.SAMConfig();
    s_nfc.setPassiveActivationRetries(0xFF);
  }
}

static void rfidRestoreBus() {
#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS)
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
#endif
}

static void rfidPn532VerStr(uint32_t ver) {
  if (!ver) {
    s_pn532VerStr[0] = '\0';
    return;
  }
  snprintf(s_pn532VerStr, sizeof(s_pn532VerStr), "PN532 0x%06lX", (unsigned long)ver);
}

static void rfidReleaseNavButtons() {
  delay(12);
  for (int i = 0; i < 80; i++) {
    if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
        !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
        !isButtonPressed(BTN_DOWN)) {
      break;
    }
    delay(5);
  }
}

static RfidUiEvt rfidPollFooter(FeatureUI::Button* btns, int n, bool mapSelectToPrimary) {
  if (isButtonPressed(BTN_LEFT)) {
    rfidReleaseNavButtons();
    return RfidUiEvt::Back;
  }
  if (n == 1 && isButtonPressed(BTN_SELECT)) {
    rfidReleaseNavButtons();
    return RfidUiEvt::Back;
  }
  if (mapSelectToPrimary && n >= 2 && isButtonPressed(BTN_SELECT)) {
    if (!btns[1].disabled) {
      rfidReleaseNavButtons();
      return RfidUiEvt::Primary;
    }
  }
  int x = 0, y = 0;
  if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
    return RfidUiEvt::None;
  }
  int h = FeatureUI::hit(btns, n, x, y);
  if (h < 0) {
    return RfidUiEvt::None;
  }
  if (btns[h].disabled) {
    return RfidUiEvt::None;
  }
  rfidReleaseNavButtons();
  if (h == 0) {
    return RfidUiEvt::Back;
  }
  return RfidUiEvt::Primary;
}

static constexpr int RF_PAD_X = FeatureUI::PAD_X;
static constexpr int RF_RESULT_LINE_PX = 17;
static constexpr int RF_BODY_LINE_PX = 18;

static constexpr int RF_SB_H = 20;
static constexpr int RF_HEAD_TOP = RF_SB_H;
static constexpr int RF_HEAD_H = 34;
static constexpr int RF_RULE_Y = RF_HEAD_TOP + RF_HEAD_H;

static constexpr int RF_CARD_TOP = RF_RULE_Y + 4;
static constexpr int RF_CARD_INNER_PAD = 10;
static constexpr int RF_CARD_RADIUS = 6;

static constexpr uint16_t kRfidCardBg = 0x2104;
static constexpr int RF_CARD_PAD = 8;

static constexpr int RF_HINT_Y = TFT_HEIGHT - FeatureUI::FOOTER_H - 16;
static constexpr int RF_DYN_BOTTOM = RF_HINT_Y - 10;
static constexpr int RF_DYN_TOP_MAIN = RF_CARD_TOP;

static constexpr int RF_DYN_TOP_PROG = RF_DYN_TOP_MAIN;

static char s_rfidBandAux[72];

static void rfidFeatureSetup() {
  applyThemeToPalette(settings().theme);
  tft.fillScreen(FEATURE_BG);
  drawStatusBar(readBatteryVoltage(), true);
  s_rfidBandAux[0] = '\0';
}

static void rfidSetBandAux(const char* s) {
  if (!s || !s[0]) {
    s_rfidBandAux[0] = '\0';
    return;
  }
  strncpy(s_rfidBandAux, s, sizeof(s_rfidBandAux) - 1);
  s_rfidBandAux[sizeof(s_rfidBandAux) - 1] = '\0';
}

static void rfidDrawFeatureHeader(const char* title) {
  const int headX = RF_CARD_PAD;
  const int headY = RF_HEAD_TOP + 2;
  const int headW = TFT_WIDTH - 2 * RF_CARD_PAD;
  const int headH = RF_HEAD_H - 5;
  tft.fillRoundRect(headX, headY, headW, headH, RF_CARD_RADIUS, kRfidCardBg);
  tft.drawRoundRect(headX, headY, headW, headH, RF_CARD_RADIUS, UI_LINE);
  tft.fillRoundRect(headX + 6, headY + 5, 4, headH - 10, 2, UI_ICON);

  tft.setTextFont(2);
  tft.setTextColor(UI_ICON, kRfidCardBg);
  const int fh = tft.fontHeight();
  const int ty = headY + (headH - fh) / 2;
  tft.setCursor(headX + 16, ty);
  tft.print(title);

  const int chipW = 62;
  const int chipH = 13;
  const int chipX = headX + headW - chipW - 6;
  const int chipY = headY + (headH - chipH) / 2;
  tft.fillRoundRect(chipX, chipY, chipW, chipH, chipH / 2, FEATURE_BG);
  tft.drawRoundRect(chipX, chipY, chipW, chipH, chipH / 2, UI_LINE);
  static const char kIsoLabel[] = "ISO14443A";
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  const int isoW = tft.textWidth(kIsoLabel);
  const int isoH = tft.fontHeight();
  tft.setCursor(chipX + (chipW - isoW) / 2, chipY + (chipH - isoH) / 2);
  tft.print(kIsoLabel);
}

static void rfidPrintWrappedStepBg(int x, int y, int maxWidth, const char* text, int lineStepPx,
                                   uint16_t fg, uint16_t bg, int bottomY = TFT_HEIGHT) {
  String message = text ? text : "";
  int cursorY = y;
  bool clipped = false;

  while (message.length() > 0) {
    if (cursorY > bottomY - lineStepPx) {
      clipped = true;
      break;
    }

    int nl = message.indexOf('\n');
    String paragraph = (nl >= 0) ? message.substring(0, nl) : message;
    message = (nl >= 0) ? message.substring(nl + 1) : "";

    if (paragraph.length() == 0) {
      cursorY += lineStepPx;
      continue;
    }

    while (paragraph.length() > 0) {
      if (cursorY > bottomY - lineStepPx) {
        clipped = true;
        break;
      }

      int lineEnd = (int)paragraph.length();
      while (tft.textWidth(paragraph.substring(0, lineEnd)) > maxWidth && lineEnd > 1) {
        lineEnd--;
      }
      if (lineEnd < (int)paragraph.length()) {
        int lastSpace = paragraph.substring(0, lineEnd).lastIndexOf(' ');
        if (lastSpace > 0) {
          lineEnd = lastSpace;
        }
      }

      tft.setTextColor(fg, bg);
      tft.setCursor(x, cursorY);
      tft.print(paragraph.substring(0, lineEnd));
      paragraph = paragraph.substring(lineEnd);
      paragraph.trim();
      cursorY += lineStepPx;
    }
  }

  if (clipped && bottomY >= y + lineStepPx) {
    tft.setTextColor(UI_DIM_TEXT, bg);
    tft.setCursor(x, bottomY - lineStepPx);
    tft.print("...");
  }
}

static void rfidPrintWrappedStep(int x, int y, int maxWidth, const char* text, int lineStepPx) {
  rfidPrintWrappedStepBg(x, y, maxWidth, text, lineStepPx, UI_TEXT, FEATURE_BG);
}

static int rfidCountWrappedLines(int maxWidth, const char* text) {
  String message = text ? text : "";
  int total = 0;
  while (message.length() > 0) {
    int nl = message.indexOf('\n');
    String paragraph = (nl >= 0) ? message.substring(0, nl) : message;
    message = (nl >= 0) ? message.substring(nl + 1) : "";

    if (paragraph.length() == 0) {
      total++;
      continue;
    }

    while (paragraph.length() > 0) {
      int lineEnd = (int)paragraph.length();
      while (tft.textWidth(paragraph.substring(0, lineEnd)) > maxWidth && lineEnd > 1) {
        lineEnd--;
      }
      if (lineEnd < (int)paragraph.length()) {
        int lastSpace = paragraph.substring(0, lineEnd).lastIndexOf(' ');
        if (lastSpace > 0) {
          lineEnd = lastSpace;
        }
      }
      paragraph = paragraph.substring(lineEnd);
      paragraph.trim();
      total++;
    }
  }
  return total > 0 ? total : 1;
}

static void rfidPrintWrappedPageBg(int x, int y, int maxWidth, const char* text, int lineStepPx,
                                   uint16_t fg, uint16_t bg, int firstLine, int maxLines) {
  String message = text ? text : "";
  int lineNo = 0;
  int drawn = 0;
  while (message.length() > 0 && drawn < maxLines) {
    int nl = message.indexOf('\n');
    String paragraph = (nl >= 0) ? message.substring(0, nl) : message;
    message = (nl >= 0) ? message.substring(nl + 1) : "";

    if (paragraph.length() == 0) {
      if (lineNo >= firstLine) {
        drawn++;
      }
      lineNo++;
      continue;
    }

    while (paragraph.length() > 0 && drawn < maxLines) {
      int lineEnd = (int)paragraph.length();
      while (tft.textWidth(paragraph.substring(0, lineEnd)) > maxWidth && lineEnd > 1) {
        lineEnd--;
      }
      if (lineEnd < (int)paragraph.length()) {
        int lastSpace = paragraph.substring(0, lineEnd).lastIndexOf(' ');
        if (lastSpace > 0) {
          lineEnd = lastSpace;
        }
      }

      if (lineNo >= firstLine) {
        tft.setTextColor(fg, bg);
        tft.setCursor(x, y + drawn * lineStepPx);
        tft.print(paragraph.substring(0, lineEnd));
        drawn++;
      }
      paragraph = paragraph.substring(lineEnd);
      paragraph.trim();
      lineNo++;
    }
  }
}

static char s_dynPrev1[72];
static char s_dynPrev2[72];
static char s_dynPrevAux[72];
static const char* s_dynPillLabel = "LIVE";
static uint16_t s_dynPillColor = UI_WARN;
static bool s_dynCardDrawn = false;
static int s_dynCardTop = -1;
static int s_dynCardH = 0;

static unsigned s_listenSec = 0xFFFFFFFFu;
static uint8_t s_listenSpin = 0;
static unsigned long s_listenSpinMs = 0;

static void rfidListenShellReset() {
  s_listenSec = 0xFFFFFFFFu;
  s_listenSpin = 0;
  s_listenSpinMs = 0;
}

static void rfidDynClearPrev() {
  s_dynPrev1[0] = '\0';
  s_dynPrev2[0] = '\0';
  s_dynPrevAux[0] = '\0';
  s_dynCardDrawn = false;
  s_dynCardTop = -1;
  s_dynCardH = 0;
}

static void rfidSetDynamicPill(const char* label, uint16_t color) {
  if (!label) {
    return;
  }
  const bool changed = strcmp(s_dynPillLabel, label) != 0 || s_dynPillColor != color;
  s_dynPillLabel = label;
  s_dynPillColor = color;
  if (changed) {
    s_dynCardDrawn = false;
  }
}

static void rfidDrawHint(bool twoActions, int padX = RF_PAD_X) {
  tft.fillRect(0, RF_HINT_Y - 3, TFT_WIDTH, 17, FEATURE_BG);
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(padX, RF_HINT_Y);
  tft.print(twoActions ? "LEFT: Back   SELECT: action" : "LEFT / SELECT: close");
}

static void rfidDrawResultHint(bool paged) {
  tft.fillRect(0, RF_HINT_Y - 3, TFT_WIDTH, 17, FEATURE_BG);
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(RF_PAD_X, RF_HINT_Y);
  tft.print(paged ? "UP/Prev   DOWN/Next   LEFT Back" : "LEFT / SELECT: close");
}

static void rfidDynamicBand(int topY, const char* line1, const char* line2) {
  const char* l2 = line2 ? line2 : "";
  /* If chrome was invalidated (e.g. pill label changed), must run even when text unchanged. */
  if (s_dynCardDrawn && strcmp(s_dynPrev1, line1) == 0 && strcmp(s_dynPrev2, l2) == 0 &&
      strcmp(s_dynPrevAux, s_rfidBandAux) == 0) {
    return;
  }

  const int h = RF_DYN_BOTTOM - topY;
  const int cardX = RF_CARD_PAD;
  const int cardY = topY;
  const int cardW = TFT_WIDTH - 2 * RF_CARD_PAD;
  const int cardH = (h > 76) ? 76 : h;
  if (cardH < 48) {
    return;
  }

  const int pillX = cardX + RF_CARD_INNER_PAD;
  const int auxBandH = s_rfidBandAux[0] ? 14 : 0;
  const int pillY = cardY + 10 + auxBandH;
  const int pillW = 54;
  const int pillH = 16;
  const int textX = pillX + pillW + 8;
  const int textW = cardW - (textX - cardX) - RF_CARD_INNER_PAD;
  const int line2Y = pillY + 24;
  const bool drawChrome = !s_dynCardDrawn || s_dynCardTop != topY || s_dynCardH != cardH;

  if (drawChrome) {
    if (h > 0) {
      tft.fillRect(0, topY, TFT_WIDTH, h, FEATURE_BG);
    }
    tft.fillRoundRect(cardX, cardY, cardW, cardH, RF_CARD_RADIUS, kRfidCardBg);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, RF_CARD_RADIUS, UI_LINE);
    s_dynCardDrawn = true;
    s_dynCardTop = topY;
    s_dynCardH = cardH;
  } else {
    if (s_rfidBandAux[0]) {
      tft.fillRect(pillX, cardY + 6, cardW - 2 * RF_CARD_INNER_PAD, auxBandH + 4, kRfidCardBg);
    }
    tft.fillRect(textX, pillY - 2, textW, 36, kRfidCardBg);
    tft.fillRect(pillX, line2Y - 2, cardW - 2 * RF_CARD_INNER_PAD, cardH - (line2Y - cardY) - 4,
                 kRfidCardBg);
  }

  if (s_rfidBandAux[0]) {
    tft.setTextFont(1);
    tft.setTextColor(UI_DIM_TEXT, kRfidCardBg);
    String aux = s_rfidBandAux;
    const int auxMaxW = cardW - 2 * RF_CARD_INNER_PAD;
    const bool auxLong = tft.textWidth(aux) > auxMaxW;
    while (aux.length() > 0 && tft.textWidth(auxLong ? aux + "..." : aux) > auxMaxW) {
      aux.remove(aux.length() - 1);
    }
    if (auxLong) {
      aux += "...";
    }
    tft.setCursor(pillX, cardY + 8);
    tft.print(aux);
  }

  if (drawChrome) {
    tft.fillRoundRect(pillX, pillY, pillW, pillH, pillH / 2, s_dynPillColor);
    tft.drawRoundRect(pillX, pillY, pillW, pillH, pillH / 2, UI_ICON);
    tft.setTextFont(1);
    tft.setTextColor(FEATURE_BG, s_dynPillColor);
    int labelW = tft.textWidth(s_dynPillLabel);
    tft.setCursor(pillX + (pillW - labelW) / 2, pillY + 4);
    tft.print(s_dynPillLabel);
  }

  strncpy(s_dynPrev1, line1, sizeof(s_dynPrev1) - 1);
  s_dynPrev1[sizeof(s_dynPrev1) - 1] = '\0';
  strncpy(s_dynPrev2, l2, sizeof(s_dynPrev2) - 1);
  s_dynPrev2[sizeof(s_dynPrev2) - 1] = '\0';
  strncpy(s_dynPrevAux, s_rfidBandAux, sizeof(s_dynPrevAux) - 1);
  s_dynPrevAux[sizeof(s_dynPrevAux) - 1] = '\0';

  rfidPrintWrappedStepBg(textX, pillY + 4, textW, line1, 15, UI_TEXT, kRfidCardBg, pillY + 32);
  if (l2[0]) {
    rfidPrintWrappedStepBg(pillX, line2Y, cardW - 2 * RF_CARD_INNER_PAD, l2, 15, UI_DIM_TEXT,
                           kRfidCardBg, cardY + cardH - 6);
  }
}

static void rfidListenTick(const char* statusLine, unsigned long t0) {
  const char* spin = "|/-\\";
  unsigned sec = (unsigned)((millis() - t0) / 1000UL);
  char detail[40];
  if (sec != s_listenSec) {
    s_listenSec = sec;
    snprintf(detail, sizeof(detail), "%u s", sec);
  } else {
    if (millis() - s_listenSpinMs > 320) {
      s_listenSpin = (uint8_t)((s_listenSpin + 1) % 4);
      s_listenSpinMs = millis();
    }
    snprintf(detail, sizeof(detail), "%u s  %c", sec, spin[s_listenSpin]);
  }
  rfidDynamicBand(RF_DYN_TOP_MAIN, statusLine, detail);
}

/** Full chrome once — listening-style (PN532 / meta line on dynamic card, not header). */
static void rfidShellListen(const char* title, const char* footerLabel) {
  rfidListenShellReset();
  rfidDynClearPrev();
  rfidSetDynamicPill("SCAN", UI_WARN);
  rfidFeatureSetup();
  const char* sub = (s_pn532VerStr[0] != '\0') ? s_pn532VerStr : nullptr;
  rfidSetBandAux(sub);
  rfidDrawFeatureHeader(title);
  FeatureUI::drawFooterBg();
  FeatureUI::layoutFooter1(s_rfFoot[0], footerLabel, FeatureUI::ButtonStyle::Secondary);
  FeatureUI::drawButton(s_rfFoot[0]);
  rfidDrawHint(false);
  /* First listen tick may run next frame; paint card + aux now so the band is not empty. */
  rfidDynamicBand(RF_DYN_TOP_MAIN, "Ready", "");
}

/** Full chrome once — progress-style (step line on dynamic card under title). */
static void rfidShellProgress(const char* mainTitle, const char* stepLine, const char* footerLabel) {
  rfidListenShellReset();
  rfidDynClearPrev();
  rfidSetDynamicPill("WORK", UI_OK);
  rfidFeatureSetup();
  rfidSetBandAux(stepLine);
  rfidDrawFeatureHeader(mainTitle);
  FeatureUI::drawFooterBg();
  FeatureUI::layoutFooter1(s_rfFoot[0], footerLabel, FeatureUI::ButtonStyle::Secondary);
  FeatureUI::drawButton(s_rfFoot[0]);
  rfidDrawHint(false);
  rfidDynamicBand(RF_DYN_TOP_PROG, "Ready", "");
}

static void rfidLayoutFull(const char* title, const char* subtitleOrNull, const char* body,
                           bool twoButtons, const char* primaryLabel,
                           FeatureUI::ButtonStyle primaryStyle, bool primaryDisabled) {
  rfidFeatureSetup();
  rfidDrawFeatureHeader(title);

  const int cardY = RF_CARD_TOP;
  const int cardBottom = RF_HINT_Y - 8;
  const int cardH = cardBottom - cardY;
  const int cardW = TFT_WIDTH - 2 * RF_CARD_PAD;
  if (cardH > 12 && cardW > 0) {
    tft.fillRoundRect(RF_CARD_PAD, cardY, cardW, cardH, RF_CARD_RADIUS, kRfidCardBg);
    tft.drawRoundRect(RF_CARD_PAD, cardY, cardW, cardH, RF_CARD_RADIUS, UI_LINE);
    const int innerX = RF_CARD_PAD + RF_CARD_INNER_PAD;
    const int innerW = TFT_WIDTH - 2 * innerX;
    const bool hasSub = subtitleOrNull && subtitleOrNull[0];
    int tagTop = cardY + 7;
    int bodyTop = cardY + 27;
    if (hasSub) {
      tft.setTextFont(1);
      tft.setTextColor(UI_DIM_TEXT, kRfidCardBg);
      String sub = subtitleOrNull;
      const bool wasLong = tft.textWidth(sub) > innerW;
      while (sub.length() > 0 && tft.textWidth(wasLong ? sub + "..." : sub) > innerW) {
        sub.remove(sub.length() - 1);
      }
      if (wasLong) {
        sub += "...";
      }
      tft.setCursor(innerX, cardY + 8);
      tft.print(sub);
      tagTop = cardY + 22;
      bodyTop = tagTop + 20;
    }
    const char* tag = (primaryStyle == FeatureUI::ButtonStyle::Danger) ? "CAUTION" : "DETAILS";
    tft.setTextFont(1);
    tft.fillRoundRect(innerX, tagTop, 52, 14, 7, FEATURE_BG);
    tft.setTextColor(UI_ICON, FEATURE_BG);
    tft.setCursor(innerX + 7, tagTop + 3);
    tft.print(tag);
    rfidPrintWrappedStepBg(innerX, bodyTop, innerW, body, RF_BODY_LINE_PX,
                           UI_TEXT, kRfidCardBg, cardY + cardH - 8);
  }

  FeatureUI::drawFooterBg();
  if (twoButtons && primaryLabel) {
    FeatureUI::layoutFooter2(s_rfFoot, "Back", FeatureUI::ButtonStyle::Secondary, primaryLabel,
                             primaryStyle, false, primaryDisabled);
    FeatureUI::drawButton(s_rfFoot[0]);
    FeatureUI::drawButton(s_rfFoot[1]);
    rfidDrawHint(true);
  } else {
    FeatureUI::layoutFooter1(s_rfFoot[0], "Back", FeatureUI::ButtonStyle::Secondary);
    FeatureUI::drawButton(s_rfFoot[0]);
    rfidDrawHint(false);
  }
}

/** Result / summary layout — same chrome, roomier body text on card. */
static void rfidLayoutResult(const char* title, const char* subtitleOrNull, const char* body,
                             int pageIndex, int pageCount, int linesPerPage) {
  rfidFeatureSetup();
  rfidDrawFeatureHeader(title);

  const int cardY = RF_CARD_TOP;
  const int cardBottom = RF_HINT_Y - 8;
  const int cardH = cardBottom - cardY;
  const int cardW = TFT_WIDTH - 2 * RF_CARD_PAD;
  if (cardH > 12 && cardW > 0) {
    tft.fillRoundRect(RF_CARD_PAD, cardY, cardW, cardH, RF_CARD_RADIUS, kRfidCardBg);
    tft.drawRoundRect(RF_CARD_PAD, cardY, cardW, cardH, RF_CARD_RADIUS, UI_LINE);
    const int innerX = RF_CARD_PAD + RF_CARD_INNER_PAD;
    const int innerW = TFT_WIDTH - 2 * innerX;
    const bool hasSub = subtitleOrNull && subtitleOrNull[0];
    int tagTop = cardY + 7;
    int bodyTop = cardY + 27;
    if (hasSub) {
      tft.setTextFont(1);
      tft.setTextColor(UI_DIM_TEXT, kRfidCardBg);
      String sub = subtitleOrNull;
      const bool wasLong = tft.textWidth(sub) > innerW;
      while (sub.length() > 0 && tft.textWidth(wasLong ? sub + "..." : sub) > innerW) {
        sub.remove(sub.length() - 1);
      }
      if (wasLong) {
        sub += "...";
      }
      tft.setCursor(innerX, cardY + 8);
      tft.print(sub);
      tagTop = cardY + 22;
      bodyTop = tagTop + 20;
    }
    tft.setTextFont(1);
    const int pillW = (pageCount > 1) ? 62 : 48;
    tft.fillRoundRect(innerX, tagTop, pillW, 14, 7, FEATURE_BG);
    tft.setTextColor(UI_ICON, FEATURE_BG);
    tft.setCursor(innerX + 7, tagTop + 3);
    if (pageCount > 1) {
      tft.printf("%d/%d", pageIndex + 1, pageCount);
    } else {
      tft.print("RESULT");
    }
    rfidPrintWrappedPageBg(innerX, bodyTop, innerW, body, RF_RESULT_LINE_PX,
                           UI_TEXT, kRfidCardBg, pageIndex * linesPerPage, linesPerPage);
  }

  FeatureUI::drawFooterBg();
  if (pageCount > 1) {
    const bool hasNext = pageIndex < pageCount - 1;
    FeatureUI::layoutFooter3(s_rfPageFoot,
                             "Back", FeatureUI::ButtonStyle::Secondary,
                             "Prev", FeatureUI::ButtonStyle::Secondary,
                             hasNext ? "Next" : "Done", FeatureUI::ButtonStyle::Primary,
                             false, pageIndex == 0, false);
    FeatureUI::drawButton(s_rfPageFoot[0]);
    FeatureUI::drawButton(s_rfPageFoot[1]);
    FeatureUI::drawButton(s_rfPageFoot[2]);
    rfidDrawResultHint(true);
  } else {
    FeatureUI::layoutFooter1(s_rfFoot[0], "Back", FeatureUI::ButtonStyle::Secondary);
    FeatureUI::drawButton(s_rfFoot[0]);
    rfidDrawResultHint(false);
  }
}

static void rfidWaitBackOnly() {
  for (;;) {
    RfidUiEvt e = rfidPollFooter(s_rfFoot, 1, false);
    if (e != RfidUiEvt::None) {
      return;
    }
    delay(8);
  }
}

static RfidPageEvt rfidPollResultPage(int pageCount) {
  if (isButtonPressed(BTN_LEFT)) {
    rfidReleaseNavButtons();
    return RfidPageEvt::Back;
  }
  if (pageCount > 1 && isButtonPressed(BTN_UP)) {
    rfidReleaseNavButtons();
    return RfidPageEvt::Prev;
  }
  if (pageCount > 1 &&
      (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_RIGHT) || isButtonPressed(BTN_DOWN))) {
    rfidReleaseNavButtons();
    return RfidPageEvt::Next;
  }
  if (pageCount == 1 && isButtonPressed(BTN_SELECT)) {
    rfidReleaseNavButtons();
    return RfidPageEvt::Back;
  }

  int x = 0, y = 0;
  if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
    return RfidPageEvt::None;
  }
  if (pageCount > 1) {
    int h = FeatureUI::hit(s_rfPageFoot, 3, x, y);
    if (h < 0 || s_rfPageFoot[h].disabled) {
      return RfidPageEvt::None;
    }
    rfidReleaseNavButtons();
    if (h == 0) {
      return RfidPageEvt::Back;
    }
    if (h == 1) {
      return RfidPageEvt::Prev;
    }
    return RfidPageEvt::Next;
  }

  int h = FeatureUI::hit(s_rfFoot, 1, x, y);
  if (h < 0 || s_rfFoot[h].disabled) {
    return RfidPageEvt::None;
  }
  rfidReleaseNavButtons();
  return RfidPageEvt::Back;
}

static void rfidResultAndDismiss(const char* title, const char* sub, const char* body) {
  const int bodyY = RF_CARD_TOP + 27;
  const int bodyBottom = RF_HINT_Y - 16;
  const int innerX = RF_CARD_PAD + RF_CARD_INNER_PAD;
  const int innerW = TFT_WIDTH - 2 * innerX;
  int linesPerPage = (bodyBottom - bodyY) / RF_RESULT_LINE_PX;
  if (linesPerPage < 1) {
    linesPerPage = 1;
  }
  int totalLines = rfidCountWrappedLines(innerW, body);
  int pageCount = (totalLines + linesPerPage - 1) / linesPerPage;
  if (pageCount < 1) {
    pageCount = 1;
  }

  int page = 0;
  bool redraw = true;
  for (;;) {
    if (redraw) {
      rfidLayoutResult(title, sub, body, page, pageCount, linesPerPage);
      redraw = false;
    }

    RfidPageEvt e = rfidPollResultPage(pageCount);
    if (e == RfidPageEvt::Back) {
      return;
    }
    if (e == RfidPageEvt::Next) {
      if (page < pageCount - 1) {
        page++;
        redraw = true;
      } else {
        return;
      }
    }
    if (e == RfidPageEvt::Prev && page > 0) {
      page--;
      redraw = true;
    }
    delay(8);
  }
}

/** @return true = primary (right) action, false = back */
static bool rfidRunTwoButtonDialog(const char* title, const char* sub, const char* body,
                                   const char* primaryLabel, FeatureUI::ButtonStyle primaryStyle) {
  rfidLayoutFull(title, sub, body, true, primaryLabel, primaryStyle, false);
  for (;;) {
    RfidUiEvt e = rfidPollFooter(s_rfFoot, 2, true);
    if (e == RfidUiEvt::Back) {
      return false;
    }
    if (e == RfidUiEvt::Primary) {
      return true;
    }
    delay(8);
  }
}

static bool rfidPollCancel() { return rfidPollFooter(s_rfFoot, 1, false) != RfidUiEvt::None; }

static bool tryMagicBackdoor() {
  uint8_t commands[][2] = {{0x40, 0x00}, {0x43, 0x00}};
  uint8_t response[32];
  uint8_t responseLength = 32;
  for (int i = 0; i < 2; i++) {
    if (s_nfc.inDataExchange(commands[i], sizeof(commands[i]), response, &responseLength)) {
      return true;
    }
  }
  return false;
}

static CardType detectCardType(uint8_t* uid, uint8_t uidLength) {
  if (uidLength != 4 && uidLength != 7) {
    return UNKNOWN;
  }
  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, keyA)) {
    return MIFARE_CLASSIC;
  }
  uint8_t data[16];
  if (s_nfc.mifareultralight_ReadPage(4, data)) {
    uint8_t cmd[] = {0x60};
    uint8_t response[32];
    uint8_t responseLength = 32;
    if (s_nfc.inDataExchange(cmd, sizeof(cmd), response, &responseLength) &&
        responseLength >= 8) {
      if (response[0] == 0x00 && response[1] == 0x04) {
        return NTAG;
      }
    }
    return MIFARE_ULTRALIGHT;
  }
  uint8_t cmd[] = {0x5A, 0x00, 0x00, 0x00};
  uint8_t response[32];
  uint8_t responseLength = 32;
  if (s_nfc.inDataExchange(cmd, sizeof(cmd), response, &responseLength) && responseLength > 0) {
    return MIFARE_DESFIRE;
  }
  return UNKNOWN;
}

static const char* cardTypeStr(CardType t) {
  switch (t) {
    case MIFARE_CLASSIC:
      return "MIFARE Classic";
    case MIFARE_ULTRALIGHT:
      return "Ultralight";
    case NTAG:
      return "NTAG";
    case MIFARE_DESFIRE:
      return "DESFire";
    default:
      return "Unknown";
  }
}

static void uidToHex(char* out, size_t outLen, uint8_t* uid, uint8_t uidLen) {
  size_t pos = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < uidLen && i < 7u && pos + 3 < outLen; i++) {
    pos += (size_t)snprintf(out + pos, outLen - pos, "%02X", uid[i]);
  }
}

static void formatClassicBlock0(char* out, size_t outLen, const uint8_t* d) {
  snprintf(out, outLen,
           "Sector 0 block 0 (read with key A = FF FF FF FF FF FF):\n"
           "%02X %02X %02X %02X %02X %02X %02X %02X\n"
           "%02X %02X %02X %02X %02X %02X %02X %02X",
           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13],
           d[14], d[15]);
}

namespace RfidNfc {

bool begin() {
  if (s_hwOk) {
    return true;
  }
  rfidAttachBus();
  s_nfc.begin();
  uint32_t ver = s_nfc.getFirmwareVersion();
  if (!ver) {
    rfidPn532VerStr(0);
    restoreSdAfterSharedSpi();
    return false;
  }
  rfidPn532VerStr(ver);
  s_nfc.SAMConfig();
  s_nfc.setPassiveActivationRetries(0xFF);
  s_hwOk = true;
  rfidRestoreBus();
  return true;
}

bool hardwareOk() { return s_hwOk; }

void resetCloneBuffer() {}

static bool rfidListenIso14443a(const char* title, const char* footer, uint8_t* uid,
                                uint8_t* uidLenOut, unsigned long timeoutMs, const char* tickMsg) {
  unsigned long t0 = millis();
  rfidShellListen(title, footer);
  for (;;) {
    rfidListenTick(tickMsg, t0);
    if (rfidPollCancel()) {
      return false;
    }
    if (s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLenOut, 150)) {
      return true;
    }
    if (millis() - t0 > timeoutMs) {
      return false;
    }
    delay(12);
  }
}

void sessionErase() {
  if (!rfidRunTwoButtonDialog(
          "Erase", nullptr,
          "Clears MIFARE Classic data block 4 (16 bytes to 00) using default key A.\n\n"
          "Ultralight / NTAG are not erased by this mode.",
          "Continue", FeatureUI::ButtonStyle::Primary)) {
    return;
  }
  rfidAttachBus();
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!rfidListenIso14443a("Erase", "Cancel", uid, &uidLength, (unsigned long)RFID_TAG_LISTEN_TIMEOUT_MS,
                           "Classic tag — block 4 erase")) {
    rfidRestoreBus();
    return;
  }

  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);
  CardType ct = detectCardType(uid, uidLength);
  if (ct != MIFARE_CLASSIC) {
    rfidRestoreBus();
    rfidResultAndDismiss("Erase", "Skipped", "This erase only targets MIFARE Classic.");
    return;
  }

  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t emptyBlock[16] = {0};
  if (!s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keyA)) {
    rfidRestoreBus();
    rfidResultAndDismiss("Erase", "Auth failed", "Could not authenticate block 4 with key A.");
    return;
  }
  if (!s_nfc.mifareclassic_WriteDataBlock(4, emptyBlock)) {
    rfidRestoreBus();
    rfidResultAndDismiss("Erase", "Write failed", "Block 4 write was rejected.");
    return;
  }

  rfidRestoreBus();
  char body[180];
  snprintf(body, sizeof(body), "UID %s\nBlock 4 cleared (16 bytes zero).", uidStr);
  rfidResultAndDismiss("Erase", "Done", body);
}

void sessionDump() {
  if (!rfidRunTwoButtonDialog(
          "Dump", nullptr,
          "Reads Classic blocks 0–63 or UL/NTAG user pages.\n\n"
          "Summary here; full hex prints on Serial Monitor.",
          "Start", FeatureUI::ButtonStyle::Primary)) {
    return;
  }

  rfidAttachBus();
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!rfidListenIso14443a("Dump", "Cancel", uid, &uidLength, (unsigned long)RFID_TAG_LISTEN_TIMEOUT_MS,
                           "Present tag to dump")) {
    rfidRestoreBus();
    return;
  }

  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);
  CardType ct = detectCardType(uid, uidLength);
  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t data[16];
  int blocksWithData = 0;
  int blocksRead = 0;

  if (ct == MIFARE_CLASSIC) {
    rfidShellProgress("Dump", "Reading Classic blocks", "Cancel");
    Serial.println(F("\n--- RFID Dump Classic ---"));
    for (uint8_t block = 0; block < 64; block++) {
      char l2[48];
      snprintf(l2, sizeof(l2), "Block %u / 63", (unsigned)block);
      rfidDynamicBand(RF_DYN_TOP_PROG, "Dumping MIFARE Classic", l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        rfidResultAndDismiss("Dump", "Stopped", "Dump aborted.");
        return;
      }
      if (s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, keyA) &&
          s_nfc.mifareclassic_ReadDataBlock(block, data)) {
        blocksRead++;
        bool nz = false;
        for (int i = 0; i < 16; i++) {
          if (data[i] != 0) {
            nz = true;
          }
        }
        if (nz) {
          blocksWithData++;
        }
        Serial.printf("B%u:", (unsigned)block);
        for (int i = 0; i < 16; i++) {
          Serial.printf(" %02X", data[i]);
        }
        Serial.println();
      }
      delay(25);
    }
  } else if (ct == MIFARE_ULTRALIGHT || ct == NTAG) {
    uint8_t maxPages = (ct == MIFARE_ULTRALIGHT) ? 16u : 36u;
    rfidShellProgress("Dump", "Reading user pages", "Cancel");
    Serial.println(F("\n--- RFID Dump Ultralight / NTAG ---"));
    for (uint8_t page = 4; page < maxPages; page++) {
      char l2[48];
      snprintf(l2, sizeof(l2), "Page %u / %u", (unsigned)page, (unsigned)(maxPages - 1));
      rfidDynamicBand(RF_DYN_TOP_PROG, "Dumping pages", l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        rfidResultAndDismiss("Dump", "Stopped", "Dump aborted.");
        return;
      }
      uint8_t pg[4];
      if (s_nfc.mifareultralight_ReadPage(page, pg)) {
        blocksRead++;
        bool nz = false;
        for (int i = 0; i < 4; i++) {
          if (pg[i] != 0) {
            nz = true;
          }
        }
        if (nz) {
          blocksWithData++;
        }
        Serial.printf("P%u:", (unsigned)page);
        for (int i = 0; i < 4; i++) {
          Serial.printf(" %02X", pg[i]);
        }
        Serial.println();
      }
      delay(18);
    }
  } else {
    rfidRestoreBus();
    rfidResultAndDismiss("Dump", "Unsupported",
                         "Dump targets Classic or Ultralight/NTAG.\nTag did not match.");
    return;
  }

  rfidRestoreBus();
  char body[420];
  snprintf(body, sizeof(body),
           "UID: %s\nType: %s\n\n"
           "Segments read OK: %d\nBlocks/pages with any non-zero byte: %d\n\n"
           "Open Serial Monitor for full hex.",
           uidStr, cardTypeStr(ct), blocksRead, blocksWithData);
  rfidResultAndDismiss("Dump", "Done", body);
}

void sessionDecodeAccess() {
  if (!rfidRunTwoButtonDialog(
          "Decode Access", nullptr,
          "Reads MIFARE Classic sector 1 trailer (block 7) and shows raw access bytes.\n"
          "Uses default keys A/B.",
          "Scan", FeatureUI::ButtonStyle::Primary)) {
    return;
  }

  rfidAttachBus();
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!rfidListenIso14443a("Decode Access", "Cancel", uid, &uidLength, (unsigned long)RFID_TAG_LISTEN_TIMEOUT_MS,
                           "Classic tag")) {
    rfidRestoreBus();
    return;
  }

  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);
  CardType ct = detectCardType(uid, uidLength);
  if (ct != MIFARE_CLASSIC) {
    rfidRestoreBus();
    rfidResultAndDismiss("Decode Access", "Skipped", "Only MIFARE Classic sector trailers.");
    return;
  }

  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t keyB[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t blk[16] = {0};
  bool authenticated = false;
  (void)tryMagicBackdoor();
  if (s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 7, 0, keyA)) {
    authenticated = true;
  } else if (s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 7, 1, keyB)) {
    authenticated = true;
  }

  if (!authenticated || !s_nfc.mifareclassic_ReadDataBlock(7, blk)) {
    rfidRestoreBus();
    rfidResultAndDismiss("Decode Access", "Failed", "Could not read block 7 (sector 1 trailer).");
    return;
  }

  uint8_t a6 = blk[6], a7 = blk[7], a8 = blk[8];
  uint8_t c1 = (uint8_t)((a7 >> 5) & 0x01);
  uint8_t c2 = (uint8_t)((a8 >> 1) & 0x01);
  uint8_t c3 = (uint8_t)((a8 >> 5) & 0x01);
  uint8_t accessCond = (uint8_t)((c1 << 2) | (c2 << 1) | c3);

  rfidRestoreBus();

  char body[420];
  snprintf(body, sizeof(body),
           "UID %s\nSector 1 trailer (block 7):\n"
           "%02X %02X %02X %02X %02X %02X %02X %02X\n"
           "%02X %02X %02X %02X %02X %02X %02X %02X\n\n"
           "Access bytes [6..8]: %02X %02X %02X\n"
           "Condition bits C1..C3 (nibble-style): %u %u %u → code %u\n\n"
           "(Interpret with MFC access-bit tables for your sector.)",
           uidStr, blk[0], blk[1], blk[2], blk[3], blk[4], blk[5], blk[6], blk[7], blk[8], blk[9],
           blk[10], blk[11], blk[12], blk[13], blk[14], blk[15], a6, a7, a8, (unsigned)c1,
           (unsigned)c2, (unsigned)c3, (unsigned)accessCond);
  rfidResultAndDismiss("Decode Access", "Block 7", body);
}

void sessionJamReader() {
  if (!rfidRunTwoButtonDialog(
          "Jam Reader", "Authorized testing",
          "Puts the PN532 into ISO14443A target mode and sends rapid replies toward "
          "an external reader.\n\nKeep antennas close; LEFT/Cancel stops.",
          "Start", FeatureUI::ButtonStyle::Danger)) {
    return;
  }

  rfidAttachBus();
  s_nfc.setPassiveActivationRetries(0xFF);
  if (!s_nfc.inListPassiveTarget()) {
    rfidRestoreBus();
    rfidResultAndDismiss("Jam Reader", "Target failed",
                         "PN532 could not enter card-emulation target mode.\n"
                         "Try again near an active reader.");
    return;
  }

  rfidShellProgress("Jam Reader", "Flooding — Cancel stops", "Stop");
  rfidSetDynamicPill("JAM", UI_WARN);
  uint8_t targetData[64];
  uint8_t targetResponse[4] = {0xFF, 0xFF, 0xFF, 0xFF};

  for (;;) {
    if (rfidPollCancel()) {
      break;
    }
    uint8_t respLen = (uint8_t)sizeof(targetData);
    s_nfc.inDataExchange(targetResponse, 4, targetData, &respLen);
    rfidDynamicBand(RF_DYN_TOP_PROG, "Jam active", "Reader RF collision / flood");
    delay(10);
  }

  s_nfc.SAMConfig();
  s_nfc.setPassiveActivationRetries(0xFF);
  rfidRestoreBus();
  rfidResultAndDismiss("Jam Reader", "Stopped", "SAM reconfigured; reader mode restored.");
}

void sessionDisruptEmulate() {
  if (!rfidRunTwoButtonDialog(
          "Disrupt Emulate", "Authorized testing",
          "Target mode: cycles payloads (FF / 00 / random) on each exchange.\n"
          "Use only on gear you own or are permitted to test.",
          "Start", FeatureUI::ButtonStyle::Danger)) {
    return;
  }

  rfidAttachBus();
  s_nfc.setPassiveActivationRetries(0xFF);
  if (!s_nfc.inListPassiveTarget()) {
    rfidRestoreBus();
    rfidResultAndDismiss("Disrupt Emulate", "Target failed",
                         "Could not activate PN532 as passive target.");
    return;
  }

  rfidShellProgress("Disrupt Emulate", "Disrupting — Cancel stops", "Stop");
  rfidSetDynamicPill("TX", UI_WARN);
  uint8_t targetData[64];
  uint8_t targetResponse[16];
  uint8_t cycle = 0;

  for (;;) {
    if (rfidPollCancel()) {
      break;
    }
    if (cycle % 3 == 0) {
      memset(targetResponse, 0xFF, sizeof(targetResponse));
    } else if (cycle % 3 == 1) {
      memset(targetResponse, 0x00, sizeof(targetResponse));
    } else {
      for (unsigned i = 0; i < sizeof(targetResponse); i++) {
        targetResponse[i] = (uint8_t)(random(256));
      }
    }
    cycle++;

    uint8_t respLen = (uint8_t)sizeof(targetData);
    s_nfc.inDataExchange(targetResponse, sizeof(targetResponse), targetData, &respLen);
    rfidDynamicBand(RF_DYN_TOP_PROG, "Disrupt payload active", "FF / 00 / random");
    delay(5);
  }

  s_nfc.SAMConfig();
  s_nfc.setPassiveActivationRetries(0xFF);
  rfidRestoreBus();
  rfidResultAndDismiss("Disrupt Emulate", "Stopped", "SAM reconfigured.");
}

void sessionCardReader() {
  if (!rfidRunTwoButtonDialog(
          "Card Reader", nullptr,
          "Passive ISO14443A read.\n\n"
          "When you scan, this screen shows UID, tag family, internal type code, "
          "and memory samples (Classic block 0 or Ultralight pages 4–7) — no PC needed.\n\n"
          "Tap Scan, then hold the tag flat on the coil.",
          "Scan", FeatureUI::ButtonStyle::Primary)) {
    return;
  }

  unsigned long t0 = millis();
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  rfidAttachBus();

  rfidShellListen("Card Reader", "Cancel");
  for (;;) {
    rfidListenTick("Listening — present ISO14443A tag", t0);
    if (rfidPollCancel()) {
      rfidRestoreBus();
      return;
    }
    if (s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 120)) {
      break;
    }
    if (millis() - t0 > (unsigned long)RFID_TAG_LISTEN_TIMEOUT_MS) {
      rfidRestoreBus();
      char tmsg[96];
      snprintf(tmsg, sizeof(tmsg),
               "No ISO14443A tag in %lu s.\nTry again from the menu.",
               (unsigned long)(RFID_TAG_LISTEN_TIMEOUT_MS / 1000UL));
      rfidResultAndDismiss("Card Reader", "Timeout", tmsg);
      return;
    }
    delay(12);
  }

  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);
  CardType ct = detectCardType(uid, uidLength);

  char classicBlk[200] = "";
  if (ct == MIFARE_CLASSIC) {
    uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t data[16];
    if (s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, keyA) &&
        s_nfc.mifareclassic_ReadDataBlock(0, data)) {
      formatClassicBlock0(classicBlk, sizeof(classicBlk), data);
    } else {
      snprintf(classicBlk, sizeof(classicBlk),
               "Sector 0 block 0 was not readable using default key A (FF FF FF FF FF FF).");
    }
  }

  char ulSample[240] = "";
  if (ct == MIFARE_ULTRALIGHT || ct == NTAG) {
    uint8_t pg[4][4];
    bool ok = true;
    for (int pi = 0; pi < 4; pi++) {
      if (!s_nfc.mifareultralight_ReadPage((uint8_t)(4 + pi), pg[pi])) {
        ok = false;
      }
    }
    if (ok) {
      snprintf(ulSample, sizeof(ulSample),
               "User memory sample (pages 4–7, 4 bytes each):\n"
               "P4  %02X %02X %02X %02X\n"
               "P5  %02X %02X %02X %02X\n"
               "P6  %02X %02X %02X %02X\n"
               "P7  %02X %02X %02X %02X",
               pg[0][0], pg[0][1], pg[0][2], pg[0][3], pg[1][0], pg[1][1], pg[1][2], pg[1][3],
               pg[2][0], pg[2][1], pg[2][2], pg[2][3], pg[3][0], pg[3][1], pg[3][2], pg[3][3]);
    } else {
      snprintf(ulSample, sizeof(ulSample), "Pages 4–7 could not all be read.");
    }
  }

  rfidRestoreBus();

  const char* fw = s_pn532VerStr[0] ? s_pn532VerStr : "PN532 (version not cached)";
  char body[640];
  snprintf(body, sizeof(body),
           "Session: ISO14443A passive poll\n"
           "Reader IC: %s\n\n"
           "UID (hex): %s\n"
           "UID length: %u bytes\n"
           "Detected type: %s\n"
           "Type code (internal): %d\n",
           fw, uidStr, (unsigned)uidLength, cardTypeStr(ct), (int)ct);

  if (ct == MIFARE_CLASSIC && classicBlk[0]) {
    size_t len = strlen(body);
    snprintf(body + len, sizeof(body) - len, "\n%s", classicBlk);
  }
  if ((ct == MIFARE_ULTRALIGHT || ct == NTAG) && ulSample[0]) {
    size_t len = strlen(body);
    snprintf(body + len, sizeof(body) - len, "\n\n%s", ulSample);
  }
  if (ct == MIFARE_DESFIRE) {
    size_t len = strlen(body);
    snprintf(body + len, sizeof(body) - len, "\n\n"
             "DESFire / ISO-DEP-style tag detected.\n"
             "This tool only dumps UID here; full DESFire ops need other APDUs.");
  }
  if (ct == UNKNOWN) {
    size_t len = strlen(body);
    snprintf(body + len, sizeof(body) - len, "\n\n"
             "Family could not be determined from quick probes.");
  }

  rfidResultAndDismiss("Card Reader", "Result", body);
}

static uint8_t s_srcData[16][3][16];
static uint8_t s_srcPages[256][4];

void sessionClone() {
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t keyB[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t keyZero[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t data[16];
  uint8_t srcUidCopy[7];

#if RFID_UID_CLONE
  const char* introFoot =
      "Default keys (A/B) and zero.\nUID byte clone on Gen2 \"magic\" cards when detected.";
#else
  const char* introFoot = "Default keys (A/B) and zero. UID block may stay factory.";
#endif

  char introBody[180];
  snprintf(introBody, sizeof(introBody),
           "Two-tag flow: read source, then write a blank of the same type.\n\n%s",
           introFoot);

  if (!rfidRunTwoButtonDialog("Clone", "1 — Prepare", introBody, "Start",
                              FeatureUI::ButtonStyle::Primary)) {
    return;
  }

  rfidAttachBus();
  unsigned long tSrc = millis();
  rfidShellListen("Clone", "Cancel");
  for (;;) {
    rfidListenTick("Step 1/4 — Present SOURCE tag", tSrc);
    if (rfidPollCancel()) {
      rfidRestoreBus();
      return;
    }
    if (s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 150)) {
      break;
    }
    if (millis() - tSrc > (unsigned long)RFID_CLONE_PHASE_TIMEOUT_MS) {
      rfidRestoreBus();
      rfidResultAndDismiss("Clone", "Aborted", "No source tag (timeout).");
      return;
    }
    delay(15);
  }

  memcpy(srcUidCopy, uid, uidLength);
  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);

  CardType srcType = detectCardType(uid, uidLength);
  if (srcType == UNKNOWN) {
    rfidRestoreBus();
    rfidResultAndDismiss("Clone", "Unsupported", "Could not classify tag.\nNeed Classic, Ultralight, or NTAG.");
    return;
  }

  memset(s_srcData, 0, sizeof(s_srcData));
  memset(s_srcPages, 0, sizeof(s_srcPages));
  bool readOk = true;

  if (srcType == MIFARE_CLASSIC) {
    rfidShellProgress("Clone", "Step 2/4 — Read source", "Cancel");
    for (uint8_t sector = 0; sector < 16 && readOk; sector++) {
      char l1[48];
      char l2[48];
      snprintf(l1, sizeof(l1), "Classic — reading data blocks");
      snprintf(l2, sizeof(l2), "Sector %u / 15", (unsigned)sector);
      rfidDynamicBand(RF_DYN_TOP_PROG, l1, l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        return;
      }
      for (uint8_t block = 0; block < 3; block++) {
        uint8_t blockNum = (uint8_t)(sector * 4 + block);
        bool auth =
            s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNum, 0, keyA);
        if (!auth) {
          auth = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNum, 1, keyB);
        }
        if (auth) {
          if (s_nfc.mifareclassic_ReadDataBlock(blockNum, data)) {
            memcpy(s_srcData[sector][block], data, 16);
          } else {
            readOk = false;
          }
        } else {
          readOk = false;
        }
        delay(18);
      }
    }
  } else if (srcType == MIFARE_ULTRALIGHT || srcType == NTAG) {
    uint8_t maxPages = (srcType == MIFARE_ULTRALIGHT) ? 16u : 36u;
    rfidShellProgress("Clone", "Step 2/4 — Read source", "Cancel");
    for (uint8_t page = 4; page < maxPages; page++) {
      char l2[40];
      snprintf(l2, sizeof(l2), "Page %u / %u", (unsigned)page, (unsigned)(maxPages - 1));
      rfidDynamicBand(RF_DYN_TOP_PROG, "Ultralight / NTAG — user pages", l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        return;
      }
      if (s_nfc.mifareultralight_ReadPage(page, data)) {
        memcpy(s_srcPages[page], data, 4);
      } else {
        readOk = false;
      }
      delay(12);
    }
  }

  rfidRestoreBus();

  if (!readOk) {
    rfidResultAndDismiss("Clone", "Read failed", "Some blocks/pages did not read.\nCheck keys or tag damage.");
    return;
  }

  char summ[160];
  snprintf(summ, sizeof(summ),
           "Source UID %s\nType: %s\n\nRemove SOURCE and tap Continue when a blank "
           "same-type tag is ready.",
           uidStr, cardTypeStr(srcType));

  if (!rfidRunTwoButtonDialog("Clone", "3 — Swap tags", summ, "Continue",
                              FeatureUI::ButtonStyle::Primary)) {
    return;
  }

  rfidAttachBus();
  unsigned long tBlank = millis();
  rfidShellListen("Clone", "Cancel");
  for (;;) {
    rfidListenTick("Step 4/4 — Present BLANK tag", tBlank);
    if (rfidPollCancel()) {
      rfidRestoreBus();
      return;
    }
    if (s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 150)) {
      break;
    }
    if (millis() - tBlank > (unsigned long)RFID_CLONE_PHASE_TIMEOUT_MS) {
      rfidRestoreBus();
      rfidResultAndDismiss("Clone", "Timeout", "No blank tag seen.");
      return;
    }
    delay(15);
  }

  char blankUidStr[24] = "";
  uidToHex(blankUidStr, sizeof(blankUidStr), uid, uidLength);

  CardType blankType = detectCardType(uid, uidLength);
  if (blankType != srcType) {
    rfidRestoreBus();
    rfidResultAndDismiss("Clone", "Type mismatch", "Blank must match source family.");
    return;
  }

  bool cloneOk = true;
  int blocksDone = 0;
#if RFID_UID_CLONE
  bool gen2MagicDetected = false;
#endif

  if (blankType == MIFARE_CLASSIC) {
    bool magic = tryMagicBackdoor();
#if RFID_UID_CLONE
    gen2MagicDetected = magic;
#else
    (void)magic;
#endif
    bool uidWritten = false;
#if RFID_UID_CLONE
    if (magic) {
      bool ok = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, keyA);
      if (!ok) {
        ok = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 1, keyB);
      }
      if (!ok) {
        ok = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, keyZero);
      }
      if (ok) {
        uint8_t block0Data[16];
        memcpy(block0Data, srcUidCopy, uidLength);
        block0Data[4] =
            (uint8_t)(block0Data[0] ^ block0Data[1] ^ block0Data[2] ^ block0Data[3]);
        memcpy(&block0Data[5], &s_srcData[0][0][5], 11);
        if (s_nfc.mifareclassic_WriteDataBlock(0, block0Data)) {
          blocksDone++;
          uidWritten = true;
        } else {
          cloneOk = false;
        }
      } else {
        cloneOk = false;
      }
    }
#else
    (void)magic;
    (void)uidWritten;
#endif
    rfidShellProgress("Clone", "Writing — Classic", "Cancel");
    for (uint8_t sector = 0; sector < 16; sector++) {
      char l2[40];
      snprintf(l2, sizeof(l2), "Sector %u / 15", (unsigned)sector);
      rfidDynamicBand(RF_DYN_TOP_PROG, "Writing copied blocks", l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        rfidResultAndDismiss("Clone", "Stopped", "Clone aborted mid-write.");
        return;
      }
      for (uint8_t block = 0; block < 3; block++) {
        uint8_t blockNum = (uint8_t)(sector * 4 + block);
#if RFID_UID_CLONE
        if (magic && uidWritten && sector == 0 && block == 0) {
          continue;
        }
#endif
        bool auth =
            s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNum, 0, keyA);
        if (!auth) {
          auth = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNum, 1, keyB);
        }
        if (!auth) {
          auth = s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNum, 0, keyZero);
        }
        if (auth) {
          if (s_nfc.mifareclassic_WriteDataBlock(blockNum, s_srcData[sector][block])) {
            blocksDone++;
          } else {
            cloneOk = false;
          }
        } else {
          cloneOk = false;
        }
        delay(28);
      }
    }
  } else if (blankType == MIFARE_ULTRALIGHT || blankType == NTAG) {
    uint8_t maxPages = (blankType == MIFARE_ULTRALIGHT) ? 16u : 36u;
    rfidShellProgress("Clone", "Writing — Ultralight / NTAG", "Cancel");
    for (uint8_t page = 4; page < maxPages; page++) {
      char l2[40];
      snprintf(l2, sizeof(l2), "Page %u / %u", (unsigned)page, (unsigned)(maxPages - 1));
      rfidDynamicBand(RF_DYN_TOP_PROG, "Writing copied pages", l2);
      if (rfidPollCancel()) {
        rfidRestoreBus();
        rfidResultAndDismiss("Clone", "Stopped", "Clone aborted mid-write.");
        return;
      }
      if (s_nfc.mifareultralight_WritePage(page, s_srcPages[page])) {
        blocksDone++;
      } else {
        cloneOk = false;
      }
      delay(18);
    }
  }

  rfidRestoreBus();

  char summary[512];
  const char* fw = s_pn532VerStr[0] ? s_pn532VerStr : "PN532";
#if RFID_UID_CLONE
  snprintf(summary, sizeof(summary),
           "%s\n\n"
           "Source UID: %s\n"
           "Blank UID:    %s\n"
           "Tag type:     %s\n"
           "Reader IC:    %s\n\n"
           "Approx. write ops OK: %d\n"
           "Gen2 magic UID path: %s",
           cloneOk ? "Clone finished." : "Clone finished with some errors.",
           uidStr, blankUidStr, cardTypeStr(srcType), fw, blocksDone,
           gen2MagicDetected ? "detected (UID block attempted)" : "not detected");
#else
  snprintf(summary, sizeof(summary),
           "%s\n\n"
           "Source UID: %s\n"
           "Blank UID:    %s\n"
           "Tag type:     %s\n"
           "Reader IC:    %s\n\n"
           "Approx. write ops OK: %d",
           cloneOk ? "Clone finished." : "Clone finished with some errors.",
           uidStr, blankUidStr, cardTypeStr(srcType), fw, blocksDone);
#endif

  rfidResultAndDismiss("Clone", "Done", summary);
}

void sessionTagDisrupt() {
  if (!rfidRunTwoButtonDialog(
          "Tag Disrupt", "Authorized testing only",
          "Writes sector trailer blocks on MIFARE Classic (default key A).\n"
          "Bad access bits can brick a tag.\n\nOnly proceed on tags you own or are "
          "explicitly allowed to modify.",
          "I understand", FeatureUI::ButtonStyle::Danger)) {
    return;
  }

  rfidAttachBus();

  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  uint8_t maliciousData[16];
  uint8_t key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static const uint8_t sectorTrailers[] = {3,  7,  11, 15, 19, 23, 27, 31,
                                           35, 39, 43, 47, 51, 55, 59, 63};
  bool any = false;
  int trailersOk = 0;
  int authFail = 0;

  unsigned long t0 = millis();
  rfidShellListen("Tag Disrupt", "Cancel");
  for (;;) {
    rfidListenTick("Classic tag — hold on coil (~12 s window)", t0);
    if (rfidPollCancel()) {
      rfidRestoreBus();
      return;
    }
    if (s_nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 280)) {
      break;
    }
    if (millis() - t0 > 12000UL) {
      rfidRestoreBus();
      rfidResultAndDismiss("Tag Disrupt", "No tag", "No ISO14443A tag in the listen window.");
      return;
    }
    delay(40);
  }

  CardType ct = detectCardType(uid, uidLength);
  char uidStr[24] = "";
  uidToHex(uidStr, sizeof(uidStr), uid, uidLength);

  if (ct != MIFARE_CLASSIC) {
    rfidRestoreBus();
    char msg[120];
    snprintf(msg, sizeof(msg), "Tag types as %s.\nDisrupt targets Classic trailers only.",
             cardTypeStr(ct));
    rfidResultAndDismiss("Tag Disrupt", "Skipped", msg);
    return;
  }

  rfidShellProgress("Tag Disrupt", "Writing sector trailers", "Cancel");
  rfidSetDynamicPill("WRITE", UI_WARN);
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t block = sectorTrailers[i];
    char l2[48];
    snprintf(l2, sizeof(l2), "Trailer block %u — sector %u", (unsigned)block, (unsigned)i);
    rfidDynamicBand(RF_DYN_TOP_PROG, "Rotating trailer pattern", l2);
    if (rfidPollCancel()) {
      rfidRestoreBus();
      rfidResultAndDismiss("Tag Disrupt", "Stopped", "Aborted mid-write.");
      return;
    }

    if (i % 3 == 0) {
      memset(maliciousData, 0xFF, 16);
    } else if (i % 3 == 1) {
      memset(maliciousData, 0x00, 16);
    } else {
      for (uint8_t j = 0; j < 16; j++) {
        maliciousData[j] = (uint8_t)random(256);
      }
    }
    maliciousData[6] = 0x00;
    maliciousData[7] = 0x00;
    maliciousData[8] = 0x00;

    if (!s_nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, key)) {
      authFail++;
    } else if (s_nfc.mifareclassic_WriteDataBlock(block, maliciousData)) {
      any = true;
      trailersOk++;
    }
    delay(35);
  }

  rfidRestoreBus();

  char msg[320];
  const char* fw = s_pn532VerStr[0] ? s_pn532VerStr : "PN532";
  snprintf(msg, sizeof(msg),
           "Tag UID: %s\n"
           "Reader IC: %s\n\n"
           "Sector trailers touched: %d / 16\n"
           "Key A auth failures:    %d\n\n"
           "%s\n\n"
           "Patterns alternate FF / 00 / random on trailers.\n"
           "Access bits may brick the tag.",
           uidStr, fw, trailersOk, authFail,
           any ? "At least one trailer write succeeded." : "No trailer writes succeeded.");

  rfidResultAndDismiss("Tag Disrupt", any ? "Finished" : "No writes", msg);
}

} 

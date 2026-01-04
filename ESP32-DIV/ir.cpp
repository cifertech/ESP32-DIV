#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <SD.h>
#include <algorithm>
#include <vector>
#include "KeyboardUI.h"
#include "Touchscreen.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"


namespace IRRemoteFeature {

static constexpr uint16_t kRecvPin = IR_RX_PIN;
static constexpr uint16_t kSendPin = IR_TX_PIN;
static constexpr uint16_t kKhz     = IR_DEFAULT_KHZ;

static constexpr int16_t kToolbarY = 20;
static constexpr int16_t kToolbarH = 16;
static constexpr int16_t kIconSize = 16;

static constexpr int16_t kGraphYOffset = 170;

static constexpr uint16_t kMaxRawLen = 512;

static IRrecv s_recv(kRecvPin);
static IRsend s_send(kSendPin);
static decode_results s_results;

static bool s_hasCapture = false;

static uint16_t s_raw[kMaxRawLen];
static uint16_t s_rawLen = 0;

static decode_type_t s_decodeType = decode_type_t::UNKNOWN;
static uint64_t s_value = 0;
static uint16_t s_bits = 0;

static uint8_t s_repeat = 1;
static bool s_autoTx = false;
static uint32_t s_autoIntervalMs = 450;
static uint32_t s_lastAutoMs = 0;

static bool s_uiDrawn = false;
static bool s_contentDirty = true;
static uint32_t s_lastActionMs = 0;
static bool s_captureUiDirty = false;
static uint32_t s_lastCaptureUiMs = 0;
static uint32_t s_lastWaveHash = 0;
static uint32_t s_lastStableHash = 0;

static bool s_hasParsedKey = false;
static uint64_t s_keyAddr = 0;
static uint64_t s_keyCmd  = 0;
static String s_keyText   = "";

static uint8_t s_waveZoomIdx = 0;
static constexpr uint32_t kWaveZoomUs[] = {0, 50000, 20000, 10000};

static uint8_t  s_scope[240]{};
static uint16_t s_scopePos = 0;
static uint32_t s_lastScopeSampleUs = 0;
static uint32_t s_lastScopeDrawMs = 0;

static uint32_t waveformWindowUs(uint32_t totalUs) {
  const uint32_t w = kWaveZoomUs[s_waveZoomIdx % (sizeof(kWaveZoomUs)/sizeof(kWaveZoomUs[0]))];
  if (w == 0) return totalUs;
  return (totalUs < w) ? totalUs : w;
}

static const char* zoomLabel() {
  switch (s_waveZoomIdx % 4) {
    case 0: return "FIT";
    case 1: return "50ms";
    case 2: return "20ms";
    default: return "10ms";
  }
}

static void redrawGraphTitleOnly(bool captured) {
  const int y0 = kToolbarY + kToolbarH + 4;
  const int gx = 6;
  const int gy = y0 + kGraphYOffset;
  const int gw = 228;

  tft.fillRect(gx, gy - 12, gw, 12, FEATURE_BG);
  tft.setTextFont(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(gx, gy - 10);
  if (captured) {
    tft.print("IR waveform (");
    tft.print(zoomLabel());
    tft.print(")");
    tft.setTextColor(UI_LABLE, FEATURE_BG);
    tft.setCursor(gx + 140, gy - 10);
    tft.print("tap to zoom");
  } else {
    tft.print("IR scope (live)");
  }
}

static void redrawScopePlotOnly() {

  const int y0 = kToolbarY + kToolbarH + 4;
  const int gx = 6;
  const int gy = y0 + kGraphYOffset;
  const int gw = 228;
  const int gh = 86;

  const int px0 = gx + 1;
  const int py0 = gy + 1;
  const int pw  = gw - 2;
  const int ph  = gh - 2;

  const uint16_t colGrid  = UI_LABLE;
  const uint16_t colMark  = UI_OK;
  const uint16_t colSpace = UI_WARN;

  tft.startWrite();

  tft.fillRect(px0, py0, pw, ph, FEATURE_BG);

  for (int i = 1; i <= 3; i++) {
    int yy = py0 + (ph * i) / 4;
    tft.drawFastHLine(px0, yy, pw, colGrid);
  }

  const int highY = py0 + 8;
  const int lowY  = py0 + ph - 9;

  for (int x = 0; x < pw && x < 240; x++) {
    uint16_t idx = (uint16_t)((s_scopePos + x) % 240);
    bool levelHigh = (s_scope[idx] != 0);
    int y = levelHigh ? highY : lowY;
    tft.drawPixel(px0 + x, y, levelHigh ? colSpace : colMark);
    if (x > 0) {
      uint16_t pidx = (uint16_t)((s_scopePos + x - 1) % 240);
      bool prevHigh = (s_scope[pidx] != 0);
      int py = prevHigh ? highY : lowY;
      if (py != y) tft.drawFastVLine(px0 + x, highY, (lowY - highY + 1), colGrid);
    }
  }
  tft.endWrite();
}

static void redrawWaveformPlotOnly() {

  const int y0 = kToolbarY + kToolbarH + 4;
  const int gx = 6;
  const int gy = y0 + kGraphYOffset;
  const int gw = 228;
  const int gh = 86;

  const int px0 = gx + 1;
  const int py0 = gy + 1;
  const int pw  = gw - 2;
  const int ph  = gh - 2;

  const uint16_t colGridMajor = UI_LINE;
  const uint16_t colGridMinor = UI_LABLE;
  const uint16_t colMark      = UI_OK;
  const uint16_t colSpace     = UI_WARN;
  const uint16_t colText      = UI_LABLE;

  tft.fillRect(px0, py0, pw, ph, FEATURE_BG);

  const int labelY = py0 + ph - 10;

  for (int i = 1; i <= 3; i++) {
    int yy = py0 + (ph * i) / 4;
    tft.drawFastHLine(px0, yy, pw, colGridMinor);
  }

  const int highY = py0 + 10;
  const int lowY  = py0 + ph - 18;

  uint32_t totalUs = 0;
  for (uint16_t i = 0; i < s_rawLen; i++) totalUs += s_raw[i];
  if (totalUs == 0 || s_rawLen == 0) return;

  const uint32_t windowUs = waveformWindowUs(totalUs);
  if (windowUs == 0) return;

  auto tickPxFor = [&](uint32_t tickUs) -> int {
    if (tickUs == 0) return 9999;
    return (int)((tickUs * (uint32_t)pw) / windowUs);
  };

  uint32_t tickUs = 2000;
  if (windowUs > 80000) tickUs = 10000;
  else if (windowUs > 30000) tickUs = 5000;
  else if (windowUs > 15000) tickUs = 5000;

  while (tickPxFor(tickUs) < 10 && tickUs < 50000) tickUs *= 2;

  uint32_t majorEvery = 1;
  while (tickPxFor((uint32_t)(tickUs * majorEvery)) < 36 && majorEvery < 16) majorEvery *= 2;

  tft.setTextFont(1);
  tft.setTextColor(colText, FEATURE_BG);
  int lastLabelRight = -10000;

  for (uint32_t t = 0, n = 0; t <= windowUs; t += tickUs, n++) {
    int x = px0 + (int)((t * (uint32_t)pw) / windowUs);
    if (x < px0 || x >= (px0 + pw)) continue;

    const bool isMajor = (n % majorEvery) == 0;
    tft.drawFastVLine(x, py0, ph, (t == 0) ? colGridMajor : (isMajor ? colGridMajor : colGridMinor));

    if (!isMajor) continue;

    uint32_t ms = t / 1000;
    char buf[10];
    snprintf(buf, sizeof(buf), "%lums", (unsigned long)ms);
    int tw = tft.textWidth(buf, 1);
    int lx = x - (tw / 2);
    if (lx < px0) lx = px0;
    if (lx + tw > (px0 + pw)) lx = (px0 + pw) - tw;

    if (lx <= lastLabelRight) continue;
    tft.setCursor(lx, labelY);
    tft.print(buf);
    lastLabelRight = lx + tw + 2;
  }

  bool isMark = true;
  uint32_t tUs = 0;
  int prevX = px0;

  for (uint16_t i = 0; i < s_rawLen; i++) {
    if (tUs >= windowUs) break;
    uint32_t dur = s_raw[i];
    uint32_t durClip = dur;
    if (tUs + durClip > windowUs) durClip = windowUs - tUs;

    uint32_t t2 = tUs + durClip;
    int x1 = px0 + (int)((tUs * (uint32_t)pw) / windowUs);
    int x2 = px0 + (int)((t2 * (uint32_t)pw) / windowUs);
    if (x1 < px0) x1 = px0;
    if (x2 > px0 + pw) x2 = px0 + pw;

    const int y = isMark ? highY : lowY;
    const uint16_t c = isMark ? colMark : colSpace;

    if (x2 > x1) {
      tft.drawFastHLine(x1, y, x2 - x1, c);
      tft.drawFastHLine(x1, y + (isMark ? 1 : -1), x2 - x1, c);
    } else {

      tft.drawPixel(x1, y, c);
    }

    if (x2 != prevX) {
      tft.drawFastVLine(x2, highY, (lowY - highY + 1), colGridMinor);
    }

    prevX = x2;
    tUs = t2;
    isMark = !isMark;
  }
}

static void redrawCapturedDetailsOnly() {

  const int y0 = kToolbarY + kToolbarH + 4;

  tft.startWrite();
  tft.fillRect(0, y0 + 56, 240, 86, FEATURE_BG);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(10, y0 + 60);
  tft.println("Captured");

  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(10, y0 + 78);
  tft.print("Type: "); tft.println(typeToString(s_decodeType));
  tft.setCursor(10, y0 + 92);
  tft.printf("Bits: %u", (unsigned)s_bits);
  tft.setCursor(10, y0 + 106);

  tft.print("Key: ");
  if (s_keyText.length() > 28) tft.println(s_keyText.substring(0, 28));
  else tft.println(s_keyText);
  tft.setCursor(10, y0 + 120);
  tft.printf("Raw len: %u", (unsigned)s_rawLen);

  tft.setCursor(10, y0 + 140);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.println("UP:TX  DOWN:AUTO  LEFT/RIGHT:Repeat");

  redrawGraphTitleOnly(true);
  redrawWaveformPlotOnly();
  tft.endWrite();
}

static const char* kKeyboardRows[] = {
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM<-",
  " 0123456789"
};

static const char* kRandomNames[] = {
  "TV", "AC", "Lamp", "Fan", "Receiver", "Projector", "Power", "Mute", "VolUp", "VolDn"
};

static uint32_t hashWaveform() {

  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) {
    h ^= v;
    h *= 16777619u;
  };
  mix((uint32_t)s_rawLen);
  mix((uint32_t)s_decodeType);
  mix((uint32_t)s_bits);
  mix((uint32_t)s_value);
  mix((uint32_t)(s_value >> 32));
  const uint16_t n = (s_rawLen < 32) ? s_rawLen : 32;
  for (uint16_t i = 0; i < n; i++) mix((uint32_t)s_raw[i]);
  return h;
}

static String getUserInputName() {
  OnScreenKeyboardConfig cfg;
  cfg.titleLine1      = "Name IR capture";
  cfg.titleLine2      = "15 chars max";
  cfg.rows            = kKeyboardRows;
  cfg.rowCount        = 4;
  cfg.maxLen          = 15;
  cfg.shuffleNames    = kRandomNames;
  cfg.shuffleCount    = (uint8_t)(sizeof(kRandomNames) / sizeof(kRandomNames[0]));
  cfg.buttonsY        = 195;
  cfg.backLabel       = "Back";
  cfg.middleLabel     = "Shuffle";
  cfg.okLabel         = "OK";
  cfg.enableShuffle   = true;
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg   = "Name cannot be empty!";

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");
  if (!r.accepted) {
    tft.fillScreen(FEATURE_BG);
  }
  return r.text;
}

namespace {
  static constexpr const char* IR_DIR = "/ir";
  static constexpr const char* IR_FILE_PREFIX = "/ir/ir_";
  static constexpr uint32_t IR_MAGIC = 0x31525249;

  struct __attribute__((packed)) IrCaptureHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t khz;
    uint16_t rawLen;
    uint8_t  decodeType;
    uint8_t  reserved;
    uint16_t bits;
    uint64_t value;
    char     name[16];
  };

  static bool irEnsureDir(const char* dirPath) {
    if (SD.exists(dirPath)) return true;
    if (SD.mkdir(dirPath)) return true;
    if (dirPath && dirPath[0] == '/') return SD.mkdir(dirPath + 1);
    return false;
  }

  static bool makeNextIrPath(String& outPath) {
    char buf[32];
    for (uint16_t i = 0; i < 10000; i++) {
      snprintf(buf, sizeof(buf), "%s%04u.bin", IR_FILE_PREFIX, (unsigned)i);
      if (!SD.exists(buf)) { outPath = String(buf); return true; }
    }
    return false;
  }
}

static void updateDisplay() {
  const int y0 = kToolbarY + kToolbarH + 4;
  tft.fillRect(0, y0, 240, 320 - y0, FEATURE_BG);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);

  tft.drawBitmap(10, y0 + 4, bitmap_icon_signals, 16, 16, UI_ICON);
  tft.setCursor(30, y0 + 4);
  tft.print("IR Replay");
  if (s_autoTx) {
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.setCursor(190, y0 + 4);
    tft.print("AUTO");
    tft.setTextColor(UI_TEXT, FEATURE_BG);
  }

  tft.setTextFont(1);
  tft.setCursor(10, y0 + 24);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.print("RX ");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.printf("GPIO%u  ", (unsigned)kRecvPin);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.print("TX ");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.printf("GPIO%u  %ukHz", (unsigned)kSendPin, (unsigned)kKhz);

  tft.setCursor(10, y0 + 38);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.print("Repeat ");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.printf("%u", (unsigned)s_repeat);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.print("   Auto ");
  tft.setTextColor(s_autoTx ? UI_WARN : UI_LABLE, FEATURE_BG);
  tft.print(s_autoTx ? "ON" : "OFF");

  if (!s_hasCapture) {
    tft.setTextFont(2);
    tft.setCursor(10, y0 + 60);
    tft.setTextColor(UI_ICON, FEATURE_BG);
    tft.println("Waiting for IR");
    tft.setTextFont(1);
    tft.setCursor(10, y0 + 80);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.println("Press a remote button to capture.");
    tft.setCursor(10, y0 + 94);
    tft.setTextColor(UI_LABLE, FEATURE_BG);
    tft.println("Back: SELECT or toolbar icon");
  } else {
    tft.setTextFont(2);
    tft.setCursor(10, y0 + 60);
    tft.setTextColor(UI_ICON, FEATURE_BG);
    tft.println("Captured");

    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.setCursor(10, y0 + 78);
    tft.print("Type: "); tft.println(typeToString(s_decodeType));
    tft.setCursor(10, y0 + 92);
    tft.printf("Bits: %u", (unsigned)s_bits);
    tft.setCursor(10, y0 + 106);
    tft.print("Key: ");
    if (s_keyText.length() > 28) tft.println(s_keyText.substring(0, 28));
    else tft.println(s_keyText);
    tft.setCursor(10, y0 + 120);
    tft.printf("Raw len: %u", (unsigned)s_rawLen);

    tft.setCursor(10, y0 + 140);
    tft.setTextColor(UI_LABLE, FEATURE_BG);
    tft.println("UP:TX  DOWN:AUTO  LEFT/RIGHT:Repeat");
  }

  const int gx = 6;
  const int gy = y0 + kGraphYOffset;
  const int gw = 228;
  const int gh = 86;

  const int px0 = gx + 1;
  const int py0 = gy + 1;
  const int pw  = gw - 2;
  const int ph  = gh - 2;

  const uint16_t colFrame = UI_LINE;
  const uint16_t colGrid  = UI_LABLE;
  const uint16_t colMark  = UI_OK;
  const uint16_t colSpace = UI_WARN;
  const uint16_t colText  = UI_ICON;

  tft.setTextFont(1);
  tft.setTextColor(colText, FEATURE_BG);
  tft.setCursor(gx, gy - 10);
  if (s_hasCapture) {
    tft.print("IR waveform (");
    tft.print(zoomLabel());
    tft.print(")");
    tft.setTextColor(UI_LABLE, FEATURE_BG);
    tft.setCursor(gx + 140, gy - 10);
    tft.print("tap to zoom");
  } else {
    tft.print("IR scope (live)");
  }

  tft.drawRect(gx, gy, gw, gh, colFrame);
  tft.fillRect(px0, py0, pw, ph, FEATURE_BG);

  for (int i = 1; i <= 3; i++) {
    int yy = py0 + (ph * i) / 4;
    tft.drawFastHLine(px0, yy, pw, colGrid);
  }

  if (!s_hasCapture) {

    const int highY = py0 + 8;
    const int lowY  = py0 + ph - 9;

    for (int x = 0; x < pw && x < 240; x++) {
      uint16_t idx = (uint16_t)((s_scopePos + x) % 240);
      bool levelHigh = (s_scope[idx] != 0);
      int y = levelHigh ? highY : lowY;

      tft.drawPixel(px0 + x, y, levelHigh ? colSpace : colMark);

      if (x > 0) {
        uint16_t pidx = (uint16_t)((s_scopePos + x - 1) % 240);
        bool prevHigh = (s_scope[pidx] != 0);
        int py = prevHigh ? highY : lowY;
        if (py != y) {
          tft.drawFastVLine(px0 + x, highY, (lowY - highY + 1), colGrid);
        }
      }
    }

    tft.setTextColor(UI_LABLE, FEATURE_BG);
    tft.setCursor(gx + 118, gy - 10);
    tft.print("press remote to see bursts");
    return;
  }

  redrawWaveformPlotOnly();
}

static void tryReplay();
static void saveCapture();

static void runUI() {

  static const int ICON_NUM = 6;
  static int iconX[ICON_NUM] = {90, 130, 170, 210, 50, 10};
  static int iconY = kToolbarY;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_sort_up_plus,
    bitmap_icon_sort_down_minus,
    bitmap_icon_antenna,
    bitmap_icon_floppy,
    bitmap_icon_random,
    bitmap_icon_go_back
  };

  if (!s_uiDrawn) {

    tft.fillRect(0, kToolbarY, 240, kToolbarH, UI_FG);
    tft.drawLine(0, kToolbarY, 240, kToolbarY, UI_LINE);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i]) tft.drawBitmap(iconX[i], iconY, icons[i], kIconSize, kIconSize, UI_ICON);
    }
    tft.drawLine(0, kToolbarY + kToolbarH, 240, kToolbarY + kToolbarH, UI_LINE);
    s_uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], kIconSize, kIconSize, UI_ICON);
      animationState = 2;

      switch (activeIcon) {
        case 0:
          s_autoTx = false;
          if (s_repeat < 10) s_repeat++;
          s_contentDirty = true;
          break;
        case 1:
          s_autoTx = false;
          if (s_repeat > 1) s_repeat--;
          s_contentDirty = true;
          break;
        case 2:
          s_autoTx = false;
          if (s_hasCapture) tryReplay();
          break;
        case 3:
          saveCapture();
          break;
        case 4:
          if (s_hasCapture) {
            s_autoTx = !s_autoTx;
            s_lastAutoMs = 0;
            s_contentDirty = true;
          }
          break;
        case 5:
          feature_exit_requested = true;
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > kToolbarY && y < (kToolbarY + kToolbarH)) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < (iconX[i] + kIconSize)) {
            if (icons[i] && animationState == 0) {

              if (i == 5) {
                feature_exit_requested = true;
              } else {

                tft.drawBitmap(iconX[i], iconY, icons[i], kIconSize, kIconSize, FEATURE_BG);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
              }
            }
            break;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

static void tryReplay() {
  if (!s_hasCapture || s_rawLen == 0) return;

  s_recv.disableIRIn();

  for (uint8_t i = 0; i < s_repeat; i++) {
    s_send.sendRaw(s_raw, s_rawLen, kKhz);
    delay(35);
  }

  delay(50);
  s_recv.enableIRIn();
  s_lastActionMs = millis();
}

static void saveCapture() {
  if (!s_hasCapture || s_rawLen == 0) {
    showNotification("IR", "No capture to save yet.");
    return;
  }
  if (!isSDCardAvailable()) {
    showNotification("IR", "SD not available.");
    return;
  }
  if (!irEnsureDir(IR_DIR)) {
    showNotification("IR", "Cannot create /ir");
    return;
  }

  String name = getUserInputName();
  if (name.length() == 0) {

    s_uiDrawn = false;
    s_contentDirty = true;
    return;
  }

  String path;
  if (!makeNextIrPath(path)) {
    showNotification("IR", "Too many files in /ir");
    return;
  }

  IrCaptureHeader h{};
  h.magic = IR_MAGIC;
  h.version = 1;
  h.khz = kKhz;
  h.rawLen = s_rawLen;
  h.decodeType = (uint8_t)s_decodeType;
  h.reserved = 0;
  h.bits = s_bits;
  h.value = s_value;
  memset(h.name, 0, sizeof(h.name));
  strncpy(h.name, name.c_str(), sizeof(h.name) - 1);

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    showNotification("IR", "Open file failed");
    return;
  }
  bool ok = true;
  ok = ok && (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
  ok = ok && (f.write((const uint8_t*)s_raw, (size_t)s_rawLen * sizeof(uint16_t)) == (size_t)s_rawLen * sizeof(uint16_t));
  f.close();

  if (!ok) {
    showNotification("IR", "Write failed");
    return;
  }

  tft.fillRect(0, 40, 240, 60, FEATURE_BG);
  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(10, 44);
  tft.println("Saved!");
  tft.setTextFont(1);
  tft.setCursor(10, 62);
  tft.println(name);
  delay(800);

  s_uiDrawn = false;
  s_contentDirty = true;
}

static bool extractHexAfterLabel(const String& s, const char* label, uint64_t& out) {
  int li = s.indexOf(label);
  if (li < 0) return false;
  int ox = s.indexOf("0x", li);
  if (ox < 0) return false;
  int i = ox + 2;
  uint64_t v = 0;
  bool any = false;
  while (i < (int)s.length()) {
    char c = s[i];
    uint8_t d;
    if (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint8_t)(10 + c - 'a');
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(10 + c - 'A');
    else break;
    any = true;
    v = (v << 4) | d;
    i++;
  }
  if (!any) return false;
  out = v;
  return true;
}

static uint32_t stableHash() {

  if (!s_hasParsedKey) return hashWaveform();
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t v) { h ^= v; h *= 16777619u; };
  mix((uint32_t)s_decodeType);
  mix((uint32_t)s_bits);
  mix((uint32_t)s_keyAddr);
  mix((uint32_t)(s_keyAddr >> 32));
  mix((uint32_t)s_keyCmd);
  mix((uint32_t)(s_keyCmd >> 32));
  return h;
}

static void pollCapture() {
  if (s_recv.decode(&s_results)) {

    const uint16_t rawlen = s_results.rawlen;
    const uint16_t want = (rawlen > 1) ? (rawlen - 1) : 0;

    if (want == 0 || want > kMaxRawLen) {

      s_recv.resume();
      return;
    }

    const bool hadCapture = s_hasCapture;

    const bool allOnes =
        (s_results.value == 0xFFFFFFFFULL) ||
        (s_results.value == 0xFFFFULL) ||
        (s_results.value == 0xFFFFFFFFFFFFFFFFULL);
    const bool shortFrame = (want > 0 && want < 12);
    const bool looksLikeRepeat =
        hadCapture && ((shortFrame) || (allOnes && s_results.decode_type == s_decodeType));
    if (looksLikeRepeat) {
      s_recv.resume();
      return;
    }

    for (uint16_t i = 1; i < rawlen; i++) {

      s_raw[i - 1] = s_results.rawbuf[i] * kRawTick;
    }
    s_rawLen = want;

    s_decodeType = s_results.decode_type;
    s_value = s_results.value;
    s_bits = s_results.bits;

    s_hasParsedKey = false;
    s_keyAddr = 0;
    s_keyCmd = 0;
    s_keyText = "";
    {
      String human = resultToHumanReadableBasic(&s_results);
      uint64_t addr = 0, cmd = 0;
      const bool hasAddr = extractHexAfterLabel(human, "Address", addr) || extractHexAfterLabel(human, "Addr", addr);
      const bool hasCmd  = extractHexAfterLabel(human, "Command", cmd)  || extractHexAfterLabel(human, "Cmd", cmd);
      if (hasAddr && hasCmd) {
        s_hasParsedKey = true;
        s_keyAddr = addr;
        s_keyCmd  = cmd;
        s_keyText = String(typeToString(s_decodeType)) + " A:0x" + uint64ToString(addr, 16) + " C:0x" + uint64ToString(cmd, 16);
      } else {

        s_keyText = String(typeToString(s_decodeType)) + " V:0x" + uint64ToString(s_value, 16);
      }
    }

    s_hasCapture = true;

    const uint32_t newHash = stableHash();

    if (!hadCapture) {
      s_contentDirty = true;
      s_lastWaveHash = newHash;
      s_lastStableHash = newHash;
    } else {
      if (newHash != s_lastStableHash) {
        s_lastStableHash = newHash;
        const uint32_t now = millis();
        if (s_lastCaptureUiMs == 0 || (now - s_lastCaptureUiMs) >= 150) {
          s_captureUiDirty = true;
          s_lastCaptureUiMs = now;
        }
      }
    }

    s_recv.resume();
  }
}

void setup() {
  s_repeat = 1;
  s_autoTx = false;
  s_lastAutoMs = 0;
  s_uiDrawn = false;
  s_contentDirty = true;
  s_lastActionMs = 0;

  pinMode(kRecvPin, INPUT_PULLUP);
  pinMode(kSendPin, OUTPUT);
  s_send.begin();
  s_recv.enableIRIn();

  s_hasCapture = false;
  s_rawLen = 0;
  s_decodeType = decode_type_t::UNKNOWN;
  s_value = 0;
  s_bits = 0;

  tft.fillScreen(FEATURE_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  runUI();
  updateDisplay();
  s_contentDirty = false;
}

void loop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  runUI();

  pollCapture();

  if (s_hasCapture && ts.touched()) {
    int x, y;
    if (readTouchXY(x, y)) {
      const uint32_t now = millis();
      if (now - s_lastActionMs > 220) {
        const int y0 = kToolbarY + kToolbarH + 4;
        const int gx = 6;
        const int gy = y0 + kGraphYOffset;
        const int gw = 228;
        const int gh = 86;
        if (x >= gx && x <= (gx + gw) && y >= (gy - 12) && y <= (gy + gh)) {
          s_waveZoomIdx = (uint8_t)((s_waveZoomIdx + 1) % 4);
          redrawGraphTitleOnly(true);
          redrawWaveformPlotOnly();
          s_lastActionMs = now;
        }
      }
    }
  }

  if (s_captureUiDirty && s_hasCapture) {
    redrawCapturedDetailsOnly();
    s_captureUiDirty = false;
  }

  if (!s_hasCapture) {
    uint32_t nowUs = micros();
    if (s_lastScopeSampleUs == 0) s_lastScopeSampleUs = nowUs;

    uint8_t steps = 0;
    while ((uint32_t)(nowUs - s_lastScopeSampleUs) >= 1000 && steps < 8) {
      s_lastScopeSampleUs += 1000;

      s_scope[s_scopePos] = (uint8_t)(digitalRead(kRecvPin) ? 1 : 0);
      s_scopePos = (uint16_t)((s_scopePos + 1) % 240);
      steps++;
    }

    uint32_t nowMs = millis();
    if (s_lastScopeDrawMs == 0) s_lastScopeDrawMs = nowMs;
    if ((uint32_t)(nowMs - s_lastScopeDrawMs) >= 80) {

      redrawScopePlotOnly();
      s_lastScopeDrawMs = nowMs;
    }
  }

  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 200;
  static bool prevLeft=false, prevRight=false, prevUp=false, prevDown=false;
  const bool leftPressed  = isButtonPressed(BTN_LEFT);
  const bool rightPressed = isButtonPressed(BTN_RIGHT);
  const bool upPressed    = isButtonPressed(BTN_UP);
  const bool downPressed  = isButtonPressed(BTN_DOWN);

  if (rightPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    if (s_repeat < 10) s_repeat++;
    s_contentDirty = true;
    lastDebounceTime = millis();
  }
  if (leftPressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    if (s_repeat > 1) s_repeat--;
    s_contentDirty = true;
    lastDebounceTime = millis();
  }
  if (upPressed && !prevUp && s_hasCapture && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    tryReplay();
    lastDebounceTime = millis();
  }
  if (downPressed && !prevDown && s_hasCapture && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = !s_autoTx;
    s_lastAutoMs = 0;
    s_contentDirty = true;
    lastDebounceTime = millis();
  }

  prevLeft = leftPressed;
  prevRight = rightPressed;
  prevUp = upPressed;
  prevDown = downPressed;

  if (s_autoTx && s_hasCapture) {
    uint32_t now = millis();
    if (s_lastAutoMs == 0 || (now - s_lastAutoMs) >= s_autoIntervalMs) {
      tryReplay();
      s_lastAutoMs = now;
    }
  }

  if (s_contentDirty) {
    currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    runUI();
    updateDisplay();
    s_contentDirty = false;
  }

  delay(10);
}

}

namespace IRSavedProfile {

static constexpr const char* IR_DIR = "/ir";
static constexpr uint32_t IR_MAGIC = 0x31525249;

struct __attribute__((packed)) IrCaptureHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t khz;
  uint16_t rawLen;
  uint8_t  decodeType;
  uint8_t  reserved;
  uint16_t bits;
  uint64_t value;
  char     name[16];
};

static bool uiDrawn = false;
static int yshift = 16;

static constexpr uint8_t ITEMS_PER_PAGE = 7;
static constexpr int LIST_X = 6;
static constexpr int LIST_W = 228;
static constexpr int LIST_Y = 64;
static constexpr int ROW_H  = 18;

static constexpr int BOT_H = 32;
static constexpr int BOT_Y = 320 - BOT_H;
static constexpr int UI_GAP_Y = 6;
static constexpr int DETAILS_H = (BOT_Y - (LIST_Y + (ITEMS_PER_PAGE * ROW_H)) - (2 * UI_GAP_Y));
static constexpr int DETAILS_Y = BOT_Y - UI_GAP_Y - DETAILS_H;

static constexpr int BOT_GAP = 8;
static constexpr int BOT_BTN_W = (240 - 10 - 10 - BOT_GAP) / 2;
static constexpr int BOT_BTN_H = 24;
static constexpr int BOT_BTN_Y = BOT_Y + 4;
static constexpr int BOT_TX_X  = 10;
static constexpr int BOT_DEL_X = BOT_TX_X + BOT_BTN_W + BOT_GAP;

static std::vector<String> irFiles;
static uint16_t irTotal = 0;
static uint16_t currentIndex = 0;
static String sdLastErr = "";

static String selectedPath = "";
static IrCaptureHeader selectedHeader{};
static bool selectedValid = false;

static uint16_t cachedPageStart = 0xFFFF;
static IrCaptureHeader cachedPage[ITEMS_PER_PAGE]{};
static bool cachedOk[ITEMS_PER_PAGE]{};
static bool cacheDirty = true;

static bool deleteArmed = false;
static uint32_t deleteArmUntilMs = 0;

static constexpr uint16_t kMaxRawLen2 = 512;
static uint16_t txRaw[kMaxRawLen2]{};

static IRsend irsend(IR_TX_PIN);

static bool endsWith(const String& s, const char* suf) {
  int sl = s.length();
  int tl = (int)strlen(suf);
  if (tl > sl) return false;
  return s.substring(sl - tl) == suf;
}

static String baseName(const String& path) {
  int slash = path.lastIndexOf('/');
  return (slash >= 0) ? path.substring(slash + 1) : path;
}

static bool readHeader(const String& path, IrCaptureHeader& out, String* errOut = nullptr) {
  File f = SD.open(path, FILE_READ);
  if (!f) { if (errOut) *errOut = "Open failed"; return false; }
  if (f.read((uint8_t*)&out, sizeof(out)) != (int)sizeof(out)) { f.close(); if (errOut) *errOut="Read hdr failed"; return false; }
  f.close();
  if (out.magic != IR_MAGIC || out.version != 1) { if (errOut) *errOut="Bad file"; return false; }
  out.name[15] = '\0';
  if (out.rawLen == 0 || out.rawLen > kMaxRawLen2) { if (errOut) *errOut="Bad rawLen"; return false; }
  if (out.khz == 0 || out.khz > 100) { if (errOut) *errOut="Bad khz"; return false; }
  return true;
}

static bool readRaw(const String& path, const IrCaptureHeader& h, uint16_t* outRaw, String* errOut = nullptr) {
  File f = SD.open(path, FILE_READ);
  if (!f) { if (errOut) *errOut = "Open failed"; return false; }

  if (!f.seek(sizeof(IrCaptureHeader))) { f.close(); if (errOut) *errOut="Seek failed"; return false; }
  const size_t want = (size_t)h.rawLen * sizeof(uint16_t);
  if (f.read((uint8_t*)outRaw, want) != (int)want) { f.close(); if (errOut) *errOut="Read raw failed"; return false; }
  f.close();
  return true;
}

static void drawBottomButtons() {
  tft.fillRect(0, BOT_Y, 240, BOT_H, FEATURE_BG);
  FeatureUI::drawButtonRect(BOT_TX_X, BOT_BTN_Y, BOT_BTN_W, BOT_BTN_H,
                            "Transmit", FeatureUI::ButtonStyle::Primary);

  const bool armed = deleteArmed && (int32_t)(millis() - deleteArmUntilMs) < 0;
  const char* delLabel = armed ? "Delete?" : "Delete";
  FeatureUI::drawButtonRect(BOT_DEL_X, BOT_BTN_Y, BOT_BTN_W, BOT_BTN_H,
                            delLabel, FeatureUI::ButtonStyle::Danger);
}

static uint16_t pageStartForIndex(uint16_t idx) {
  return (uint16_t)((idx / ITEMS_PER_PAGE) * ITEMS_PER_PAGE);
}

static void refreshSdIndex(bool keepSelection = true) {
  String oldSel = keepSelection && irTotal > 0 ? irFiles[currentIndex] : String("");

  irFiles.clear();
  irTotal = 0;
  sdLastErr = "";

  if (!isSDCardAvailable()) {
    sdLastErr = "SD not available";
    currentIndex = 0;
    selectedValid = false;
    cacheDirty = true;
    return;
  }

  if (!SD.exists(IR_DIR)) {
    sdLastErr = "No /ir";
    currentIndex = 0;
    selectedValid = false;
    cacheDirty = true;
    return;
  }

  File d = SD.open(IR_DIR);
  if (!d) {
    sdLastErr = "Open /ir failed";
    currentIndex = 0;
    selectedValid = false;
    cacheDirty = true;
    return;
  }

  for (;;) {
    File f = d.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String name = String(f.name());

      if (endsWith(name, ".bin") && (name.startsWith("ir_") || name.startsWith("/ir/ir_") || name.startsWith(String(IR_DIR) + "/ir_"))) {
        String full = name.startsWith("/") ? name : (String(IR_DIR) + "/" + name);
        irFiles.push_back(full);
      }
    }
    f.close();
  }
  d.close();

  if (irFiles.empty()) {
    sdLastErr = "No IR captures";
    currentIndex = 0;
    selectedValid = false;
    cacheDirty = true;
    return;
  }

  std::sort(irFiles.begin(), irFiles.end(), [](const String& a, const String& b) {
    return baseName(a) < baseName(b);
  });

  irTotal = (uint16_t)irFiles.size();

  if (keepSelection && oldSel.length()) {
    auto it = std::find(irFiles.begin(), irFiles.end(), oldSel);
    if (it != irFiles.end()) currentIndex = (uint16_t)std::distance(irFiles.begin(), it);
  }
  if (currentIndex >= irTotal) currentIndex = (uint16_t)(irTotal - 1);

  selectedValid = false;
  cacheDirty = true;
}

static bool loadSelected(String* errOut = nullptr) {
  if (irTotal == 0) { selectedValid = false; return false; }
  selectedPath = irFiles[currentIndex];
  IrCaptureHeader h{};
  if (!readHeader(selectedPath, h, errOut)) { selectedValid = false; return false; }
  selectedHeader = h;
  selectedValid = true;
  return true;
}

static void ensurePageCache() {
  if (irTotal == 0) return;
  uint16_t start = pageStartForIndex(currentIndex);
  if (!cacheDirty && cachedPageStart == start) return;
  cachedPageStart = start;
  for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
    cachedOk[i] = false;
    uint16_t idx = (uint16_t)(start + i);
    if (idx >= irTotal) continue;
    String err;
    cachedOk[i] = readHeader(irFiles[idx], cachedPage[i], &err);
    if (!cachedOk[i]) memset(&cachedPage[i], 0, sizeof(IrCaptureHeader));
  }
  cacheDirty = false;
}

static void drawHeaderLine() {
  int hy = 30 + yshift;
  tft.fillRect(0, hy, 240, 14, FEATURE_BG);
  tft.setCursor(10, hy);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.printf("Profile %d/%d", (int)currentIndex + 1, (int)irTotal);
}

static void drawRow(uint16_t pageStart, uint8_t row) {
  uint16_t idx = (uint16_t)(pageStart + row);
  if (idx >= irTotal) return;
  bool isSel = (idx == currentIndex);
  int y = LIST_Y + (row * ROW_H);

  uint16_t bg = isSel ? UI_FG : FEATURE_BG;
  uint16_t fg = isSel ? UI_ICON : UI_TEXT;
  tft.fillRect(LIST_X, y, LIST_W, ROW_H - 1, bg);
  tft.setTextColor(fg, bg);
  tft.setCursor(LIST_X + 2, y + 4);
  tft.printf("%2d.", (int)idx + 1);
  tft.setCursor(LIST_X + 34, y + 4);

  if (cachedOk[row]) {
    char nameBuf[17];
    memcpy(nameBuf, cachedPage[row].name, 16);
    nameBuf[16] = '\0';
    String nm = String(nameBuf);
    if (nm.length() == 0) nm = baseName(irFiles[idx]);
    if (nm.length() > 10) nm = nm.substring(0, 10);
    tft.print(nm);

    decode_type_t dt = (decode_type_t)cachedPage[row].decodeType;
    String ty = String(typeToString(dt));
    if (ty.length() > 6) ty = ty.substring(0, 6);
    int tw = tft.textWidth(ty, 1);
    tft.setCursor(LIST_X + LIST_W - 4 - tw, y + 4);
    tft.print(ty);
  } else {
    tft.print("<?>");
  }
}

static void drawListPage(uint16_t pageStart) {
  ensurePageCache();
  tft.fillRect(LIST_X, LIST_Y, LIST_W, (ITEMS_PER_PAGE * ROW_H), FEATURE_BG);
  for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
    if ((uint16_t)(pageStart + row) >= irTotal) break;
    drawRow(pageStart, row);
  }
}

static void drawDetails() {
  tft.fillRect(0, DETAILS_Y, 240, (DETAILS_H + UI_GAP_Y), FEATURE_BG);

  String err;
  if (!selectedValid) loadSelected(&err);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(10, DETAILS_Y);
  if (!selectedValid) {
    tft.print("Read failed: ");
    tft.print(err);
    drawBottomButtons();
    return;
  }

  tft.print("Name: ");
  tft.print(selectedHeader.name);

  decode_type_t dt = (decode_type_t)selectedHeader.decodeType;
  tft.setCursor(10, DETAILS_Y + 14);
  tft.print("Type: ");
  tft.print(String(typeToString(dt)));
  tft.print("  ");
  tft.printf("%ukHz", (unsigned)selectedHeader.khz);

  tft.setCursor(10, DETAILS_Y + 28);
  tft.printf("Bits:%u  Raw:%u", (unsigned)selectedHeader.bits, (unsigned)selectedHeader.rawLen);

  tft.setCursor(10, DETAILS_Y + 42);
  tft.print("Val: 0x");
  tft.print(uint64ToString(selectedHeader.value, 16));

  tft.setCursor(10, DETAILS_Y + 56);
  tft.setTextColor(UI_LABLE, FEATURE_BG);
  tft.print("SRC: ");
  tft.print(baseName(selectedPath));

  if (deleteArmed && (int32_t)(millis() - deleteArmUntilMs) < 0) {
    int hintY = DETAILS_Y + 70;
    if (hintY >= BOT_Y) hintY = BOT_Y - 12;
    tft.setCursor(10, hintY);
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.print("Press delete again to confirm");
  }

  drawBottomButtons();
}

static void updateSelectionUI(uint16_t oldIndex, bool forceListRedraw = false) {
  if (irTotal == 0) return;
  uint16_t oldPage = pageStartForIndex(oldIndex);
  uint16_t newPage = pageStartForIndex(currentIndex);

  tft.startWrite();
  drawHeaderLine();
  if (forceListRedraw || oldPage != newPage) {
    drawListPage(newPage);
  } else {

    uint16_t pageStart = newPage;
    uint8_t oldRow = (uint8_t)(oldIndex - pageStart);
    uint8_t newRow = (uint8_t)(currentIndex - pageStart);
    ensurePageCache();
    if (oldRow < ITEMS_PER_PAGE) drawRow(pageStart, oldRow);
    if (newRow < ITEMS_PER_PAGE) drawRow(pageStart, newRow);
  }
  drawDetails();
  tft.endWrite();
}

static void updateDisplay() {
  tft.fillScreen(FEATURE_BG);
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  uiDrawn = false;

  drawHeaderLine();
  if (irTotal == 0) {
    tft.setCursor(10, 50 + yshift);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.print(sdLastErr.length() ? sdLastErr : "No profiles on SD.");
    drawBottomButtons();
    return;
  }
  drawListPage(pageStartForIndex(currentIndex));
  drawDetails();
}

static void transmitProfile(uint16_t idx) {
  if (irTotal == 0 || idx >= irTotal) return;

  String path = irFiles[idx];
  String err;
  IrCaptureHeader h{};
  if (!readHeader(path, h, &err)) {
    showNotification("IR", "Read failed");
    selectedValid = false;
    cacheDirty = true;
    updateDisplay();
    return;
  }
  if (!readRaw(path, h, txRaw, &err)) {
    showNotification("IR", "Raw read failed");
    updateDisplay();
    return;
  }

  irsend.begin();
  tft.fillRect(0, 40, 240, 28, FEATURE_BG);
  tft.setCursor(10, 44);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.print("Sending...");

  irsend.sendRaw(txRaw, h.rawLen, h.khz);
  delay(250);

  tft.fillRect(0, 40, 240, 28, FEATURE_BG);
  tft.setCursor(10, 44);
  tft.print("Done!");
  delay(300);

  updateDisplay();
}

static void deleteProfile(uint16_t idx) {
  if (irTotal == 0 || idx >= irTotal) return;

  uint32_t now = millis();
  if (!deleteArmed || (int32_t)(now - deleteArmUntilMs) >= 0) {
    deleteArmed = true;
    deleteArmUntilMs = now + 3000;
    drawDetails();
    return;
  }

  deleteArmed = false;

  String path = irFiles[idx];
  if (!SD.remove(path)) {
    tft.fillRect(0, 40, 240, 280, FEATURE_BG);
    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.print("Delete FAILED");
    tft.setCursor(10, 45 + yshift);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.print(baseName(path));
    delay(1200);
    updateDisplay();
    return;
  }

  refreshSdIndex(false);
  if (irTotal == 0) currentIndex = 0;
  else if (currentIndex >= irTotal) currentIndex = (uint16_t)(irTotal - 1);
  selectedValid = false;
  cacheDirty = true;
  updateDisplay();
}

static void runUI() {

  static const int ICON_NUM = 6;
  static int iconX[ICON_NUM] = {90, 130, 170, 210, 50, 10};
  static int iconY = 20;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_sort_down_minus,
    bitmap_icon_sort_up_plus,
    bitmap_icon_antenna,
    bitmap_icon_recycle,
    bitmap_icon_sdcard,
    bitmap_icon_go_back
  };

  if (!uiDrawn) {

    tft.fillRect(0, 20, 240, 16, UI_FG);
    tft.drawLine(0, 20, 240, 20, UI_LINE);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i]) tft.drawBitmap(iconX[i], iconY, icons[i], 16, 16, UI_ICON);
    }
    tft.drawLine(0, 20 + 16, 240, 20 + 16, UI_LINE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], 16, 16, UI_ICON);
      animationState = 2;

      switch (activeIcon) {
        case 0:
          if (irTotal > 0) {
            uint16_t oldIdx = currentIndex;
            currentIndex = (uint16_t)((currentIndex + 1) % irTotal);
            selectedValid = false;
            cacheDirty = true;
            deleteArmed = false;
            updateSelectionUI(oldIdx, false);
          }
          break;
        case 1:
          if (irTotal > 0) {
            uint16_t oldIdx = currentIndex;
            currentIndex = (uint16_t)((currentIndex + irTotal - 1) % irTotal);
            selectedValid = false;
            cacheDirty = true;
            deleteArmed = false;
            updateSelectionUI(oldIdx, false);
          }
          break;
        case 2:
          if (irTotal > 0) transmitProfile(currentIndex);
          break;
        case 3:
          if (irTotal > 0) deleteProfile(currentIndex);
          break;
        case 4:
          refreshSdIndex(true);
          selectedValid = false;
          cacheDirty = true;
          deleteArmed = false;
          updateDisplay();
          break;
        case 5:
          feature_exit_requested = true;
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {

      if (y >= BOT_Y && y < (BOT_Y + BOT_H)) {
        if (x >= BOT_TX_X && x < (BOT_TX_X + BOT_BTN_W)) {
          if (irTotal > 0) transmitProfile(currentIndex);
        } else if (x >= BOT_DEL_X && x < (BOT_DEL_X + BOT_BTN_W)) {
          if (irTotal > 0) deleteProfile(currentIndex);
        }
        lastTouchCheck = millis();
        return;
      }

      if (y >= LIST_Y && y < (LIST_Y + (ITEMS_PER_PAGE * ROW_H)) && x >= LIST_X && x < (LIST_X + LIST_W)) {
        uint8_t row = (uint8_t)((y - LIST_Y) / ROW_H);
        uint16_t oldIdx = currentIndex;
        uint16_t start = pageStartForIndex(currentIndex);
        uint16_t idx = (uint16_t)(start + row);
        if (idx < irTotal) {
          currentIndex = idx;
          selectedValid = false;
          cacheDirty = true;
          deleteArmed = false;
          updateSelectionUI(oldIdx, false);
        }
      }

      if (y > 20 && y < (20 + 16)) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + 16) {
            if (icons[i] && animationState == 0) {
              if (i == 5) {
                feature_exit_requested = true;
              } else {
                tft.drawBitmap(iconX[i], iconY, icons[i], 16, 16, FEATURE_BG);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
              }
            }
            break;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

void setup() {
  irsend.begin();
  tft.fillScreen(FEATURE_BG);
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  uiDrawn = false;

  refreshSdIndex(false);
  cacheDirty = true;
  deleteArmed = false;
  updateDisplay();
}

void loop() {
  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  runUI();

  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 200;

  bool prevPressed    = isButtonPressed(BTN_UP);
  bool nextPressed    = isButtonPressed(BTN_DOWN);
  bool txPressed      = isButtonPressed(BTN_RIGHT);
  bool refreshPressed = isButtonPressed(BTN_LEFT);

  if (irTotal > 0) {

    if (nextPressed && millis() - lastDebounceTime > debounceDelay) {
      uint16_t oldIdx = currentIndex;
      currentIndex = (uint16_t)((currentIndex + 1) % irTotal);
      selectedValid = false;
      updateSelectionUI(oldIdx, false);
      lastDebounceTime = millis();
    }

    if (prevPressed && millis() - lastDebounceTime > debounceDelay) {
      uint16_t oldIdx = currentIndex;
      currentIndex = (uint16_t)((currentIndex + irTotal - 1) % irTotal);
      selectedValid = false;
      updateSelectionUI(oldIdx, false);
      lastDebounceTime = millis();
    }

    if (txPressed && millis() - lastDebounceTime > debounceDelay) {
      transmitProfile(currentIndex);
      lastDebounceTime = millis();
    }

    if (refreshPressed && millis() - lastDebounceTime > debounceDelay) {
      refreshSdIndex(true);
      selectedValid = false;
      cacheDirty = true;
      deleteArmed = false;
      updateDisplay();
      lastDebounceTime = millis();
    }
  } else {
    tft.setCursor(10, 50 + yshift);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.print(sdLastErr.length() ? sdLastErr : "No profiles on SD.");
  }
}

}

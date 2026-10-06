#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <algorithm>
#include <vector>
#include "KeyboardUI.h"
#include "Touchscreen.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"

static constexpr int kIrScreenH = 320;

static int irContentBottom() {
  return featureHasTouchNavBar() ? touchNavContentBottomY() : kIrScreenH;
}

static void irClearBody(uint16_t color = FEATURE_BG) {
  if (featureHasTouchNavBar()) {
    featureClearContent(color);
  } else {
    tft.fillScreen(color);
  }
}

static void irClearContentFrom(int topY, uint16_t color = FEATURE_BG) {
  const int bottom = irContentBottom();
  if (bottom > topY) {
    tft.fillRect(0, topY, 240, bottom - topY, color);
  }
}

static void irRedrawNavChrome() {
  redrawTouchButtonBar();
  maintainTouchNavBar();
}

static void irSetCaptureNavLabels() {
  setTouchNavLabels("Rep-", "Save", "Exit", "Send", "Rep+");
}

static void irSetSavedNavLabels() {
  setTouchNavLabels("Delete", "Next", "Exit", "Prev", "TX");
}

static void irSetUniversalRemoteNavLabels() {
  setTouchNavLabels("Prev", "Del", "Exit", "Reload", "Next");
}

static void irSetUniversalBrowseNavLabels() {
  setTouchNavLabels("Back", "Next", "Exit", "Prev", "Select");
}

static void irSetCopyNavLabels(const char* left, const char* down, const char* center,
                               const char* up, const char* right) {
  setTouchNavLabels(left, down, center, up, right);
  // Don't force an immediate full nav bar redraw here — maintainTouchNavBar()
  // in the feature loop will paint once when the cue is invalidated.
}


namespace IRRemoteFeature {

static constexpr uint16_t kRecvPin = IR_RX_PIN;
static constexpr uint16_t kSendPin = IR_TX_PIN;
static constexpr uint16_t kKhz     = IR_DEFAULT_KHZ;

static constexpr int16_t kToolbarY = 20;
static constexpr int16_t kToolbarH = 16;
static constexpr int16_t kIconSize = 16;

static constexpr int kIrToolbarBottom = 36;
static constexpr int kIrToolbarGap = 8;
static constexpr int kIrBoxHeaderH = 15;
static constexpr int kIrStatusY = kIrToolbarBottom + kIrToolbarGap;
static constexpr int kIrStatusBoxH = 91;
static constexpr int kIrLogGap = 4;
static constexpr int kIrLogBoxH = 49;
static constexpr int kIrLogBoxTop = kIrStatusY + kIrStatusBoxH + kIrLogGap;
static constexpr int kIrLogStartY = kIrLogBoxTop + kIrBoxHeaderH;
static constexpr int kIrLogEndY = kIrLogBoxTop + kIrLogBoxH - 2;
static constexpr int kIrGraphTop = kIrLogBoxTop + kIrLogBoxH + 4;
static constexpr int kIrGraphMarginX = 6;
static constexpr int kIrLineHeight = 12;
static constexpr int kIrStatusLineCount = 6;
static constexpr int kIrStatusTextY = kIrStatusY + kIrBoxHeaderH;
static constexpr int kIrMaxLogLines = 48;
static constexpr uint16_t kIrPlotBg = 0x0842;
static constexpr uint16_t kIrGridColor = 0x2945;
static constexpr int kIrGridDivisions = 4;

struct IrPlotLayout {
  int graphTop = 0;
  int axisX = 0;
  int plotRight = 0;
  int plotTop = 0;
  int plotBottom = 0;
  int plotHeight = 0;
  int plotWidth = 0;
  bool valid = false;
};

static IrPlotLayout s_irPlot;
static bool s_irBoxesDrawn = false;
static bool s_irGraphChromeDrawn = false;
static String s_irLogBuffer[kIrMaxLogLines];
static uint16_t s_irLogColor[kIrMaxLogLines];
static int s_irLogIndex = 0;
static String s_irStatusLineText[kIrStatusLineCount];
static uint16_t s_irStatusLineColor[kIrStatusLineCount];
static bool s_irStatusStaticDrawn = false;
static uint32_t s_irLastStatusDrawMs = 0;

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
static uint32_t s_wavePanUs = 0;
static constexpr uint32_t kWaveZoomUs[] = {0, 100000, 50000, 20000, 10000};
static constexpr uint8_t kWaveZoomCount = 5;

static uint8_t  s_scope[240]{};
static uint16_t s_scopePos = 0;
static uint32_t s_lastScopeSampleUs = 0;
static uint32_t s_lastScopeDrawMs = 0;

static uint32_t waveformWindowUs(uint32_t totalUs) {
  const uint32_t w = kWaveZoomUs[s_waveZoomIdx % kWaveZoomCount];
  if (w == 0) return totalUs;
  return (totalUs < w) ? totalUs : w;
}

static const char* zoomLabel() {
  switch (s_waveZoomIdx % kWaveZoomCount) {
    case 0: return "FIT";
    case 1: return "100ms";
    case 2: return "50ms";
    case 3: return "20ms";
    default: return "10ms";
  }
}

static uint32_t irWaveTotalUs() {
  uint32_t total = 0;
  for (uint16_t i = 0; i < s_rawLen; i++) {
    total += s_raw[i];
  }
  return total;
}

static void irWaveClampPan() {
  const uint32_t total = irWaveTotalUs();
  const uint32_t window = waveformWindowUs(total);
  if (window == 0 || window >= total) {
    s_wavePanUs = 0;
    return;
  }
  const uint32_t maxPan = total - window;
  if (s_wavePanUs > maxPan) {
    s_wavePanUs = maxPan;
  }
}

static String irGraphTitle() {
  if (!s_hasCapture) {
    return "Live IR Scope";
  }
  String title = "Waveform " + String(zoomLabel());
  if (s_waveZoomIdx != 0 && s_wavePanUs > 0) {
    title += " @+" + String(s_wavePanUs / 1000) + "ms";
  }
  return title;
}

static void irDrawGraphChrome();
static void redrawWaveformPlotOnly();
static void irPrint(const String& text, uint16_t color, bool extraSpace);
static void irEnsurePlotLayout();
static void redrawGraphTitleOnly(bool captured);

static void irGraphRefreshWaveform() {
  if (!s_irPlot.valid) {
    irEnsurePlotLayout();
  }
  redrawGraphTitleOnly(true);
  redrawWaveformPlotOnly();
}

static void irWaveZoomIn() {
  if (!s_hasCapture) {
    return;
  }
  if (s_waveZoomIdx + 1 < kWaveZoomCount) {
    s_waveZoomIdx++;
  }
  irWaveClampPan();
  irGraphRefreshWaveform();
}

static void irWaveZoomOut() {
  if (!s_hasCapture) {
    return;
  }
  if (s_waveZoomIdx > 0) {
    s_waveZoomIdx--;
  }
  if (s_waveZoomIdx == 0) {
    s_wavePanUs = 0;
  }
  irWaveClampPan();
  irGraphRefreshWaveform();
}

static void irWaveCycleZoom() {
  if (!s_hasCapture) {
    return;
  }
  s_waveZoomIdx = (uint8_t)((s_waveZoomIdx + 1) % kWaveZoomCount);
  if (s_waveZoomIdx == 0) {
    s_wavePanUs = 0;
  }
  irWaveClampPan();
  irGraphRefreshWaveform();
}

static void irWavePanBy(int32_t deltaUs) {
  if (!s_hasCapture || s_waveZoomIdx == 0) {
    return;
  }
  const uint32_t total = irWaveTotalUs();
  const uint32_t window = waveformWindowUs(total);
  if (window == 0 || window >= total) {
    return;
  }
  const uint32_t maxPan = total - window;
  const int64_t next = (int64_t)s_wavePanUs + deltaUs;
  if (next <= 0) {
    s_wavePanUs = 0;
  } else if ((uint32_t)next >= maxPan) {
    s_wavePanUs = maxPan;
  } else {
    s_wavePanUs = (uint32_t)next;
  }
  irGraphRefreshWaveform();
}

static void irHandleGraphTouch(int x, int y) {
  if (!s_hasCapture || !s_irPlot.valid) {
    return;
  }
  if (x < s_irPlot.axisX || x > s_irPlot.plotRight ||
      y < s_irPlot.plotTop || y > s_irPlot.plotBottom) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastActionMs < 220) {
    return;
  }

  const int plotW = s_irPlot.plotRight - s_irPlot.axisX;
  const int relX = x - s_irPlot.axisX;
  const uint32_t total = irWaveTotalUs();
  const uint32_t window = waveformWindowUs(total);
  const uint32_t panStep = (window / 4 > 1000U) ? (window / 4) : 1000U;

  if (relX < plotW / 3) {
    irWavePanBy(-(int32_t)panStep);
    irPrint("[*] Graph pan -", UI_DIM_TEXT, false);
  } else if (relX > (plotW * 2) / 3) {
    irWavePanBy((int32_t)panStep);
    irPrint("[*] Graph pan +", UI_DIM_TEXT, false);
  } else {
    irWaveCycleZoom();
    irPrint(String("[*] Zoom ") + zoomLabel(), UI_DIM_TEXT, false);
  }
  s_lastActionMs = now;
}

static void irEnsurePlotLayout() {
  const int screenW = tft.width();
  s_irPlot.graphTop = kIrGraphTop;
  s_irPlot.axisX = kIrGraphMarginX;
  s_irPlot.plotRight = screenW - kIrGraphMarginX;
  s_irPlot.plotTop = s_irPlot.graphTop + 12;
  s_irPlot.plotBottom = irContentBottom() - 13;
  s_irPlot.plotHeight = s_irPlot.plotBottom - s_irPlot.plotTop;
  s_irPlot.plotWidth = s_irPlot.plotRight - s_irPlot.axisX;
  s_irPlot.valid = s_irPlot.plotWidth >= 32 && s_irPlot.plotHeight >= 10;
}

static String irFitStatusText(const String& text) {
  const int maxWidth = tft.width() - 16;
  tft.setTextSize(1);
  if (tft.textWidth(text) <= maxWidth) {
    return text;
  }
  String out = text;
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  if (out.length() > 0) {
    out += "...";
  }
  return out;
}

static void irScrollLog() {
  for (int i = 0; i < kIrMaxLogLines - 1; i++) {
    s_irLogBuffer[i] = s_irLogBuffer[i + 1];
    s_irLogColor[i] = s_irLogColor[i + 1];
  }
}

static int irLogVisibleLines() {
  const int h = kIrLogEndY - kIrLogStartY;
  if (h <= 0 || kIrLineHeight <= 0) {
    return 1;
  }
  return h / kIrLineHeight;
}

static void irRedrawActivityLog() {
  const int visible = irLogVisibleLines();
  tft.fillRect(8, kIrLogStartY, tft.width() - 16, kIrLogEndY - kIrLogStartY, TFT_BLACK);
  if (visible <= 0 || s_irLogIndex <= 0) {
    return;
  }

  const int start = (s_irLogIndex > visible) ? (s_irLogIndex - visible) : 0;
  for (int row = 0; row < visible; row++) {
    const int bufIndex = start + row;
    if (bufIndex >= s_irLogIndex) {
      break;
    }
    if (s_irLogBuffer[bufIndex].length() == 0) {
      continue;
    }
    const int yPos = kIrLogStartY + row * kIrLineHeight;
    tft.setTextSize(1);
    tft.setTextColor(s_irLogColor[bufIndex], TFT_BLACK);
    tft.setCursor(8, yPos);
    tft.print(s_irLogBuffer[bufIndex]);
  }
}

static void irPrint(const String& text, uint16_t color, bool extraSpace = false) {
  if (s_irLogIndex >= kIrMaxLogLines) {
    irScrollLog();
    s_irLogIndex = kIrMaxLogLines - 1;
  }

  s_irLogBuffer[s_irLogIndex] = text;
  s_irLogColor[s_irLogIndex] = color;
  s_irLogIndex++;

  if (extraSpace && s_irLogIndex < kIrMaxLogLines) {
    s_irLogBuffer[s_irLogIndex] = "";
    s_irLogColor[s_irLogIndex] = TFT_WHITE;
    s_irLogIndex++;
  }

  irRedrawActivityLog();
}

static void irDrawStatusLine(int line, const String& text, uint16_t color) {
  const int y = kIrStatusTextY + line * kIrLineHeight;
  const String fitted = irFitStatusText(text);
  tft.fillRect(8, y, tft.width() - 16, kIrLineHeight, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(8, y);
  tft.print(fitted);
}

static void irDrawStatusLineIfChanged(int line, const String& text, uint16_t color) {
  if (line < 0 || line >= kIrStatusLineCount) {
    return;
  }
  if (s_irStatusLineText[line] == text && s_irStatusLineColor[line] == color) {
    return;
  }
  s_irStatusLineText[line] = text;
  s_irStatusLineColor[line] = color;
  irDrawStatusLine(line, text, color);
}

static void irDrawTextBoxes() {
  tft.fillRect(0, kIrStatusY - 2, tft.width(), kIrGraphTop - kIrStatusY + 2, TFT_BLACK);
  tft.drawFastHLine(0, 19, tft.width(), UI_LINE);
  tft.drawRoundRect(4, kIrStatusY, tft.width() - 8, kIrStatusBoxH, 3, UI_LINE);
  tft.drawRoundRect(4, kIrLogBoxTop, tft.width() - 8, kIrLogBoxH, 3, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("Signal Status", 8, kIrStatusY + 3);
  tft.drawString("Activity", 8, kIrLogBoxTop + 3);
  s_irBoxesDrawn = true;
}

static void irDrawStaticStatusLines() {
  if (s_irStatusStaticDrawn) {
    return;
  }
  irDrawStatusLineIfChanged(5, "Carrier: " + String(kKhz) + " kHz IR", UI_DIM_TEXT);
  s_irStatusStaticDrawn = true;
}

static void irUpdateGraphHintLine() {
  if (s_hasCapture) {
    irDrawStatusLineIfChanged(5, "Graph: +/- zoom  tap L/R pan", UI_DIM_TEXT);
  } else {
    s_irStatusLineText[5] = "";
    s_irStatusLineColor[5] = 0;
    irDrawStatusLineIfChanged(5, "Carrier: " + String(kKhz) + " kHz IR", UI_DIM_TEXT);
  }
}

static void irUpdateStatusPanel(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - s_irLastStatusDrawMs < 120) {
    return;
  }
  s_irLastStatusDrawMs = now;

  irDrawStaticStatusLines();
  irDrawStatusLineIfChanged(4, "Repeat: " + String((unsigned)s_repeat) +
                            "   Auto: " + String(s_autoTx ? "ON" : "OFF"),
                            s_autoTx ? UI_WARN : UI_DIM_TEXT);

  if (!s_hasCapture) {
    irDrawStatusLineIfChanged(0, "State: Listening", UI_WARN);
    irDrawStatusLineIfChanged(1, "Type: --", UI_DIM_TEXT);
    irDrawStatusLineIfChanged(2, "Key: --", UI_DIM_TEXT);
    irDrawStatusLineIfChanged(3, "Raw: --", UI_DIM_TEXT);
    irUpdateGraphHintLine();
    return;
  }

  irDrawStatusLineIfChanged(0, "State: Captured", UI_OK);
  irDrawStatusLineIfChanged(1, "Type: " + String(typeToString(s_decodeType)), UI_TEXT);
  String keyLine = s_keyText;
  if (keyLine.length() > 28) {
    keyLine = keyLine.substring(0, 28);
  }
  irDrawStatusLineIfChanged(2, "Key: " + keyLine, UI_TEXT);
  irDrawStatusLineIfChanged(3, "Raw: " + String((unsigned)s_rawLen) +
                            " pulses  Bits: " + String((unsigned)s_bits), UI_TEXT);
  irUpdateGraphHintLine();
}

static void irDrawGraphGrid() {
  for (int g = 1; g < kIrGridDivisions; g++) {
    const int gy = s_irPlot.plotTop + ((s_irPlot.plotHeight * g) + (kIrGridDivisions / 2)) / kIrGridDivisions;
    tft.drawFastHLine(s_irPlot.axisX + 1, gy, s_irPlot.plotWidth - 2, kIrGridColor);
  }
  for (int v = 1; v < kIrGridDivisions; v++) {
    const int vx = s_irPlot.axisX + ((s_irPlot.plotWidth * v) + (kIrGridDivisions / 2)) / kIrGridDivisions;
    tft.drawFastVLine(vx, s_irPlot.plotTop + 1, s_irPlot.plotHeight - 2, kIrGridColor);
  }
}

static void irDrawGraphChrome() {
  irEnsurePlotLayout();
  if (!s_irPlot.valid) {
    return;
  }

  const int screenW = tft.width();
  const int graphBottom = irContentBottom() - 2;
  tft.fillRect(0, kIrGraphTop, screenW, graphBottom - kIrGraphTop + 2, TFT_BLACK);
  tft.fillRect(s_irPlot.axisX, s_irPlot.plotTop, s_irPlot.plotWidth, s_irPlot.plotHeight, kIrPlotBg);
  irDrawGraphGrid();
  tft.drawRect(s_irPlot.axisX, s_irPlot.plotTop, s_irPlot.plotWidth, s_irPlot.plotHeight, UI_LINE);

  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  if (s_hasCapture) {
    const String title = irGraphTitle();
    tft.drawString(title, (screenW - tft.textWidth(title)) / 2, s_irPlot.graphTop + 2);
  } else {
    tft.drawString("Live IR Scope", (screenW - 66) / 2, s_irPlot.graphTop + 2);
  }

  s_irGraphChromeDrawn = true;
}

static void irResetUiState() {
  s_irBoxesDrawn = false;
  s_irGraphChromeDrawn = false;
  s_irPlot.valid = false;
  s_irStatusStaticDrawn = false;
  s_irLastStatusDrawMs = 0;
  s_waveZoomIdx = 0;
  s_wavePanUs = 0;
  s_irLogIndex = 0;
  for (int i = 0; i < kIrMaxLogLines; i++) {
    s_irLogBuffer[i] = "";
    s_irLogColor[i] = 0;
  }
  for (int i = 0; i < kIrStatusLineCount; i++) {
    s_irStatusLineText[i] = "";
    s_irStatusLineColor[i] = 0;
  }
}

static void redrawGraphTitleOnly(bool captured) {
  (void)captured;
  if (s_irGraphChromeDrawn) {
    const int screenW = tft.width();
    tft.fillRect(0, s_irPlot.graphTop, screenW, 12, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    if (s_hasCapture) {
      const String title = irGraphTitle();
      tft.drawString(title, (screenW - tft.textWidth(title)) / 2, s_irPlot.graphTop + 2);
    } else {
      tft.drawString("Live IR Scope", (screenW - 66) / 2, s_irPlot.graphTop + 2);
    }
  }
}

static void redrawScopePlotOnly() {
  if (!s_irGraphChromeDrawn) {
    return;
  }
  if (!s_irPlot.valid) {
    irEnsurePlotLayout();
  }
  if (!s_irPlot.valid) {
    return;
  }

  const int px0 = s_irPlot.axisX + 1;
  const int py0 = s_irPlot.plotTop + 1;
  const int pw  = s_irPlot.plotWidth - 2;
  const int ph  = s_irPlot.plotHeight - 2;

  tft.startWrite();
  tft.fillRect(px0, py0, pw, ph, kIrPlotBg);

  const int highY = py0 + 8;
  const int lowY  = py0 + ph - 9;

  for (int x = 0; x < pw && x < 240; x++) {
    uint16_t idx = (uint16_t)((s_scopePos + x) % 240);
    bool levelHigh = (s_scope[idx] != 0);
    int y = levelHigh ? highY : lowY;
    tft.drawPixel(px0 + x, y, levelHigh ? UI_WARN : UI_OK);
    if (x > 0) {
      uint16_t pidx = (uint16_t)((s_scopePos + x - 1) % 240);
      bool prevHigh = (s_scope[pidx] != 0);
      int py = prevHigh ? highY : lowY;
      if (py != y) {
        tft.drawFastVLine(px0 + x, highY, (lowY - highY + 1), kIrGridColor);
      }
    }
  }
  tft.endWrite();
}

static void redrawWaveformPlotOnly() {
  if (!s_irGraphChromeDrawn) {
    return;
  }
  if (!s_irPlot.valid) {
    irEnsurePlotLayout();
  }
  if (!s_irPlot.valid) {
    return;
  }

  const int px0 = s_irPlot.axisX + 1;
  const int py0 = s_irPlot.plotTop + 1;
  const int pw  = s_irPlot.plotWidth - 2;
  const int ph  = s_irPlot.plotHeight - 2;

  const uint16_t colGridMajor = UI_LINE;
  const uint16_t colGridMinor = kIrGridColor;
  const uint16_t colMark      = UI_OK;
  const uint16_t colSpace     = UI_WARN;
  const uint16_t colText      = UI_DIM_TEXT;

  tft.startWrite();
  tft.fillRect(px0, py0, pw, ph, kIrPlotBg);

  const int labelY = py0 + ph - 10;
  const int highY = py0 + 10;
  const int lowY  = py0 + ph - 18;

  const uint32_t totalUs = irWaveTotalUs();
  if (totalUs == 0 || s_rawLen == 0) {
    tft.endWrite();
    return;
  }

  const uint32_t windowUs = waveformWindowUs(totalUs);
  if (windowUs == 0) {
    tft.endWrite();
    return;
  }

  const uint32_t panUs = s_wavePanUs;
  const uint32_t viewEnd = panUs + windowUs;

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
  tft.setTextColor(colText, kIrPlotBg);
  int lastLabelRight = -10000;

  for (uint32_t t = 0, n = 0; t <= windowUs; t += tickUs, n++) {
    int x = px0 + (int)((t * (uint32_t)pw) / windowUs);
    if (x < px0 || x >= (px0 + pw)) continue;

    const bool isMajor = (n % majorEvery) == 0;
    tft.drawFastVLine(x, py0, ph, (t == 0) ? colGridMajor : (isMajor ? colGridMajor : colGridMinor));

    if (!isMajor) continue;

    uint32_t ms = (t + panUs) / 1000;
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

  for (uint16_t i = 0; i < s_rawLen; i++) {
    const uint32_t segStart = tUs;
    const uint32_t segEnd = tUs + s_raw[i];
    tUs = segEnd;

    if (segEnd <= panUs) {
      isMark = !isMark;
      continue;
    }
    if (segStart >= viewEnd) {
      break;
    }

    const uint32_t clipStart = (segStart < panUs) ? panUs : segStart;
    const uint32_t clipEnd = (segEnd > viewEnd) ? viewEnd : segEnd;

    int x1 = px0 + (int)(((clipStart - panUs) * (uint32_t)pw) / windowUs);
    int x2 = px0 + (int)(((clipEnd - panUs) * (uint32_t)pw) / windowUs);
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

    if (x2 != x1) {
      tft.drawFastVLine(x2, highY, (lowY - highY + 1), colGridMinor);
    }

    isMark = !isMark;
  }
  tft.endWrite();
}

static void redrawCapturedDetailsOnly() {
  irUpdateStatusPanel(true);
  redrawGraphTitleOnly(true);
  redrawWaveformPlotOnly();
}

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

static void tryReplay();
static void saveCapture();
static void runUI();
static void irRestoreChrome();

static void irRestoreChrome() {
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);

  s_uiDrawn = false;
  s_irBoxesDrawn = false;
  s_irGraphChromeDrawn = false;
  s_irStatusStaticDrawn = false;
  for (int i = 0; i < kIrStatusLineCount; i++) {
    s_irStatusLineText[i] = "";
    s_irStatusLineColor[i] = 0;
  }

  runUI();
  irDrawTextBoxes();
  irEnsurePlotLayout();
  irDrawGraphChrome();
  irUpdateStatusPanel(true);

  irRedrawActivityLog();

  if (s_hasCapture) {
    redrawWaveformPlotOnly();
  } else {
    redrawScopePlotOnly();
  }

  irRedrawNavChrome();
  tft.drawFastHLine(0, kToolbarY, 240, UI_LINE);
  tft.drawFastHLine(0, kToolbarY + kToolbarH, 240, UI_LINE);
}

static String getUserInputName() {
  OnScreenKeyboardConfig cfg;
  cfg.titleLine1      = "[!] Name this IR capture";
  cfg.titleLine2      = "(max 15 chars, ^ caps, # sym)";
  osKeyboardUseStandardLayout(cfg);
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
  irRestoreChrome();
  if (!r.accepted) {
    return "";
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
  if (!s_irBoxesDrawn) {
    irDrawTextBoxes();
    irRedrawActivityLog();
  }
  irUpdateStatusPanel(true);

  if (!s_irGraphChromeDrawn) {
    irDrawGraphChrome();
    if (!s_hasCapture) {
      redrawScopePlotOnly();
    } else {
      redrawWaveformPlotOnly();
    }
  }
}

static void handleTouchNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    s_autoTx = false;
    if (s_repeat > 1) s_repeat--;
    s_contentDirty = true;
    irUpdateStatusPanel(true);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    s_autoTx = false;
    if (s_repeat < 10) s_repeat++;
    s_contentDirty = true;
    irUpdateStatusPanel(true);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    s_autoTx = false;
    if (s_hasCapture) tryReplay();
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    if (s_hasCapture) saveCapture();
  }
}

static void runUI() {
  static constexpr int kIconBackX = 10;
  static constexpr int kIconZoomOutX = 130;
  static constexpr int kIconZoomInX = 170;
  static constexpr int kIconAutoX = 210;
  static int iconY = kToolbarY;

  if (!s_uiDrawn) {
    tft.fillRect(0, kToolbarY, 160, kToolbarH, DARK_GRAY);
    tft.setTextColor(UI_TEXT, DARK_GRAY);
    tft.setCursor(35, kToolbarY + 4);
    tft.print("IR Record");

    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.fillRect(160, kToolbarY, 80, kToolbarH, DARK_GRAY);

    tft.drawBitmap(kIconBackX, iconY, bitmap_icon_go_back, kIconSize, kIconSize, UI_ICON);
    if (s_hasCapture) {
      tft.drawBitmap(kIconZoomOutX, iconY, bitmap_icon_sort_down_minus, kIconSize, kIconSize, UI_ICON);
      tft.drawBitmap(kIconZoomInX, iconY, bitmap_icon_sort_up_plus, kIconSize, kIconSize, UI_ICON);
    }
    tft.drawBitmap(kIconAutoX, iconY, bitmap_icon_random, kIconSize, kIconSize, UI_ICON);

    tft.drawFastHLine(0, kToolbarY + kToolbarH, 240, UI_LINE);
    s_uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      switch (activeIcon) {
        case 0:
          irWaveZoomOut();
          irPrint("[*] Zoom " + String(zoomLabel()), UI_DIM_TEXT, false);
          break;
        case 1:
          irWaveZoomIn();
          irPrint("[*] Zoom " + String(zoomLabel()), UI_DIM_TEXT, false);
          break;
        case 2:
          if (s_hasCapture) {
            s_autoTx = !s_autoTx;
            s_lastAutoMs = 0;
            s_contentDirty = true;
            irPrint(String("[*] Auto ") + (s_autoTx ? "ON" : "OFF"), s_autoTx ? UI_WARN : UI_DIM_TEXT, false);
            irUpdateStatusPanel(true);
          } else {
            irPrint("[!] No capture for auto", UI_DIM_TEXT, false);
          }
          break;
        default:
          break;
      }
      if (activeIcon == 0) {
        tft.drawBitmap(kIconZoomOutX, iconY, bitmap_icon_sort_down_minus, kIconSize, kIconSize, UI_ICON);
      } else if (activeIcon == 1) {
        tft.drawBitmap(kIconZoomInX, iconY, bitmap_icon_sort_up_plus, kIconSize, kIconSize, UI_ICON);
      } else if (activeIcon == 2) {
        tft.drawBitmap(kIconAutoX, iconY, bitmap_icon_random, kIconSize, kIconSize, UI_ICON);
      }
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
        if (x > kIconBackX && x < (kIconBackX + kIconSize)) {
          feature_exit_requested = true;
        } else if (s_hasCapture && x > kIconZoomOutX && x < (kIconZoomOutX + kIconSize) && animationState == 0) {
          tft.drawBitmap(kIconZoomOutX, iconY, bitmap_icon_sort_down_minus, kIconSize, kIconSize, TFT_BLACK);
          animationState = 1;
          activeIcon = 0;
          lastAnimationTime = millis();
        } else if (s_hasCapture && x > kIconZoomInX && x < (kIconZoomInX + kIconSize) && animationState == 0) {
          tft.drawBitmap(kIconZoomInX, iconY, bitmap_icon_sort_up_plus, kIconSize, kIconSize, TFT_BLACK);
          animationState = 1;
          activeIcon = 1;
          lastAnimationTime = millis();
        } else if (x > kIconAutoX && x < (kIconAutoX + kIconSize) && animationState == 0) {
          tft.drawBitmap(kIconAutoX, iconY, bitmap_icon_random, kIconSize, kIconSize, TFT_BLACK);
          animationState = 1;
          activeIcon = 2;
          lastAnimationTime = millis();
        }
      }
    }
    lastTouchCheck = millis();
  }
}

static void tryReplay() {
  if (!s_hasCapture || s_rawLen == 0) return;

  s_recv.disableIRIn();
  irPrint("[!] Sending...", UI_TEXT, false);
  irDrawStatusLineIfChanged(0, "State: Sending", UI_WARN);

  for (uint8_t i = 0; i < s_repeat; i++) {
    s_send.sendRaw(s_raw, s_rawLen, kKhz);
    delay(35);
  }

  if (!s_autoTx) {
    irPrint("[+] Send done", UI_WARN, false);
  }

  delay(50);
  s_recv.enableIRIn();
  s_lastActionMs = millis();
  irUpdateStatusPanel(true);
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
    return;
  }

  irPrint("[!] Saving...", UI_TEXT, false);

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

  irPrint("[+] Saved " + name, UI_OK, false);
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
      s_irGraphChromeDrawn = false;
      s_uiDrawn = false;
      s_waveZoomIdx = 0;
      s_wavePanUs = 0;
      irPrint("[+] Signal captured", UI_OK, false);
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
  setTouchButtonInputEnabled(true);
  irSetCaptureNavLabels();
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

  irResetUiState();
  irClearBody(TFT_BLACK);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  runUI();
  irDrawTextBoxes();
  irEnsurePlotLayout();
  irDrawGraphChrome();
  irUpdateStatusPanel(true);
  irPrint("[+] IR Record ready", UI_WARN, false);
  irPrint("[*] Point remote and press", UI_DIM_TEXT, false);
  irRedrawNavChrome();
  s_contentDirty = false;
}

void loop() {

  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  runUI();
  if (s_uiDrawn) {
    tft.drawFastHLine(0, kToolbarY, 240, UI_LINE);
    tft.drawFastHLine(0, kToolbarY + kToolbarH, 240, UI_LINE);
  }
  handleTouchNavButtons();

  pollCapture();

  if (s_hasCapture) {
    int x, y;
    if (readTouchXY(x, y)) {
      if (!s_irPlot.valid) {
        irEnsurePlotLayout();
      }
      irHandleGraphTouch(x, y);
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
  const bool leftPressed  = isPhysicalButtonPressed(BTN_LEFT);
  const bool rightPressed = isPhysicalButtonPressed(BTN_RIGHT);
  const bool upPressed    = isPhysicalButtonPressed(BTN_UP);
  const bool downPressed  = isPhysicalButtonPressed(BTN_DOWN);

  if (rightPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    if (s_repeat < 10) s_repeat++;
    s_contentDirty = true;
    irUpdateStatusPanel(true);
    lastDebounceTime = millis();
  }
  if (leftPressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    if (s_repeat > 1) s_repeat--;
    s_contentDirty = true;
    irUpdateStatusPanel(true);
    lastDebounceTime = millis();
  }
  if (upPressed && !prevUp && s_hasCapture && millis() - lastDebounceTime > debounceDelay) {
    s_autoTx = false;
    tryReplay();
    lastDebounceTime = millis();
  }
  if (downPressed && !prevDown && s_hasCapture && millis() - lastDebounceTime > debounceDelay) {
    saveCapture();
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
static constexpr int LIST_X = 10;
static constexpr int LIST_W = 220;
static constexpr int HEADER_Y = 50;
static constexpr int HEADER_H = 14;
static constexpr int LIST_Y = HEADER_Y + HEADER_H + 2;
static constexpr int ROW_H  = 18;
static constexpr int UI_GAP_Y = 6;
static constexpr int DETAIL_LINE_H = 14;
static constexpr int DETAIL_LINE_GAP = 3;
static constexpr int DETAIL_LINE_STEP = DETAIL_LINE_H + DETAIL_LINE_GAP;
static constexpr int DETAIL_LINES = 4;
static constexpr int DETAIL_CONTENT_H = DETAIL_LINE_STEP * (DETAIL_LINES - 1) + DETAIL_LINE_H;
static constexpr int DETAIL_LABEL_X = LIST_X;
static constexpr int DETAIL_VALUE_X = 50;
static constexpr int DETAIL_COL2_LABEL_X = 130;
static constexpr int DETAIL_COL2_VALUE_X = 165;

static int profileBottomY() {
  return irContentBottom();
}

static int listBottomY() {
  return LIST_Y + (ITEMS_PER_PAGE * ROW_H);
}

static int detailsY() {
  const int areaTop = listBottomY();
  const int areaBottom = profileBottomY();
  const int areaH = areaBottom - areaTop;
  if (areaH <= DETAIL_CONTENT_H) {
    return areaTop + UI_GAP_Y;
  }
  return areaTop + (areaH - DETAIL_CONTENT_H) / 2;
}

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
static void transmitProfile(uint16_t idx);
static void deleteProfile(uint16_t idx);

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
  tft.fillRect(LIST_X, HEADER_Y, LIST_W, HEADER_H, FEATURE_BG);
  tft.setCursor(LIST_X, HEADER_Y);
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
  tft.setCursor(LIST_X, y + 4);
  tft.printf("%2d. ", (int)idx + 1);

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
  const int dy = detailsY();
  const int gapTop = listBottomY();
  const int gapH = profileBottomY() - gapTop;
  if (gapH > 0) {
    tft.fillRect(LIST_X, gapTop, LIST_W, gapH, FEATURE_BG);
  }
  tft.drawFastHLine(LIST_X, listBottomY(), LIST_W, UI_LINE);

  String err;
  if (!selectedValid) loadSelected(&err);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  if (!selectedValid) {
    tft.setCursor(DETAIL_LABEL_X, dy);
    tft.print("Read failed:");
    tft.setCursor(DETAIL_VALUE_X, dy);
    tft.print(err);
    return;
  }

  decode_type_t dt = (decode_type_t)selectedHeader.decodeType;
  tft.setCursor(DETAIL_LABEL_X, dy);
  tft.print("Type:");
  tft.setCursor(DETAIL_VALUE_X, dy);
  tft.print(String(typeToString(dt)));
  tft.setCursor(DETAIL_COL2_LABEL_X, dy);
  tft.print("kHz:");
  tft.setCursor(DETAIL_COL2_VALUE_X, dy);
  tft.printf("%ukHz", (unsigned)selectedHeader.khz);

  tft.setCursor(DETAIL_LABEL_X, dy + DETAIL_LINE_STEP);
  tft.print("Bits:");
  tft.setCursor(DETAIL_VALUE_X, dy + DETAIL_LINE_STEP);
  tft.print((unsigned)selectedHeader.bits);
  tft.setCursor(DETAIL_COL2_LABEL_X, dy + DETAIL_LINE_STEP);
  tft.print("Raw:");
  tft.setCursor(DETAIL_COL2_VALUE_X, dy + DETAIL_LINE_STEP);
  tft.print((unsigned)selectedHeader.rawLen);

  tft.setCursor(DETAIL_LABEL_X, dy + (DETAIL_LINE_STEP * 2));
  tft.print("Val:");
  tft.setCursor(DETAIL_VALUE_X, dy + (DETAIL_LINE_STEP * 2));
  tft.print("0x");
  tft.print(uint64ToString(selectedHeader.value, 16));

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(DETAIL_LABEL_X, dy + (DETAIL_LINE_STEP * 3));
  tft.print("SRC:");
  tft.setCursor(DETAIL_VALUE_X, dy + (DETAIL_LINE_STEP * 3));
  tft.print(baseName(selectedPath));

  if (deleteArmed && (int32_t)(millis() - deleteArmUntilMs) < 0) {
    int hintY = dy + (DETAIL_LINE_STEP * 4);
    if (hintY >= profileBottomY() - 12) hintY = profileBottomY() - 12;
    tft.setCursor(DETAIL_LABEL_X, hintY);
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.print("Press Delete again to confirm");
  }
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

static void selectNext() {
  if (irTotal == 0) return;
  uint16_t oldIdx = currentIndex;
  currentIndex = (uint16_t)((currentIndex + 1) % irTotal);
  selectedValid = false;
  cacheDirty = true;
  deleteArmed = false;
  updateSelectionUI(oldIdx, false);
}

static void selectPrev() {
  if (irTotal == 0) return;
  uint16_t oldIdx = currentIndex;
  currentIndex = (uint16_t)((currentIndex + irTotal - 1) % irTotal);
  selectedValid = false;
  cacheDirty = true;
  deleteArmed = false;
  updateSelectionUI(oldIdx, false);
}

static void handleTouchNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    if (irTotal > 0) deleteProfile(currentIndex);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    selectNext();
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    selectPrev();
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    if (irTotal > 0) transmitProfile(currentIndex);
  }
}

static void updateDisplay() {
  irClearBody(FEATURE_BG);
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  uiDrawn = false;

  drawHeaderLine();
  if (irTotal == 0) {
    tft.setCursor(LIST_X, HEADER_Y + DETAIL_LINE_H);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.print(sdLastErr.length() ? sdLastErr : "No profiles on SD.");
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
  irClearContentFrom(40, FEATURE_BG);
  tft.setCursor(10, 44);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.print("Sending...");

  irsend.sendRaw(txRaw, h.rawLen, h.khz);
  delay(250);

  irClearContentFrom(40, FEATURE_BG);
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
    irClearContentFrom(40, FEATURE_BG);
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

  static const int ICON_NUM = 4;
  static int iconX[ICON_NUM] = {130, 170, 210, 10};
  static int iconY = 20;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_antenna,
    bitmap_icon_recycle,
    bitmap_icon_undo,
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
          if (irTotal > 0) transmitProfile(currentIndex);
          break;
        case 1:
          if (irTotal > 0) deleteProfile(currentIndex);
          break;
        case 2:
          refreshSdIndex(true);
          selectedValid = false;
          cacheDirty = true;
          deleteArmed = false;
          updateDisplay();
          break;
        case 3:
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
              if (i == 3) {
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
  setTouchButtonInputEnabled(true);
  irSetSavedNavLabels();
  irsend.begin();
  irClearBody(FEATURE_BG);
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  uiDrawn = false;

  refreshSdIndex(false);
  cacheDirty = true;
  deleteArmed = false;
  updateDisplay();
  irRedrawNavChrome();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  runUI();
  if (uiDrawn) {
    tft.drawFastHLine(0, 20, 240, UI_LINE);
    tft.drawFastHLine(0, 36, 240, UI_LINE);
  }
  handleTouchNavButtons();

  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 200;
  static bool prevUp = false;
  static bool prevDown = false;
  static bool prevRight = false;
  static bool prevLeft = false;

  bool prevPressed    = isPhysicalButtonPressed(BTN_UP);
  bool nextPressed    = isPhysicalButtonPressed(BTN_DOWN);
  bool txPressed      = isPhysicalButtonPressed(BTN_RIGHT);
  bool deletePressed  = isPhysicalButtonPressed(BTN_LEFT);

  if (irTotal > 0) {

    if (nextPressed && !prevDown && millis() - lastDebounceTime > debounceDelay) {
      selectNext();
      lastDebounceTime = millis();
    }

    if (prevPressed && !prevUp && millis() - lastDebounceTime > debounceDelay) {
      selectPrev();
      lastDebounceTime = millis();
    }

    if (txPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
      transmitProfile(currentIndex);
      lastDebounceTime = millis();
    }

    if (deletePressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
      deleteProfile(currentIndex);
      lastDebounceTime = millis();
    }
  } else {
    tft.setCursor(10, 50 + yshift);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.print(sdLastErr.length() ? sdLastErr : "No profiles on SD.");
  }

  prevUp = prevPressed;
  prevDown = nextPressed;
  prevRight = txPressed;
  prevLeft = deletePressed;
}

}

// Shared Copy Controller → Universal bridge (session RAM + known SD paths).
// SD directory scans are flaky on shared-SPI boards; Universal always merges these.
static constexpr int kIrCustomKeySlots = 12;
static constexpr int kIrRamCustomMax = 8;
static constexpr const char* kIrUnivCustomPath = "/ir/univ_custom.json";
static constexpr const char* kIrUnivCustomRoot = "/univ_custom.json";

struct IrRamCustomProfile {
  bool used = false;
  char name[40]{};
  decode_type_t proto = decode_type_t::UNKNOWN;
  uint16_t bits = 0;
  uint64_t code[kIrCustomKeySlots]{};
  bool has[kIrCustomKeySlots]{};
};

static IrRamCustomProfile s_ramCustoms[kIrRamCustomMax];
static uint8_t s_ramCustomCount = 0;

static void irRamCustomRemember(const char* name, decode_type_t proto, uint16_t bits,
                                const uint64_t* codes, const bool* hasKeys) {
  if (!name || !name[0] || !codes || !hasKeys) return;

  // Replace same name if already cached.
  int slot = -1;
  for (int i = 0; i < (int)s_ramCustomCount; i++) {
    if (s_ramCustoms[i].used && strncmp(s_ramCustoms[i].name, name, sizeof(s_ramCustoms[i].name)) == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (s_ramCustomCount < kIrRamCustomMax) {
      slot = s_ramCustomCount++;
    } else {
      // Drop oldest.
      for (int i = 0; i < kIrRamCustomMax - 1; i++) s_ramCustoms[i] = s_ramCustoms[i + 1];
      slot = kIrRamCustomMax - 1;
      s_ramCustomCount = kIrRamCustomMax;
    }
  }

  IrRamCustomProfile& p = s_ramCustoms[slot];
  p = IrRamCustomProfile{};
  p.used = true;
  strncpy(p.name, name, sizeof(p.name) - 1);
  p.proto = proto;
  p.bits = bits;
  for (int i = 0; i < kIrCustomKeySlots; i++) {
    p.has[i] = hasKeys[i];
    p.code[i] = hasKeys[i] ? codes[i] : 0;
  }
}

namespace IRUniversalController {

static constexpr const char* PROFILES_PATH = "/ir_profiles.json";
static constexpr const char* PROFILES_DIR  = "/ir_profiles";
static constexpr const char* UNIV_CUSTOM_PATH = "/ir/univ_custom.json";
static constexpr const char* UNIV_CUSTOM_ROOT = "/univ_custom.json";

static IRsend s_send(IR_TX_PIN);

static constexpr uint16_t kMaxProfiles = 500;

static constexpr int16_t kToolbarY = 20;
static constexpr int16_t kToolbarH = 16;
static constexpr int16_t kIconSize = 16;
static constexpr int kToolbarBottom = kToolbarY + kToolbarH;
static constexpr int kBodyTop = kToolbarBottom + 1;

static constexpr int kPadX = 10;
static constexpr int kListW = 220;
static constexpr int kDetailLineH = 14;
static constexpr int kDetailLineGap = 3;
static constexpr int kDetailLineStep = kDetailLineH + kDetailLineGap;
static constexpr int kDetailValueX = 50;
static constexpr int kDetailCol2LabelX = 138;
static constexpr int kDetailCol2ValueX = 173;

static constexpr int kInfoBoxX = 4;
static constexpr int kInfoBoxW = 232;
static constexpr int kInfoBoxRight = kInfoBoxX + kInfoBoxW;
static constexpr int kInfoBoxHeaderH = 15;
static constexpr int kInfoLines = 3;
static constexpr int kInfoContentH = kDetailLineStep * (kInfoLines - 1) + kDetailLineH;
static constexpr int kInfoBoxY = kBodyTop + 6;
static constexpr int kInfoBoxH = kInfoBoxHeaderH + kInfoContentH + 8;
static constexpr int kInfoTextY = kInfoBoxY + kInfoBoxHeaderH + 2;
static constexpr int kRemoteKeysTop = kInfoBoxY + kInfoBoxH + 10;

static constexpr int kBrowseHeaderY = kToolbarBottom + 8;
static constexpr int kBrowseHeaderH = 14;
static constexpr int kListY = kBrowseHeaderY + kBrowseHeaderH + 2;

static constexpr int kIconBackX = 10;
static constexpr int kIconDeleteX = 130;
static constexpr int kIconBrowseX = 170;
static constexpr int kIconReloadX = 210;

enum KeyId : uint8_t {
  Power = 0,
  Mute,
  VolUp,
  VolDn,
  ChUp,
  ChDn,
  Up,
  Down,
  Left,
  Right,
  Ok,
  Back,
  Settings,
  KeyCount
};

static const char* keyLabel(KeyId k) {
  switch (k) {
    case Power: return "PWR";
    case Mute:  return "MUTE";
    case VolUp: return "VOL+";
    case VolDn: return "VOL-";
    case ChUp:  return "CH+";
    case ChDn:  return "CH-";
    case Up:    return "UP";
    case Down:  return "DOWN";
    case Left:  return "LEFT";
    case Right: return "RIGHT";
    case Ok:    return "OK";
    case Back:  return "BACK";
    case Settings: return "SET";
    default:    return "?";
  }
}

struct Profile {
  String name;
  String category;
  String brand;
  String source;  // "built-in" | "sd" | "custom"
  String path;    // SD file path (empty for built-in / RAM-only)
  decode_type_t proto = decode_type_t::UNKNOWN;
  uint16_t bits = 0;
  // Keep as 64-bit so we can support protocols like RC6 (36-bit), Panasonic (48-bit),
  // Pioneer (64-bit), etc. Avoid ArduinoJson's 64-bit requirement by only parsing
  // 64-bit values from strings (not Variant::as<uint64_t>).
  uint64_t code[KeyCount]{};
  bool has[KeyCount]{};
};

static std::vector<Profile> s_profiles;
static int s_profileIdx = 0;
static bool s_uiDrawn = false;
static String s_lastErr = "";
static bool s_loadedFromSd = false;
static bool s_deleteArmed = false;
static uint32_t s_deleteArmUntilMs = 0;

static FeatureUI::Button s_keyBtns[KeyCount];

// Browser state.
enum class Screen : uint8_t { Remote, Category, Brand, Profile };
static Screen s_screen = Screen::Remote;
static std::vector<String> s_categories;
static std::vector<String> s_brands;
static std::vector<int> s_filteredProfileIdx;   
static String s_selectedCategory = "";
static String s_selectedBrand = "";
static int s_listSel = 0;
static uint32_t s_browseVersion = 0;

static const unsigned char* keyIcon(KeyId k) {
  switch (k) {
    case Power: return bitmap_icon_power;
    case Mute:  return bitmap_icon_dialog;
    case VolUp: return bitmap_icon_sort_up_plus;
    case VolDn: return bitmap_icon_sort_down_minus;
    case ChUp:  return bitmap_icon_UP;
    case ChDn:  return bitmap_icon_DOWN;
    case Up:    return bitmap_icon_UP;
    case Down:  return bitmap_icon_DOWN;
    case Left:  return bitmap_icon_LEFT;
    case Right: return bitmap_icon_RIGHT;
    case Ok:    return bitmap_icon_start;
    case Back:  return bitmap_icon_go_back;
    case Settings: return bitmap_icon_document_gear;
    default:    return nullptr;
  }
}

static void drawKeyButton(KeyId k, bool pressed = false) {
  const FeatureUI::Button& b = s_keyBtns[(int)k];
  if (b.w <= 0 || b.h <= 0) return;

  const bool disabled = b.disabled;
  const uint16_t fill = disabled ? UI_BG : (pressed ? UI_FG : UI_BG);
  const uint16_t edge = disabled ? UI_LINE : (pressed ? UI_ICON : UI_LINE);

  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, edge);

  const unsigned char* ico = keyIcon(k);
  if (ico) {
    const int iconSize = 16;
    const int ix = b.x + (b.w - iconSize) / 2;
    const int iy = b.y + (b.h - iconSize) / 2;
    tft.drawBitmap(ix, iy, ico, iconSize, iconSize, disabled ? UI_DIM_TEXT : UI_ICON);
  }
}

static String shortStatusLine(const String& s, int maxLen = 32) {
  if ((int)s.length() <= maxLen) return s;
  return s.substring(0, maxLen);
}

static String truncateWithEllipsis(const String& s, int maxPx) {
  if (maxPx <= 0 || s.length() == 0) return s;
  if ((int)tft.textWidth(s, 1) <= maxPx) return s;
  const int ellW = tft.textWidth("...", 1);
  String out = s;
  while (out.length() > 0 && (int)tft.textWidth(out, 1) + ellW > maxPx) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static String normalizeToken(const String& in) {
  String s;
  s.reserve(in.length());
  for (int i = 0; i < (int)in.length(); i++) {
    char c = in[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) s += c;
  }
  return s;
}

static bool keyFromToken(const String& tok, KeyId& out) {
  String k = normalizeToken(tok);
  if (k == "POWER" || k == "PWR") { out = Power; return true; }
  if (k == "MUTE") { out = Mute; return true; }
  if (k == "SETTINGS" || k == "SET" || k == "MENU") { out = Settings; return true; }
  if (k == "VOLUP" || k == "VOLUMEUP" || k == "VOLPLUS" || k == "VUP" || k == "VOLP") { out = VolUp; return true; }
  if (k == "VOLDN" || k == "VOLDOWN" || k == "VOLUMEDOWN" || k == "VOLMINUS" || k == "VDN" || k == "VOLM") { out = VolDn; return true; }
  if (k == "CHUP" || k == "CHANUP" || k == "CHANNELUP" || k == "CHPLUS") { out = ChUp; return true; }
  if (k == "CHDN" || k == "CHDOWN" || k == "CHANDOWN" || k == "CHANNELDOWN" || k == "CHMINUS") { out = ChDn; return true; }
  if (k == "UP") { out = Up; return true; }
  if (k == "DOWN" || k == "DN") { out = Down; return true; }
  if (k == "LEFT") { out = Left; return true; }
  if (k == "RIGHT") { out = Right; return true; }
  if (k == "OK" || k == "ENTER" || k == "SELECT") { out = Ok; return true; }
  if (k == "BACK" || k == "RETURN" || k == "EXIT") { out = Back; return true; }
  return false;
}

static bool protoFromToken(const String& tok, decode_type_t& out) {
  String p = normalizeToken(tok);
  if (p == "NEC") { out = decode_type_t::NEC; return true; }
  if (p == "NECLIKE") { out = decode_type_t::NEC_LIKE; return true; }
  if (p == "SONY" || p == "SIRC") { out = decode_type_t::SONY; return true; }
  if (p == "SAMSUNG" || p == "SAMSUNG32" || p == "SAMSUNG36") { out = decode_type_t::SAMSUNG36; return true; }
  if (p == "LG") { out = decode_type_t::LG; return true; }
  if (p == "LG2") { out = decode_type_t::LG2; return true; }
  if (p == "JVC") { out = decode_type_t::JVC; return true; }
  if (p == "DENON") { out = decode_type_t::DENON; return true; }
  if (p == "PANASONIC" || p == "KASEIKYO") { out = decode_type_t::PANASONIC; return true; }
  if (p == "RC5" || p == "RC5X") { out = decode_type_t::RC5; return true; }
  if (p == "RC6") { out = decode_type_t::RC6; return true; }
  if (p == "PIONEER") { out = decode_type_t::PIONEER; return true; }
  if (p == "DISH") { out = decode_type_t::DISH; return true; }
  if (p == "GICABLE") { out = decode_type_t::GICABLE; return true; }
  if (p == "EPSON") { out = decode_type_t::EPSON; return true; }

  // Fall back to IRremoteESP8266 name table (matches typeToString output).
  decode_type_t lib = strToDecodeType(tok.c_str());
  if (lib != decode_type_t::UNKNOWN) {
    out = lib;
    return true;
  }

  // Numeric enum id saved by Copy Controller.
  if (p.length() > 0) {
    bool allDigits = true;
    for (int i = 0; i < (int)p.length(); i++) {
      if (p[i] < '0' || p[i] > '9') { allDigits = false; break; }
    }
    if (allDigits) {
      int id = p.toInt();
      if (id > 0 && id <= (int)decode_type_t::kLastDecodeType) {
        out = (decode_type_t)id;
        return true;
      }
    }
  }

  return false;
}

// Stable token for JSON — do not use typeToString() alone (round-trip can fail).
static const char* protocolToken(decode_type_t p) {
  switch (p) {
    case decode_type_t::NEC: return "NEC";
    case decode_type_t::NEC_LIKE: return "NECLIKE";
    case decode_type_t::SONY: return "SONY";
    case decode_type_t::SAMSUNG36: return "SAMSUNG36";
    case decode_type_t::LG: return "LG";
    case decode_type_t::LG2: return "LG2";
    case decode_type_t::JVC: return "JVC";
    case decode_type_t::DENON: return "DENON";
    case decode_type_t::PANASONIC: return "PANASONIC";
    case decode_type_t::RC5: return "RC5";
    case decode_type_t::RC6: return "RC6";
    case decode_type_t::PIONEER: return "PIONEER";
    case decode_type_t::DISH: return "DISH";
    case decode_type_t::GICABLE: return "GICABLE";
    case decode_type_t::EPSON: return "EPSON";
    default: return nullptr;
  }
}

static bool resolveProtocol(JsonObject o, decode_type_t& proto) {
  if (o.containsKey("protocol_id")) {
    int id = o["protocol_id"] | 0;
    if (id > 0 && id <= (int)decode_type_t::kLastDecodeType) {
      proto = (decode_type_t)id;
      return true;
    }
  }
  const char* pr = o["protocol"] | "";
  return protoFromToken(String(pr), proto);
}

static String inferCategoryFromName(const String& name) {
  String n = name;
  n.toUpperCase();
  if (n.indexOf("TV") >= 0) return "TV";
  if (n.indexOf("AVR") >= 0 || n.indexOf("RECEIVER") >= 0 || n.indexOf("SOUNDBAR") >= 0) return "AVR";
  if (n.indexOf("PROJECTOR") >= 0) return "PROJECTOR";
  if (n.indexOf("XBOX") >= 0 || n.indexOf("PLAYSTATION") >= 0) return "GAME";
  if (n.indexOf("AC") >= 0 || n.indexOf("AIR") >= 0) return "AC";
  if (n.indexOf("CABLE") >= 0 || n.indexOf("DISH") >= 0 || n.indexOf("STB") >= 0) return "STB";
  return "OTHER";
}

static String inferBrandFromName(const String& name) {
  int par = name.indexOf('(');
  String s = (par > 0) ? name.substring(0, par) : name;
  s.trim();
  int sp = s.indexOf(' ');
  if (sp > 0) s = s.substring(0, sp);
  s.trim();
  if (!s.length()) return "UNKNOWN";
  s.toUpperCase();
  return s;
}

static void loadBuiltinProfiles() {
  s_profiles.clear();

  {
    Profile p{};
    p.name = "Samsung TV (common)";
    p.source = "built-in";
    p.proto = decode_type_t::SAMSUNG36;
    p.bits = 32;
    p.code[Power] = 0xE0E040BFu; p.has[Power] = true;
    p.code[Mute]  = 0xE0E0F00Fu; p.has[Mute]  = true;
    p.code[Settings] = 0xE0E058A7u; p.has[Settings] = true;  // menu
    p.code[VolUp] = 0xE0E0E01Fu; p.has[VolUp] = true;
    p.code[VolDn] = 0xE0E0D02Fu; p.has[VolDn] = true;
    p.code[ChUp]  = 0xE0E048B7u; p.has[ChUp]  = true;
    p.code[ChDn]  = 0xE0E008F7u; p.has[ChDn]  = true;
    p.code[Up]    = 0xE0E006F9u; p.has[Up]    = true;
    p.code[Down]  = 0xE0E08679u; p.has[Down]  = true;
    p.code[Left]  = 0xE0E0A659u; p.has[Left]  = true;
    p.code[Right] = 0xE0E046B9u; p.has[Right] = true;
    p.code[Ok]    = 0xE0E016E9u; p.has[Ok]    = true;
    p.code[Back]  = 0xE0E01AE5u; p.has[Back]  = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "LG TV (common)";
    p.source = "built-in";
    p.proto = decode_type_t::NEC;
    p.bits = 32;
    p.code[Power] = 0x20DF10EFu; p.has[Power] = true;
    p.code[Mute]  = 0x20DF906Fu; p.has[Mute]  = true;
    p.code[Settings] = 0x20DFC23Du; p.has[Settings] = true;  // menu/settings
    p.code[VolUp] = 0x20DF40BFu; p.has[VolUp] = true;
    p.code[VolDn] = 0x20DFC03Fu; p.has[VolDn] = true;
    p.code[ChUp]  = 0x20DF00FFu; p.has[ChUp]  = true;
    p.code[ChDn]  = 0x20DF807Fu; p.has[ChDn]  = true;
    p.code[Up]    = 0x20DF02FDu; p.has[Up]    = true;
    p.code[Down]  = 0x20DF827Du; p.has[Down]  = true;
    p.code[Left]  = 0x20DFE01Fu; p.has[Left]  = true;
    p.code[Right] = 0x20DF609Fu; p.has[Right] = true;
    p.code[Ok]    = 0x20DF22DDu; p.has[Ok]    = true;
    p.code[Back]  = 0x20DF14EBu; p.has[Back]  = true;
    s_profiles.push_back(p);
  }

  // Common Roku remotes (NEC address 0x5743). PWR sends Home; SET sends
  // options/subtitle (*). Other Roku address families (e.g. 0x57E3) need SD profiles.
  {
    Profile p{};
    p.name = "Roku (common)";
    p.source = "built-in";
    p.proto = decode_type_t::NEC;
    p.bits = 32;
    p.code[Power] = 0x5743C03Fu; p.has[Power] = true;  // Home
    p.code[Settings] = 0x57438679u; p.has[Settings] = true;  // * / options
    p.code[Up]    = 0x57439867u; p.has[Up]    = true;
    p.code[Down]  = 0x5743CC33u; p.has[Down]  = true;
    p.code[Left]  = 0x57437887u; p.has[Left]  = true;
    p.code[Right] = 0x5743B44Bu; p.has[Right] = true;
    p.code[Ok]    = 0x574354ABu; p.has[Ok]    = true;
    p.code[Back]  = 0x57436699u; p.has[Back]  = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Sony TV (common)";
    p.source = "built-in";
    p.proto = decode_type_t::SONY;
    p.bits = 12;
    p.code[Power] = 0x0A90u; p.has[Power] = true;
    p.code[Mute]  = 0x0290u; p.has[Mute]  = true;
    p.code[Settings] = 0x070u; p.has[Settings] = true;  // menu
    p.code[VolUp] = 0x0490u; p.has[VolUp] = true;
    p.code[VolDn] = 0x0C90u; p.has[VolDn] = true;
    p.code[ChUp]  = 0x0090u; p.has[ChUp]  = true;
    p.code[ChDn]  = 0x0890u; p.has[ChDn]  = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Xbox 360";
    p.source = "built-in";
    p.proto = decode_type_t::RC6;
    p.bits = 36;
    p.code[Power] = 0x0C800F740Cu; p.has[Power] = true;  
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Epson Projector";
    p.source = "built-in";
    p.proto = decode_type_t::EPSON;
    p.bits = 32;
    p.code[Power] = 0xC1AA09F6u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "JVC VCR";
    p.source = "built-in";
    p.proto = decode_type_t::JVC;
    p.bits = 16;
    p.code[Power] = 0xC2B8u; p.has[Power] = true; 
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Denon AVR";
    p.source = "built-in";
    p.proto = decode_type_t::DENON;
    p.bits = 15;
    p.code[Power] = 0x2278u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Denon AVR";
    p.source = "built-in";
    p.proto = decode_type_t::DENON;
    p.bits = 48;
    p.code[Power] = 0x2A4C028D6CE3ULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Panasonic";
    p.source = "built-in";
    p.proto = decode_type_t::PANASONIC;
    p.bits = 48;
    p.code[Power] = 0x40040190ED7CULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Pioneer";
    p.source = "built-in";
    p.proto = decode_type_t::PIONEER;
    p.bits = 64;
    p.code[Power] = 0x55FF00AAAA00FF55ULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "DISH";
    p.source = "built-in";
    p.proto = decode_type_t::DISH;
    p.bits = 16;
    p.code[Power] = 0x9C00u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "GI Cable";
    p.source = "built-in";
    p.proto = decode_type_t::GICABLE;
    p.bits = 16;
    p.code[Power] = 0x8807u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  s_lastErr = "Built-in profiles";
}

static bool endsWith(const String& s, const char* suf) {
  int sl = s.length();
  int tl = (int)strlen(suf);
  if (tl > sl) return false;
  return s.substring(sl - tl) == suf;
}

static bool profileAlreadyLoaded(const String& name, decode_type_t proto, const String& source) {
  for (const auto& p : s_profiles) {
    if (p.name == name && p.proto == proto && p.source == source) return true;
  }
  return false;
}

static bool parseProfilesJson(File& f, size_t docCapacity, String* errOut = nullptr) {

  if (docCapacity < 512) docCapacity = 512;
  DynamicJsonDocument doc(docCapacity);
  DeserializationError err = deserializeJson(doc, f);
  if (err) { if (errOut) *errOut = "JSON parse failed"; return false; }

  int added = 0;

  auto applySource = [](Profile& p, const char* srcField) {
    String src = String(srcField ? srcField : "");
    src.trim();
    if (src.length()) {
      p.source = src;
    } else if (p.category.equalsIgnoreCase("CUSTOM") || p.brand.equalsIgnoreCase("CUSTOM")) {
      p.source = "custom";
    } else {
      p.source = "sd";
    }
  };

  auto pushProfile = [&](Profile& p) {
    if (profileAlreadyLoaded(p.name, p.proto, p.source)) return;
    s_profiles.push_back(p);
    added++;
  };

  JsonArray arr = doc["profiles"].as<JsonArray>();
  if (!arr.isNull()) {
    for (JsonVariant v : arr) {
      JsonObject o = v.as<JsonObject>();
      if (o.isNull()) continue;

      const char* nm = o["name"] | "";
      const char* pr = o["protocol"] | "";
      const char* cat = o["category"] | "";
      const char* br  = o["brand"] | "";
      const char* src = o["source"] | "";
      decode_type_t proto = decode_type_t::UNKNOWN;
      if (!resolveProtocol(o, proto)) continue;

      Profile p{};
      p.name = String(nm);
      if (!p.name.length()) p.name = String(pr);
      p.category = String(cat);
      p.brand = String(br);
      if (!p.category.length()) p.category = inferCategoryFromName(p.name);
      if (!p.brand.length()) p.brand = inferBrandFromName(p.name);
      applySource(p, src);
      p.proto = proto;
      p.bits = (uint16_t)(o["bits"] | ((proto == decode_type_t::SONY) ? 12 : 32));

      JsonObject codes = o["codes"].as<JsonObject>();
      if (!codes.isNull()) {
        for (JsonPair kv : codes) {
          String keyName = String(kv.key().c_str());
          KeyId kid;
          if (!keyFromToken(keyName, kid)) continue;

          const char* valStr = kv.value().as<const char*>();
          uint64_t code = 0;
          bool ok = false;
          if (valStr && strlen(valStr)) {
            String s(valStr);
            s.trim();
            if (s.startsWith("0x") || s.startsWith("0X")) {
              code = strtoull(s.c_str() + 2, nullptr, 16);
              ok = true;
            } else {
              code = strtoull(s.c_str(), nullptr, 10);
              ok = true;
            }
          } else if (kv.value().is<uint32_t>()) {
            code = (uint64_t)kv.value().as<uint32_t>();
            ok = true;
          } else if (kv.value().is<uint16_t>()) {
            code = (uint64_t)kv.value().as<uint16_t>();
            ok = true;
          } else if (kv.value().is<uint8_t>()) {
            code = (uint64_t)kv.value().as<uint8_t>();
            ok = true;
          }
          if (!ok) continue;

          p.code[kid] = code;
          p.has[kid] = true;
        }
      }

      pushProfile(p);
      if ((int)s_profiles.size() >= (int)kMaxProfiles) break;
    }
    if (added == 0) { if (errOut) *errOut = "No profiles in JSON array"; return false; }
    return true;
  }

  JsonObject o = doc.as<JsonObject>();
  if (!o.isNull() && (o.containsKey("name") || o.containsKey("protocol"))) {
    const char* nm = o["name"] | "";
    const char* cat = o["category"] | "";
    const char* br  = o["brand"] | "";
    const char* src = o["source"] | "";
    decode_type_t proto = decode_type_t::UNKNOWN;
    if (!resolveProtocol(o, proto)) { if (errOut) *errOut = "Bad protocol"; return false; }

    Profile p{};
    p.name = String(nm);
    if (!p.name.length()) {
      const char* pr = o["protocol"] | "";
      p.name = String(pr);
    }
    p.category = String(cat);
    p.brand = String(br);
    if (!p.category.length()) p.category = inferCategoryFromName(p.name);
    if (!p.brand.length()) p.brand = inferBrandFromName(p.name);
    applySource(p, src);
    p.proto = proto;
    p.bits = (uint16_t)(o["bits"] | ((proto == decode_type_t::SONY) ? 12 : 32));

    JsonObject codes = o["codes"].as<JsonObject>();
    if (!codes.isNull()) {
      for (JsonPair kv : codes) {
        String keyName = String(kv.key().c_str());
        KeyId kid;
        if (!keyFromToken(keyName, kid)) continue;
        const char* valStr = kv.value().as<const char*>();
        if (!valStr || !strlen(valStr)) continue;
        String s(valStr);
        s.trim();
        uint64_t code = 0;
        if (s.startsWith("0x") || s.startsWith("0X")) code = strtoull(s.c_str() + 2, nullptr, 16);
        else code = strtoull(s.c_str(), nullptr, 10);
        p.code[kid] = code;
        p.has[kid] = true;
      }
    }
    pushProfile(p);
    return added > 0;
  }

  if (errOut) *errOut = "No profiles";
  return false;
}

static bool loadProfilesFromSd(String* errOut = nullptr) {
  // Appends SD profiles onto whatever is already in s_profiles (builtins).
  restoreSdAfterSharedSpi();
  if (!isSDCardAvailable()) { if (errOut) *errOut = "SD not available"; return false; }

  const size_t before = s_profiles.size();
  uint16_t loadedFiles = 0;
  String lastParseErr;

  auto loadOnePath = [&](const String& path, size_t capacity = 4096) {
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    // Skip empty files.
    if (f.size() == 0) { f.close(); return; }
    String perr;
    const size_t beforeFile = s_profiles.size();
    if (parseProfilesJson(f, capacity, &perr)) {
      if (s_profiles.size() > beforeFile) {
        loadedFiles++;
        for (size_t i = beforeFile; i < s_profiles.size(); i++) {
          if (!s_profiles[i].path.length()) s_profiles[i].path = path;
        }
      }
    } else if (perr.length()) {
      lastParseErr = perr + "@" + path;
    }
    f.close();
  };

  auto scanDirForJson = [&](const char* dirPath) {
    File d = SD.open(dirPath);
    if (!d) return;
    if (!d.isDirectory()) { d.close(); return; }
    for (;;) {
      File entry = d.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String name = String(entry.name());
        String base = name;
        int slash = name.lastIndexOf('/');
        if (slash >= 0) base = name.substring(slash + 1);
        // Copy Controller marker files are loaded explicitly; still allow other
        // library JSON in the same folder.
        if (endsWith(base, ".json") || endsWith(base, ".JSON")) {
          String full = name;
          if (!name.startsWith("/")) {
            String root = String(dirPath);
            if (root.length() && root[0] != '/') root = "/" + root;
            if (!root.endsWith("/")) root += "/";
            full = root + base;
          }
          entry.close();
          loadOnePath(full);
          if ((int)s_profiles.size() >= (int)kMaxProfiles) break;
          continue;
        }
      }
      entry.close();
    }
    d.close();
  };

  // 1) User-managed multi-profile library (must load first, never overwritten by Copy).
  loadOnePath(String(PROFILES_PATH), 16384);

  // 2) Profile folders.
  scanDirForJson(PROFILES_DIR);
  scanDirForJson("/ir_profiles");
  scanDirForJson("/profiles");
  scanDirForJson("profiles");

  // 3) /ir folder (Record + Copy Controller + any hand-placed JSON).
  scanDirForJson("/ir");
  scanDirForJson("ir");

  // 4) Explicit Copy Controller fallbacks (deduped if already loaded via scan).
  loadOnePath(String(UNIV_CUSTOM_PATH));
  loadOnePath(String(UNIV_CUSTOM_ROOT));
  loadOnePath(String("/irc.json"));

  // 5) Root-level custom files.
  {
    File root = SD.open("/");
    if (root) {
      for (;;) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
          String name = String(entry.name());
          String base = name;
          int slash = name.lastIndexOf('/');
          if (slash >= 0) base = name.substring(slash + 1);
          const bool cust =
              base.indexOf("ir_cust_") >= 0 ||
              base.equalsIgnoreCase("irc.json") ||
              base.equalsIgnoreCase("univ_custom.json") ||
              base.startsWith("prof_");
          if (cust && (endsWith(base, ".json") || endsWith(base, ".JSON"))) {
            String full = name.startsWith("/") ? name : (String("/") + base);
            entry.close();
            loadOnePath(full);
            if ((int)s_profiles.size() >= (int)kMaxProfiles) break;
            continue;
          }
        }
        entry.close();
      }
      root.close();
    }
  }

  if (s_profiles.size() <= before) {
    if (errOut) {
      if (lastParseErr.length()) *errOut = lastParseErr;
      else *errOut = "No SD profiles found";
    }
    return false;
  }
  if (errOut) {
    *errOut = String("+") + String((int)(s_profiles.size() - before)) +
              " from SD (" + String(loadedFiles) + " files)";
  }
  return true;
}

static void mergeRamCustomProfiles() {
  for (int i = 0; i < (int)s_ramCustomCount; i++) {
    const IrRamCustomProfile& c = s_ramCustoms[i];
    if (!c.used || !c.name[0]) continue;

    // Skip if already present (same name + custom).
    bool exists = false;
    for (const auto& p : s_profiles) {
      if (p.name.equals(c.name) &&
          (p.source == "custom" || p.category.equalsIgnoreCase("CUSTOM"))) {
        exists = true;
        break;
      }
    }
    if (exists) continue;
    if ((int)s_profiles.size() >= (int)kMaxProfiles) break;

    Profile p{};
    p.name = String(c.name);
    p.category = "CUSTOM";
    p.brand = "CUSTOM";
    p.source = "custom";
    p.proto = c.proto;
    p.bits = c.bits;
    const int n = (KeyCount < kIrCustomKeySlots) ? (int)KeyCount : kIrCustomKeySlots;
    for (int k = 0; k < n; k++) {
      p.has[k] = c.has[k];
      p.code[k] = c.code[k];
    }
    s_profiles.push_back(p);
  }
}

static void refreshProfiles() {
  s_lastErr = "";
  s_loadedFromSd = false;

  loadBuiltinProfiles();

  String err;
  if (loadProfilesFromSd(&err)) {
    s_loadedFromSd = true;
  } else if (err.length()) {
    s_lastErr = err;
  }

  mergeRamCustomProfiles();

  int customCount = 0;
  int sdCount = 0;
  for (const auto& p : s_profiles) {
    if (p.source == "custom" || p.category.equalsIgnoreCase("CUSTOM")) customCount++;
    else if (p.source == "sd") sdCount++;
  }

  if (sdCount > 0 || customCount > 0) {
    s_loadedFromSd = true;
    s_lastErr = String("SD ") + String(sdCount) + " + Cust " + String(customCount);
  } else if (s_lastErr.length()) {
    s_lastErr = "Built-in (SD: " + s_lastErr + ")";
  } else {
    s_lastErr = "Built-in";
  }

  // Start on first custom if any; otherwise keep index 0 (builtins / SD).
  s_profileIdx = 0;
  for (int i = 0; i < (int)s_profiles.size(); i++) {
    if (s_profiles[i].source == "custom" ||
        s_profiles[i].category.equalsIgnoreCase("CUSTOM")) {
      s_profileIdx = i;
      break;
    }
  }
  if (s_profileIdx < 0) s_profileIdx = 0;
  if (s_profileIdx >= (int)s_profiles.size()) s_profileIdx = 0;
}

static void browseBack();
static void openCategoryBrowser();
static void reloadUniversalProfiles();
static void deleteCurrentProfile();

static void drawToolbar() {
  tft.fillRect(0, kToolbarY, 240, kToolbarH, UI_FG);
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  tft.drawBitmap(kIconBackX, kToolbarY, bitmap_icon_go_back, kIconSize, kIconSize, UI_ICON);

  if (s_screen == Screen::Remote) {
    tft.drawBitmap(kIconDeleteX, kToolbarY, bitmap_icon_recycle, kIconSize, kIconSize, UI_ICON);
    tft.drawBitmap(kIconBrowseX, kToolbarY, bitmap_icon_list, kIconSize, kIconSize, UI_ICON);
    tft.drawBitmap(kIconReloadX, kToolbarY, bitmap_icon_undo, kIconSize, kIconSize, UI_ICON);
  }

  tft.drawFastHLine(0, kToolbarBottom, 240, UI_LINE);
}

static void drawInfoPanel() {
  const int panelH = kRemoteKeysTop - kBodyTop;
  tft.fillRect(0, kBodyTop, 240, panelH, FEATURE_BG);

  tft.drawRoundRect(kInfoBoxX, kInfoBoxY, kInfoBoxW, kInfoBoxH, 3, UI_LINE);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.drawString("Profile", kInfoBoxX + 4, kInfoBoxY + 3);

  if (s_profiles.empty()) {
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.setCursor(kPadX, kInfoTextY);
    tft.print(shortStatusLine(s_lastErr.length() ? s_lastErr : "No profiles", 32));
    return;
  }

  const Profile& p = s_profiles[s_profileIdx];

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoTextY);
  tft.print("Name:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(kDetailValueX, kInfoTextY);
  const int nameMaxW = kDetailCol2LabelX - kDetailValueX - 4;
  tft.print(truncateWithEllipsis(p.name, nameMaxW));
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kDetailCol2LabelX, kInfoTextY);
  tft.print("Idx:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(kDetailCol2ValueX, kInfoTextY);
  tft.printf("%d/%d", s_profileIdx + 1, (int)s_profiles.size());

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoTextY + kDetailLineStep);
  tft.print("Type:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(kDetailValueX, kInfoTextY + kDetailLineStep);
  const int typeMaxW = kDetailCol2LabelX - kDetailValueX - 4;
  tft.print(truncateWithEllipsis(String(typeToString(p.proto)), typeMaxW));
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kDetailCol2LabelX, kInfoTextY + kDetailLineStep);
  tft.print("Bits:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(kDetailCol2ValueX, kInfoTextY + kDetailLineStep);
  tft.print((unsigned)p.bits);

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoTextY + (kDetailLineStep * 2));
  tft.print("Src:");
  tft.setCursor(kDetailValueX, kInfoTextY + (kDetailLineStep * 2));
  if (s_deleteArmed && (int32_t)(millis() - s_deleteArmUntilMs) < 0) {
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.print("Del? tap again");
  } else if (p.source == "built-in" || (!p.source.length() && !s_loadedFromSd)) {
    tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    tft.print("Built-in");
  } else if (p.source == "custom" || p.category.equalsIgnoreCase("CUSTOM")) {
    tft.setTextColor(UI_OK, FEATURE_BG);
    tft.print("Custom");
  } else {
    tft.setTextColor(UI_OK, FEATURE_BG);
    tft.print("SD card");
  }
}

static void drawBrowseHeader() {
  tft.fillRect(0, kBodyTop, 240, kListY - kBodyTop, FEATURE_BG);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(kPadX, kBrowseHeaderY);
  if (s_screen == Screen::Category) {
    tft.print("Select Category");
  } else if (s_screen == Screen::Brand) {
    tft.print("Brand:");
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.setCursor(kDetailValueX, kBrowseHeaderY);
    String cat = s_selectedCategory.length() ? s_selectedCategory : "ALL";
    if (cat.length() > 18) cat = cat.substring(0, 18);
    tft.print(cat);
  } else if (s_screen == Screen::Profile) {
    tft.print("Profile:");
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.setCursor(kDetailValueX, kBrowseHeaderY);
    String br = s_selectedBrand.length() ? s_selectedBrand : "ALL";
    if (br.length() > 18) br = br.substring(0, 18);
    tft.print(br);
  }
  tft.drawFastHLine(kPadX, kListY - 1, kListW, UI_LINE);
}

static bool handleToolbarTouch(int x, int y) {
  if (y <= kToolbarY || y >= kToolbarBottom) return false;

  if (x > kIconBackX && x < (kIconBackX + kIconSize)) {
    if (s_screen == Screen::Remote) {
      feature_exit_requested = true;
    } else {
      browseBack();
      s_uiDrawn = false;
    }
    return true;
  }

  if (s_screen != Screen::Remote) return false;

  if (x > kIconDeleteX && x < (kIconDeleteX + kIconSize)) {
    deleteCurrentProfile();
    return true;
  }
  if (x > kIconBrowseX && x < (kIconBrowseX + kIconSize)) {
    openCategoryBrowser();
    return true;
  }
  if (x > kIconReloadX && x < (kIconReloadX + kIconSize)) {
    reloadUniversalProfiles();
    return true;
  }
  return false;
}

static void rebuildCategoriesAndBrands() {
  s_categories.clear();
  s_brands.clear();
  if (s_profiles.empty()) return;

  auto addUnique = [](std::vector<String>& v, const String& s) {
    for (auto& x : v) if (x == s) return;
    v.push_back(s);
  };

  for (auto& p : s_profiles) {
    addUnique(s_categories, p.category.length() ? p.category : String("OTHER"));
  }
  std::sort(s_categories.begin(), s_categories.end());
  s_browseVersion++;
}

static void rebuildBrandsForCategory(const String& cat) {
  s_brands.clear();
  if (s_profiles.empty()) return;
  auto addUnique = [](std::vector<String>& v, const String& s) {
    for (auto& x : v) if (x == s) return;
    v.push_back(s);
  };
  addUnique(s_brands, "ALL");
  for (auto& p : s_profiles) {
    if (p.category == cat) addUnique(s_brands, p.brand.length() ? p.brand : String("UNKNOWN"));
  }
  std::sort(s_brands.begin() + 1, s_brands.end());
  s_browseVersion++;
}

static void rebuildFilteredProfiles() {
  s_filteredProfileIdx.clear();
  if (s_profiles.empty()) return;

  for (int i = 0; i < (int)s_profiles.size(); i++) {
    const Profile& p = s_profiles[i];
    if (s_selectedCategory.length() && p.category != s_selectedCategory) continue;
    if (s_selectedBrand.length() && s_selectedBrand != "ALL" && p.brand != s_selectedBrand) continue;
    s_filteredProfileIdx.push_back(i);
  }
  s_browseVersion++;
}

static constexpr int LIST_ROW_H = 18;

static int listTopY() { return kListY; }
static int universalContentBottom() { return irContentBottom(); }
static int listRowsVisible() {
  int h = universalContentBottom() - listTopY() - 4;
  int rows = h / LIST_ROW_H;
  if (rows < 4) rows = 4;
  if (rows > 10) rows = 10;
  return rows;
}

static void drawListRow(int y, const String& left, const String& right, bool selected) {
  uint16_t bg = selected ? UI_FG : FEATURE_BG;
  uint16_t fg = selected ? UI_ICON : UI_TEXT;
  tft.fillRect(kPadX, y, kListW, LIST_ROW_H - 1, bg);
  tft.setTextFont(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(kPadX, y + 4);
  tft.print(truncateWithEllipsis(left, kListW - 4));
  if (right.length()) {
    int tw = tft.textWidth(right, 1);
    tft.setCursor(kPadX + kListW - tw, y + 4);
    tft.print(right);
  }
}

static void drawListScreen(const std::vector<String>& items) {
  static Screen s_lastListScreen = Screen::Remote;
  static int s_lastSel = -1;
  static int s_lastPageStart = -1;
  static uint32_t s_lastBrowseVersion = 0;

  const int top = listTopY();
  const int rows = listRowsVisible();
  const int total = (int)items.size();
  if (s_listSel < 0) s_listSel = 0;
  if (total > 0 && s_listSel >= total) s_listSel = total - 1;

  int pageStart = (rows > 0) ? ((s_listSel / rows) * rows) : 0;
  const bool needFull =
      (s_lastListScreen != s_screen) ||
      (s_lastBrowseVersion != s_browseVersion) ||
      (s_lastPageStart != pageStart) ||
      (s_lastSel < 0);

  if (needFull) {
    tft.fillRect(0, listTopY(), 240, universalContentBottom() - listTopY(), FEATURE_BG);
    for (int r = 0; r < rows; r++) {
      int idx = pageStart + r;
      if (idx >= total) break;
      drawListRow(top + r * LIST_ROW_H, items[idx], "", idx == s_listSel);
    }
  } else {
    int oldSel = s_lastSel;
    int newSel = s_listSel;
    int oldRow = oldSel - pageStart;
    int newRow = newSel - pageStart;
    if (oldRow >= 0 && oldRow < rows && oldSel >= 0 && oldSel < total) {
      drawListRow(top + oldRow * LIST_ROW_H, items[oldSel], "", false);
    }
    if (newRow >= 0 && newRow < rows && newSel >= 0 && newSel < total) {
      drawListRow(top + newRow * LIST_ROW_H, items[newSel], "", true);
    }
  }

  s_lastListScreen = s_screen;
  s_lastSel = s_listSel;
  s_lastPageStart = pageStart;
  s_lastBrowseVersion = s_browseVersion;
}

static int browseItemCount() {
  if (s_screen == Screen::Category) return (int)s_categories.size();
  if (s_screen == Screen::Brand) return (int)s_brands.size();
  if (s_screen == Screen::Profile) return (int)s_filteredProfileIdx.size();
  return 0;
}

static std::vector<String> profileBrowseNames() {
  std::vector<String> names;
  names.reserve(s_filteredProfileIdx.size());
  for (int idx : s_filteredProfileIdx) {
    if (idx < 0 || idx >= (int)s_profiles.size()) continue;
    names.push_back(s_profiles[idx].name);
  }
  return names;
}

static void redrawBrowseListOnly() {
  if (s_screen == Screen::Category) drawListScreen(s_categories);
  else if (s_screen == Screen::Brand) drawListScreen(s_brands);
  else if (s_screen == Screen::Profile) drawListScreen(profileBrowseNames());
}

static void bumpBrowseSelection(int delta) {
  const int total = browseItemCount();
  if (total <= 0) return;
  s_listSel += delta;
  if (s_listSel < 0) s_listSel = 0;
  if (s_listSel >= total) s_listSel = total - 1;
  redrawBrowseListOnly();
}

static void layoutKeyButtons() {
  const int topY = kRemoteKeysTop;
  const int bottomY = universalContentBottom() - 6;

  const int gap = 4;
  const int topBtnH = 34;
  const int topBtnW = (kInfoBoxW - 3 * gap) / 4;
  const int yTop = topY;
  const int x0 = kInfoBoxX;
  const int x1 = x0 + topBtnW + gap;
  const int x2 = x1 + topBtnW + gap;
  const int x3 = kInfoBoxRight - topBtnW;
  s_keyBtns[(int)Power] = {(int16_t)x0,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Power),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Settings] = {(int16_t)x1,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Settings), FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Mute]  = {(int16_t)x2,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Mute), FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Back]  = {(int16_t)x3,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Back), FeatureUI::ButtonStyle::Secondary,true};

  const int yMidTop = yTop + topBtnH + 8;
  const int midH = bottomY - yMidTop;
  const int rockerW = 54;
  const int rockerH = 72;
  const int dpadSize = 92;
  const int dpadX = (240 - dpadSize) / 2;
  const int dpadY = yMidTop + (midH - dpadSize) / 2;

  const int volX = kInfoBoxX;
  const int chX = kInfoBoxRight - rockerW;
  const int rockerY = yMidTop + (midH - rockerH) / 2;
  s_keyBtns[(int)VolUp] = {(int16_t)volX,(int16_t)rockerY,(int16_t)rockerW,(int16_t)(rockerH/2 - 2),keyLabel(VolUp),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)VolDn] = {(int16_t)volX,(int16_t)(rockerY + rockerH/2 + 2),(int16_t)rockerW,(int16_t)(rockerH/2 - 2),keyLabel(VolDn),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)ChUp]  = {(int16_t)chX,(int16_t)rockerY,(int16_t)rockerW,(int16_t)(rockerH/2 - 2),keyLabel(ChUp),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)ChDn]  = {(int16_t)chX,(int16_t)(rockerY + rockerH/2 + 2),(int16_t)rockerW,(int16_t)(rockerH/2 - 2),keyLabel(ChDn),FeatureUI::ButtonStyle::Secondary,true};

  const int unit = 28;
  const int okSize = 38;
  const int okX = dpadX + (dpadSize - okSize) / 2;
  const int okY = dpadY + (dpadSize - okSize) / 2;
  s_keyBtns[(int)Ok]    = {(int16_t)okX,(int16_t)okY,(int16_t)okSize,(int16_t)okSize,keyLabel(Ok),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Up]    = {(int16_t)(dpadX + (dpadSize - unit)/2),(int16_t)dpadY,(int16_t)unit,(int16_t)unit,keyLabel(Up),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Down]  = {(int16_t)(dpadX + (dpadSize - unit)/2),(int16_t)(dpadY + dpadSize - unit),(int16_t)unit,(int16_t)unit,keyLabel(Down),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Left]  = {(int16_t)dpadX,(int16_t)(dpadY + (dpadSize - unit)/2),(int16_t)unit,(int16_t)unit,keyLabel(Left),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Right] = {(int16_t)(dpadX + dpadSize - unit),(int16_t)(dpadY + (dpadSize - unit)/2),(int16_t)unit,(int16_t)unit,keyLabel(Right),FeatureUI::ButtonStyle::Secondary,true};
}

static void drawKeys() {
  layoutKeyButtons();
  tft.fillRect(0, kRemoteKeysTop, 240, universalContentBottom() - kRemoteKeysTop, FEATURE_BG);

  if (s_profiles.empty()) {
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, FEATURE_BG);
    tft.setCursor(kPadX, kRemoteKeysTop + 12);
    tft.print("Add SD profiles:");
    tft.setCursor(kPadX, kRemoteKeysTop + 26);
    tft.print("/ir_profiles.json");
    tft.setCursor(kPadX, kRemoteKeysTop + 40);
    tft.print("or /ir_profiles/*.json");
    return;
  }

  const Profile& p = s_profiles[s_profileIdx];
  for (int i = 0; i < (int)KeyCount; i++) {
    s_keyBtns[i].disabled = !p.has[i];
  }
  for (int i = 0; i < (int)KeyCount; i++) {
    drawKeyButton((KeyId)i, false);
  }
}

static void drawFooter() {
  if (s_screen == Screen::Remote) {
    irSetUniversalRemoteNavLabels();
  } else {
    irSetUniversalBrowseNavLabels();
  }
  irRedrawNavChrome();
}

static void drawAll() {
  irClearBody(FEATURE_BG);
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  drawToolbar();

  if (s_screen == Screen::Remote) {
    drawInfoPanel();
    drawKeys();
    drawFooter();
    s_uiDrawn = true;
    return;
  }

  drawBrowseHeader();
  redrawBrowseListOnly();
  drawFooter();
  s_uiDrawn = true;
}

static void sendKey(KeyId k) {
  if (s_profiles.empty()) return;
  const Profile& p = s_profiles[s_profileIdx];
  if (!p.has[k]) {
    showNotification("IR", "Key not available in this profile");
    return;
  }

  s_send.begin();
  uint16_t repeat = 0;
  if (p.proto == decode_type_t::SONY || p.proto == decode_type_t::SONY_38K) repeat = 2;

  if (!s_send.send(p.proto, p.code[k], p.bits, repeat)) {
    showNotification("IR", "Protocol not enabled in build");
  }
}

static void changeProfile(int delta) {
  if (s_profiles.empty()) return;
  s_deleteArmed = false;
  int n = (int)s_profiles.size();
  s_profileIdx = (s_profileIdx + delta) % n;
  if (s_profileIdx < 0) s_profileIdx += n;
  drawInfoPanel();
  drawKeys();
}

static void openCategoryBrowser() {
  s_deleteArmed = false;
  s_screen = Screen::Category;
  s_listSel = 0;
  rebuildCategoriesAndBrands();
  s_uiDrawn = false;
}

static void reloadUniversalProfiles() {
  s_deleteArmed = false;
  refreshProfiles();
  rebuildCategoriesAndBrands();
  s_uiDrawn = false;
}

static void irRamForgetByName(const char* name) {
  if (!name || !name[0]) return;
  int w = 0;
  for (int i = 0; i < (int)s_ramCustomCount; i++) {
    if (!s_ramCustoms[i].used) continue;
    if (strncmp(s_ramCustoms[i].name, name, sizeof(s_ramCustoms[i].name)) == 0) {
      continue;  // drop
    }
    if (w != i) s_ramCustoms[w] = s_ramCustoms[i];
    w++;
  }
  for (int i = w; i < kIrRamCustomMax; i++) s_ramCustoms[i] = IrRamCustomProfile{};
  s_ramCustomCount = (uint8_t)w;
}

static bool rewriteAggregateWithoutProfile(const String& path, const Profile& victim) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  DynamicJsonDocument out(16384);
  JsonArray outArr = out.createNestedArray("profiles");

  auto keepObj = [&](JsonObject o) {
    if (o.isNull()) return;
    const char* nm = o["name"] | "";
    const char* pr = o["protocol"] | "";
    decode_type_t proto = decode_type_t::UNKNOWN;
    resolveProtocol(o, proto);
    if (String(nm) == victim.name && proto == victim.proto) return;
    outArr.add(o);
  };

  JsonArray arr = doc["profiles"].as<JsonArray>();
  if (!arr.isNull()) {
    for (JsonVariant v : arr) keepObj(v.as<JsonObject>());
  } else {
    JsonObject o = doc.as<JsonObject>();
    if (!o.isNull() && (o.containsKey("name") || o.containsKey("protocol"))) {
      keepObj(o);
    }
  }

  if (SD.exists(path)) SD.remove(path);
  File w = SD.open(path, FILE_WRITE);
  if (!w) w = SD.open(path.c_str(), "w");
  if (!w) return false;
  if (outArr.size() == 0) {
    // Nothing left — remove empty library file.
    w.close();
    SD.remove(path);
    return true;
  }
  const size_t n = serializeJson(out, w);
  w.flush();
  w.close();
  return n > 0;
}

static void deleteCurrentProfile() {
  if (s_profiles.empty() || s_profileIdx < 0 || s_profileIdx >= (int)s_profiles.size()) {
    showNotification("IR", "No profile selected");
    return;
  }

  const Profile victim = s_profiles[s_profileIdx];
  if (victim.source == "built-in") {
    showNotification("IR", "Can't delete built-in");
    s_deleteArmed = false;
    drawInfoPanel();
    return;
  }

  const uint32_t now = millis();
  if (!s_deleteArmed || (int32_t)(now - s_deleteArmUntilMs) >= 0) {
    s_deleteArmed = true;
    s_deleteArmUntilMs = now + 4000;
    drawInfoPanel();
    return;
  }
  s_deleteArmed = false;

  restoreSdAfterSharedSpi();

  bool sdOk = true;
  if (victim.path.length()) {
    int samePath = 0;
    for (const auto& p : s_profiles) {
      if (p.path == victim.path) samePath++;
    }

    if (samePath > 1 || victim.path == String(PROFILES_PATH)) {
      sdOk = rewriteAggregateWithoutProfile(victim.path, victim);
      if (!sdOk) {
        showNotification("IR", "SD update failed");
        drawInfoPanel();
        return;
      }
    } else if (isSDCardAvailable()) {
      if (SD.exists(victim.path) && !SD.remove(victim.path)) {
        showNotification("IR", "SD delete failed");
        drawInfoPanel();
        return;
      }
    }
  }

  irRamForgetByName(victim.name.c_str());

  s_profiles.erase(s_profiles.begin() + s_profileIdx);
  if (s_profiles.empty()) {
    s_profileIdx = 0;
  } else if (s_profileIdx >= (int)s_profiles.size()) {
    s_profileIdx = (int)s_profiles.size() - 1;
  }

  rebuildCategoriesAndBrands();
  showNotification("IR", sdOk ? "Profile deleted" : "Removed (SD issue)");
  s_uiDrawn = false;
}

static void browseBack() {
  if (s_screen == Screen::Category) s_screen = Screen::Remote;
  else if (s_screen == Screen::Brand) s_screen = Screen::Category;
  else if (s_screen == Screen::Profile) s_screen = Screen::Brand;
  s_uiDrawn = false;
}

static void browseSelect() {
  if (s_screen == Screen::Category) {
    if (s_categories.empty()) return;
    s_selectedCategory = s_categories[std::max(0, std::min(s_listSel, (int)s_categories.size() - 1))];
    rebuildBrandsForCategory(s_selectedCategory);
    s_screen = Screen::Brand;
    s_listSel = 0;
    s_uiDrawn = false;
  } else if (s_screen == Screen::Brand) {
    if (s_brands.empty()) return;
    s_selectedBrand = s_brands[std::max(0, std::min(s_listSel, (int)s_brands.size() - 1))];
    rebuildFilteredProfiles();
    s_screen = Screen::Profile;
    s_listSel = 0;
    s_uiDrawn = false;
  } else if (s_screen == Screen::Profile) {
    if (s_filteredProfileIdx.empty()) return;
    int idx = s_filteredProfileIdx[std::max(0, std::min(s_listSel, (int)s_filteredProfileIdx.size() - 1))];
    if (idx >= 0 && idx < (int)s_profiles.size()) {
      s_profileIdx = idx;
      s_screen = Screen::Remote;
      s_uiDrawn = false;
    }
  }
}

static void handleUniversalNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (s_screen == Screen::Remote) {
    if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
      changeProfile(-1);
    }
    if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
      deleteCurrentProfile();
    }
    if (isTouchNavButtonPressedEdge(BTN_UP)) {
      reloadUniversalProfiles();
    }
    if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
      changeProfile(+1);
    }
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    browseBack();
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    bumpBrowseSelection(1);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    bumpBrowseSelection(-1);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    browseSelect();
  }
}

void setup() {
  setTouchButtonInputEnabled(true);
  irSetUniversalRemoteNavLabels();
  s_send.begin();
  s_profileIdx = 0;
  s_uiDrawn = false;
  s_screen = Screen::Remote;
  refreshProfiles();
  rebuildCategoriesAndBrands();
  drawAll();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  if (isNotificationVisible()) {
    maintainTouchNavBar();
    int nx, ny;
    bool dismissed = false;
    if (readTouchXY(nx, ny)) {
      dismissed = (notificationHandleTouch(nx, ny) != NotificationAction::None);
    } else if (isPhysicalButtonPressed(BTN_SELECT) ||
               isTouchNavButtonPressedEdge(BTN_SELECT) ||
               isTouchNavButtonPressedEdge(BTN_LEFT)) {
      hideNotification();
      dismissed = true;
    }
    if (dismissed) {
      s_uiDrawn = false;
    }
    delay(10);
    return;
  }

  if (!s_uiDrawn) drawAll();
  maintainTouchNavBar();
  handleUniversalNavButtons();

  if (s_screen == Screen::Remote && s_deleteArmed &&
      (int32_t)(millis() - s_deleteArmUntilMs) >= 0) {
    s_deleteArmed = false;
    drawInfoPanel();
  }

  const uint32_t now = millis();
  static uint32_t lastBtnMs = 0;
  static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;
  const bool leftNow  = isPhysicalButtonPressed(BTN_LEFT);
  const bool rightNow = isPhysicalButtonPressed(BTN_RIGHT);
  const bool upNow    = isPhysicalButtonPressed(BTN_UP);
  const bool downNow  = isPhysicalButtonPressed(BTN_DOWN);
  constexpr uint32_t kBtnDebounceMs = 320;
  if ((uint32_t)(now - lastBtnMs) > kBtnDebounceMs) {
    if (s_screen == Screen::Remote) {
      if (leftNow && !prevLeft) { changeProfile(-1); lastBtnMs = now; }
      else if (rightNow && !prevRight) { changeProfile(+1); lastBtnMs = now; }
      else if (upNow && !prevUp) { sendKey(VolUp); lastBtnMs = now; }
      else if (downNow && !prevDown) { sendKey(VolDn); lastBtnMs = now; }
    } else {
      if (upNow && !prevUp) { bumpBrowseSelection(-1); lastBtnMs = now; }
      else if (downNow && !prevDown) { bumpBrowseSelection(1); lastBtnMs = now; }
      else if (leftNow && !prevLeft) {
        if (s_screen == Screen::Category) { s_screen = Screen::Remote; }
        else if (s_screen == Screen::Brand) { s_screen = Screen::Category; }
        else if (s_screen == Screen::Profile) { s_screen = Screen::Brand; }
        s_uiDrawn = false; lastBtnMs = now;
      } else if (rightNow && !prevRight) {
        if (s_screen == Screen::Category) {
          if (!s_categories.empty()) {
            s_selectedCategory = s_categories[std::max(0, std::min(s_listSel, (int)s_categories.size()-1))];
            rebuildBrandsForCategory(s_selectedCategory);
            s_screen = Screen::Brand;
            s_listSel = 0;
            s_uiDrawn = false;
            lastBtnMs = now;
          }
        } else if (s_screen == Screen::Brand) {
          if (!s_brands.empty()) {
            s_selectedBrand = s_brands[std::max(0, std::min(s_listSel, (int)s_brands.size()-1))];
            rebuildFilteredProfiles();
            s_screen = Screen::Profile;
            s_listSel = 0;
            s_uiDrawn = false;
            lastBtnMs = now;
          }
        } else if (s_screen == Screen::Profile) {
          if (!s_filteredProfileIdx.empty()) {
            int idx = s_filteredProfileIdx[std::max(0, std::min(s_listSel, (int)s_filteredProfileIdx.size()-1))];
            if (idx >= 0 && idx < (int)s_profiles.size()) {
              s_profileIdx = idx;
              s_screen = Screen::Remote;
              s_uiDrawn = false;
              lastBtnMs = now;
            }
          }
        }
      }
    }
  }
  prevLeft = leftNow;
  prevRight = rightNow;
  prevUp = upNow;
  prevDown = downNow;

  static uint32_t lastTouchMs = 0;
  static bool touchWasDown = false;
  const bool touchNow = isTouchDownDismiss();
  constexpr uint32_t kTouchDebounceMs = 320;
  if (touchNow && !touchWasDown) {
    int x, y;
    touchWasDown = true;
    if (!readTouchXY(x, y)) return;
    if ((uint32_t)(now - lastTouchMs) < kTouchDebounceMs) return;
    lastTouchMs = now;

    if (handleToolbarTouch(x, y)) return;

    if (s_screen == Screen::Remote) {
      int k = FeatureUI::hit(s_keyBtns, (int)KeyCount, x, y);
      if (k >= 0) {
        if (!s_keyBtns[k].disabled) {
          drawKeyButton((KeyId)k, true);
          sendKey((KeyId)k);
          drawKeyButton((KeyId)k, false);
        }
        return;
      }
    } else {

      const int top = listTopY();
      const int rows = listRowsVisible();
      if (y >= top && y < top + rows * LIST_ROW_H) {
        int row = (y - top) / LIST_ROW_H;
        int pageStart = (rows > 0) ? ((s_listSel / rows) * rows) : 0;
        int idx = pageStart + row;
        int max = 0;
        if (s_screen == Screen::Category) max = (int)s_categories.size();
        else if (s_screen == Screen::Brand) max = (int)s_brands.size();
        else if (s_screen == Screen::Profile) max = (int)s_filteredProfileIdx.size();
        if (idx >= 0 && idx < max) {
          s_listSel = idx;
          redrawBrowseListOnly();
          return;
        }
      }
    }
  }

  if (!touchNow) touchWasDown = false;

  delay(10);
}

}  // namespace IRUniversalController

namespace IRCopyController {

static constexpr int kMaxKeys = 12;
static const char* kKeyNames[kMaxKeys] = {
    "Power", "Mute", "VolUp", "VolDn", "ChUp", "ChDn",
    "Up", "Down", "Left", "Right", "Ok", "Back"};
static const char* kKeyLabels[kMaxKeys] = {
    "PWR", "MUTE", "VOL+", "VOL-", "CH+", "CH-",
    "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};

// Mirror of Universal's protocolToken — namespaces are separate.
static const char* protocolToken(decode_type_t p) {
  switch (p) {
    case decode_type_t::NEC: return "NEC";
    case decode_type_t::NEC_LIKE: return "NECLIKE";
    case decode_type_t::SONY: return "SONY";
    case decode_type_t::SAMSUNG36: return "SAMSUNG36";
    case decode_type_t::LG: return "LG";
    case decode_type_t::LG2: return "LG2";
    case decode_type_t::JVC: return "JVC";
    case decode_type_t::DENON: return "DENON";
    case decode_type_t::PANASONIC: return "PANASONIC";
    case decode_type_t::RC5: return "RC5";
    case decode_type_t::RC6: return "RC6";
    case decode_type_t::PIONEER: return "PIONEER";
    case decode_type_t::DISH: return "DISH";
    case decode_type_t::GICABLE: return "GICABLE";
    case decode_type_t::EPSON: return "EPSON";
    default: return nullptr;
  }
}

// No raw buffers — Universal profiles only need decoded codes (saves ~12KB RAM).
struct CapturedKey {
  bool has = false;
  decode_type_t proto = decode_type_t::UNKNOWN;
  uint16_t bits = 0;
  uint64_t code = 0;
};

static constexpr int16_t kToolbarY = 20;
static constexpr int16_t kToolbarH = 16;
static constexpr int16_t kIconSize = 16;
static constexpr int kToolbarBottom = kToolbarY + kToolbarH;
static constexpr int kBodyTop = kToolbarBottom + 1;
static constexpr int kIconBackX = 10;
static constexpr int kPadX = 10;
static constexpr int kBoxX = 4;
static constexpr int kBoxW = 232;
static constexpr int kBoxHeaderH = 15;
static constexpr int kRadius = 3;
static constexpr int kLineH = 14;

static CapturedKey s_keys[kMaxKeys];
static int s_currentKey = 0;
static String s_controllerName;
static bool s_hasCapture = false;
static decode_type_t s_captureProto = decode_type_t::UNKNOWN;
static uint16_t s_captureBits = 0;
static uint64_t s_captureValue = 0;
static decode_type_t s_lockedProto = decode_type_t::UNKNOWN;
static uint16_t s_lockedBits = 0;
static bool s_protoLocked = false;
static bool s_uiDrawn = false;
static bool s_exitAfterNotif = false;
static uint32_t s_lastBtnMs = 0;
static constexpr uint32_t kBtnDebounceMs = 280;

static bool s_prevLeft = false;
static bool s_prevRight = false;
static bool s_prevUp = false;
static bool s_prevDown = false;
static bool s_prevSelect = false;

static IRrecv s_recv(IR_RX_PIN);
static decode_results s_results;

static FeatureUI::Button s_actionBtns[3];  // Skip / Redo / Save (no-nav fallback + touch)

static int capturedCount() {
  int n = 0;
  for (int i = 0; i < kMaxKeys; i++) {
    if (s_keys[i].has) n++;
  }
  return n;
}

static bool allSlotsDone() { return s_currentKey >= kMaxKeys; }

static uint8_t s_navMode = 255;  // cache so we don't flicker the touch nav bar

static void applyNavLabels() {
  if (!featureHasTouchNavBar()) return;

  uint8_t mode;
  if (allSlotsDone()) mode = 2;
  else if (s_hasCapture) mode = 1;
  else mode = (capturedCount() > 0) ? 3 : 0;
  if (mode == s_navMode) return;
  s_navMode = mode;

  // setTouchNavLabels(left, down, center, up, right)
  if (mode == 2) {
    irSetCopyNavLabels(nullptr, nullptr, "Exit", nullptr, "Save");
  } else if (mode == 1) {
    irSetCopyNavLabels("Skip", nullptr, "Exit", "Redo", "Save");
  } else if (mode == 3) {
    irSetCopyNavLabels("Skip", "Done", "Exit", nullptr, nullptr);
  } else {
    irSetCopyNavLabels("Skip", nullptr, "Exit", nullptr, nullptr);
  }
}

static void drawToolbar() {
  tft.fillRect(0, kToolbarY, 240, kToolbarH, UI_FG);
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  tft.drawBitmap(kIconBackX, kToolbarY, bitmap_icon_go_back, kIconSize, kIconSize, UI_ICON);
  tft.drawFastHLine(0, kToolbarBottom, 240, UI_LINE);
}

static void drawLabeledBox(int y, int h, const char* title) {
  tft.drawRoundRect(kBoxX, y, kBoxW, h, kRadius, UI_LINE);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.drawString(title, kBoxX + 4, y + 3);
}

static void drawProgressBar(int x, int y, int w, int h, int done, int total) {
  tft.drawRoundRect(x, y, w, h, 2, UI_LINE);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, FEATURE_BG);
  if (total <= 0) return;
  int fill = ((w - 2) * done) / total;
  if (fill > 0) {
    tft.fillRoundRect(x + 1, y + 1, fill, h - 2, 2, UI_ICON);
  }
}

static void layoutActionButtons() {
  const int y = irContentBottom() - 28;
  const int gap = 6;
  const int btnW = (240 - kPadX * 2 - gap * 2) / 3;
  const int h = 22;
  s_actionBtns[0] = {(int16_t)kPadX, (int16_t)y, (int16_t)btnW, (int16_t)h, "Skip",
                     FeatureUI::ButtonStyle::Secondary, false};
  s_actionBtns[1] = {(int16_t)(kPadX + btnW + gap), (int16_t)y, (int16_t)btnW, (int16_t)h, "Redo",
                     FeatureUI::ButtonStyle::Secondary, false};
  s_actionBtns[2] = {(int16_t)(kPadX + (btnW + gap) * 2), (int16_t)y, (int16_t)btnW, (int16_t)h,
                     allSlotsDone() || s_hasCapture ? "Save" : "Done",
                     FeatureUI::ButtonStyle::Primary, false};

  // Disable states that don't make sense.
  s_actionBtns[0].disabled = allSlotsDone() || s_hasCapture;
  s_actionBtns[1].disabled = !s_hasCapture || allSlotsDone();
  if (allSlotsDone()) {
    s_actionBtns[2].disabled = false;
    s_actionBtns[2].label = "Save";
  } else if (s_hasCapture) {
    s_actionBtns[2].disabled = false;
    s_actionBtns[2].label = "Save";
  } else {
    s_actionBtns[2].disabled = capturedCount() == 0;
    s_actionBtns[2].label = "Done";
  }
}

static void drawActionButtons() {
  if (featureHasTouchNavBar()) return;
  layoutActionButtons();
  for (int i = 0; i < 3; i++) {
    const FeatureUI::Button& b = s_actionBtns[i];
    FeatureUI::drawButtonRect(b.x, b.y, b.w, b.h, b.label, b.style, false, b.disabled);
  }
}

// Layout constants for dirty-region updates (must match drawUI chrome).
static constexpr int kInfoY = kBodyTop + 34;
static constexpr int kInfoH = kBoxHeaderH + kLineH * 3 + 10;
static constexpr int kProgY = kInfoY + kInfoH + 8;
static constexpr int kProgH = kBoxHeaderH + 28;
static constexpr int kActY  = kProgY + kProgH + 8;
static constexpr int kActH  = kBoxHeaderH + kLineH * 3 + 10;

static void clearBoxBody(int y, int h) {
  tft.fillRect(kBoxX + 2, y + kBoxHeaderH, kBoxW - 4, h - kBoxHeaderH - 2, FEATURE_BG);
}

static void drawTitleStrip() {
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(kPadX, kBodyTop + 6);
  tft.print("Copy Controller");
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kBodyTop + 18);
  String name = s_controllerName;
  if (name.length() > 28) name = name.substring(0, 28);
  tft.print(name);
}

static void drawStatusContent() {
  clearBoxBody(kInfoY, kInfoH);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoY + kBoxHeaderH + 2);
  tft.print("Step:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(50, kInfoY + kBoxHeaderH + 2);
  if (allSlotsDone()) {
    tft.print("Ready to save");
  } else {
    tft.printf("%d / %d  (%s)", s_currentKey + 1, kMaxKeys, kKeyLabels[s_currentKey]);
  }

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoY + kBoxHeaderH + 2 + kLineH);
  tft.print("Proto:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(50, kInfoY + kBoxHeaderH + 2 + kLineH);
  if (s_protoLocked) {
    String pname = typeToString(s_lockedProto);
    if (!pname.length()) pname = "UNKNOWN";
    tft.print(pname);
    tft.print(' ');
    tft.print((unsigned)s_lockedBits);
    tft.print("-bit");
  } else {
    tft.print("-");
  }

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kInfoY + kBoxHeaderH + 2 + kLineH * 2);
  tft.print("Saved:");
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(50, kInfoY + kBoxHeaderH + 2 + kLineH * 2);
  tft.printf("%d key%s", capturedCount(), capturedCount() == 1 ? "" : "s");
}

static void drawCaptureContent() {
  clearBoxBody(kActY, kActH);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(s_hasCapture ? UI_OK : (allSlotsDone() ? UI_OK : UI_WARN), FEATURE_BG);
  tft.setCursor(kPadX, kActY + kBoxHeaderH + 2);
  if (allSlotsDone()) {
    tft.print("All slots complete");
  } else if (s_hasCapture) {
    String pname = typeToString(s_captureProto);
    if (!pname.length()) pname = "UNKNOWN";
    tft.print("Got ");
    tft.print(pname);
    tft.print(" | ");
    tft.print((unsigned)s_captureBits);
    tft.print(" bits");
  } else {
    tft.print("Waiting for IR signal...");
  }

  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kActY + kBoxHeaderH + 2 + kLineH);
  if (allSlotsDone()) {
    tft.print("Save writes /ir or /ir_profiles");
  } else if (s_hasCapture) {
    tft.print("Save key, Redo, or Skip");
  } else {
    tft.print("Aim remote at DIV IR receiver");
  }

  tft.setCursor(kPadX, kActY + kBoxHeaderH + 2 + kLineH * 2);
  if (featureHasTouchNavBar()) {
    if (allSlotsDone()) {
      tft.print("Nav: Save | Exit");
    } else if (s_hasCapture) {
      tft.print("Nav: Skip | Redo | Save | Exit");
    } else {
      tft.print(capturedCount() > 0 ? "Nav: Skip | Done | Exit" : "Nav: Skip | Exit");
    }
  } else {
    tft.print("Use on-screen buttons below");
  }
}

// Full: paint chrome once. Partial: only Status / Progress / Capture interiors.
static void drawUI(bool fullRedraw = true) {
  if (fullRedraw) {
    if (featureHasTouchNavBar()) {
      // Clear only under the status bar so the bar itself doesn't flash.
      const int bottom = irContentBottom();
      if (bottom > 20) {
        tft.fillRect(0, 20, 240, bottom - 20, FEATURE_BG);
      }
    } else {
      tft.fillScreen(FEATURE_BG);
    }
    currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    drawToolbar();
    drawTitleStrip();
    drawLabeledBox(kInfoY, kInfoH, "Status");
    drawLabeledBox(kProgY, kProgH, "Progress");
    drawLabeledBox(kActY, kActH, "Capture");
    s_navMode = 255;
  }

  drawStatusContent();
  drawProgressBar(kPadX, kProgY + kBoxHeaderH + 6, 220, 12, s_currentKey, kMaxKeys);
  drawCaptureContent();
  drawActionButtons();
  applyNavLabels();
  if (fullRedraw) {
    irRedrawNavChrome();
  }
  s_uiDrawn = true;
}

static void clearCapture() {
  s_hasCapture = false;
  s_captureProto = decode_type_t::UNKNOWN;
  s_captureBits = 0;
  s_captureValue = 0;
}

static void pollCapture() {
  if (!s_recv.decode(&s_results)) return;

  const decode_type_t proto = s_results.decode_type;
  const uint16_t bits = s_results.bits;
  const uint64_t value = s_results.value;
  s_recv.resume();

  if (proto == decode_type_t::UNKNOWN || bits == 0) return;

  if (s_protoLocked) {
    if (proto != s_lockedProto) {
      showNotification("IR Copy", "Protocol mismatch — same remote only");
      return;
    }
  } else {
    s_lockedProto = proto;
    s_lockedBits = bits;
    s_protoLocked = true;
  }

  s_captureProto = proto;
  s_captureBits = bits;
  s_captureValue = value;
  s_hasCapture = true;
  drawUI(false);
}

static void saveCurrentKey() {
  if (!s_hasCapture || allSlotsDone()) return;
  s_keys[s_currentKey].has = true;
  s_keys[s_currentKey].proto = s_captureProto;
  s_keys[s_currentKey].bits = s_captureBits;
  s_keys[s_currentKey].code = s_captureValue;
  clearCapture();
  s_currentKey++;
  drawUI(false);
}

static void skipKey() {
  if (allSlotsDone()) return;
  if (s_hasCapture) {
    showNotification("IR Copy", "Save or redo capture first");
    return;
  }
  s_keys[s_currentKey].has = false;
  s_currentKey++;
  drawUI(false);
}

static void redoKey() {
  if (!s_hasCapture || allSlotsDone()) return;
  clearCapture();
  drawUI(false);
}

static void finishEarly() {
  if (s_hasCapture) {
    showNotification("IR Copy", "Save or redo capture first");
    return;
  }
  if (capturedCount() == 0) {
    showNotification("IR Copy", "Capture at least one key");
    return;
  }
  s_currentKey = kMaxKeys;
  drawUI(false);
}

static bool ensureDir(const char* path) {
  if (!path || !path[0]) return false;
  if (SD.exists(path)) {
    File f = SD.open(path);
    const bool ok = f && f.isDirectory();
    if (f) f.close();
    if (ok) return true;
    // Name exists but isn't a folder — treat as failure so caller can fall back.
    return false;
  }
  if (SD.mkdir(path)) return true;
  if (path[0] == '/') return SD.mkdir(path + 1);
  return false;
}

// Prefer /ir first — same folder IR Record uses successfully on this board.
static bool pickSaveDir(String& outDir, String* errOut = nullptr) {
  restoreSdAfterSharedSpi();
  if (!isSDCardAvailable()) {
    if (errOut) *errOut = "SD not available";
    return false;
  }

  static const char* kDirs[] = {
      "/ir", "ir",
      "/ir_profiles", "ir_profiles",
      "/profiles", "profiles"};
  for (size_t i = 0; i < sizeof(kDirs) / sizeof(kDirs[0]); i++) {
    if (ensureDir(kDirs[i])) {
      outDir = (kDirs[i][0] == '/') ? String(kDirs[i]) : (String("/") + kDirs[i]);
      return true;
    }
  }

  // Root is always writable if the card mounts — skip mkdir.
  outDir = "";
  return true;
}

static String sanitizeFileStem(const String& name) {
  String out;
  out.reserve(name.length());
  for (int i = 0; i < (int)name.length(); i++) {
    char c = name[i];
    if (c == ' ' || c == '/' || c == '\\') c = '_';
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out += c;
    }
  }
  if (!out.length()) out = "custom";
  if (out.length() > 20) out = out.substring(0, 20);
  return out;
}

static bool writeJsonFile(const String& path, const DynamicJsonDocument& doc) {
  if (SD.exists(path)) SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    // Some cores want "w" explicitly.
    f = SD.open(path.c_str(), "w");
  }
  if (!f) return false;
  const size_t n = serializeJson(doc, f);
  f.flush();
  f.close();
  return n > 0;
}

static bool saveControllerToSD() {
  String dir;
  String sdErr;
  if (!pickSaveDir(dir, &sdErr)) {
    showNotification("IR Copy", sdErr.c_str());
    return false;
  }

  const int captured = capturedCount();
  if (captured == 0) {
    showNotification("IR Copy", "No keys captured");
    return false;
  }
  if (!s_protoLocked) {
    showNotification("IR Copy", "No protocol locked");
    return false;
  }

  const char* tok = protocolToken(s_lockedProto);
  String protoName = tok ? String(tok) : String(typeToString(s_lockedProto));
  if (!protoName.length() || protoName.equalsIgnoreCase("UNKNOWN")) {
    protoName = String((int)s_lockedProto);
  }

  DynamicJsonDocument doc(3072);
  doc["name"] = s_controllerName;
  doc["protocol"] = protoName;
  doc["protocol_id"] = (int)s_lockedProto;
  doc["bits"] = s_lockedBits;
  doc["category"] = "CUSTOM";
  doc["brand"] = "CUSTOM";
  doc["source"] = "custom";

  uint64_t codes[kIrCustomKeySlots]{};
  bool hasKeys[kIrCustomKeySlots]{};

  JsonObject codesObj = doc.createNestedObject("codes");
  for (int i = 0; i < kMaxKeys && i < kIrCustomKeySlots; i++) {
    if (!s_keys[i].has) continue;
    if (s_keys[i].proto != s_lockedProto) continue;
    String keyName = String(kKeyNames[i]);
    keyName.toLowerCase();
    char hexStr[24];
    snprintf(hexStr, sizeof(hexStr), "0x%llX", (unsigned long long)s_keys[i].code);
    codesObj[keyName] = hexStr;
    codes[i] = s_keys[i].code;
    hasKeys[i] = true;
  }

  // Always keep a RAM copy so Universal can show it even if SD scan fails.
  irRamCustomRemember(s_controllerName.c_str(), s_lockedProto, s_lockedBits, codes, hasKeys);

  const String stem = sanitizeFileStem(s_controllerName);
  String path;
  if (dir.length()) {
    path = dir + "/prof_" + stem + ".json";
  } else {
    path = "/ir_cust_" + stem + ".json";
  }

  String finalPath = path;
  int counter = 1;
  while (SD.exists(finalPath)) {
    if (dir.length()) {
      finalPath = dir + "/prof_" + stem + "_" + String(counter++) + ".json";
    } else {
      finalPath = "/ir_cust_" + stem + "_" + String(counter++) + ".json";
    }
  }

  bool wrote = writeJsonFile(finalPath, doc);
  if (!wrote) {
    restoreSdAfterSharedSpi();
    if (!isSDCardAvailable()) {
      // RAM cache still has the profile for this session.
      showNotification("IR Copy", "Saved in RAM (SD fail)");
      return true;
    }
    wrote = writeJsonFile(finalPath, doc);
  }
  if (!wrote) {
    finalPath = "/irc.json";
    wrote = writeJsonFile(finalPath, doc);
  }

  // Canonical Copy Controller path (do NOT touch /ir_profiles.json — that file
  // is the user-managed multi-profile library and must never be overwritten).
  ensureDir("/ir");
  writeJsonFile(String(kIrUnivCustomPath), doc);
  writeJsonFile(String(kIrUnivCustomRoot), doc);

  if (!wrote) {
    showNotification("IR Copy", "SD write failed; RAM ok");
    return true;  // Universal can still use RAM cache this session
  }

  char msg[72];
  snprintf(msg, sizeof(msg), "Saved %s (%d)", finalPath.c_str(), captured);
  showNotification("IR Copy", msg);
  return true;
}

static void resetState() {
  s_currentKey = 0;
  clearCapture();
  s_protoLocked = false;
  s_lockedProto = decode_type_t::UNKNOWN;
  s_lockedBits = 0;
  s_lastBtnMs = 0;
  s_prevLeft = s_prevRight = s_prevUp = s_prevDown = s_prevSelect = false;
  s_uiDrawn = false;
  s_exitAfterNotif = false;
  s_navMode = 255;
  for (int i = 0; i < kMaxKeys; i++) {
    s_keys[i] = CapturedKey{};
  }
}

static void doSaveAction() {
  if (allSlotsDone()) {
    if (saveControllerToSD()) {
      s_exitAfterNotif = true;  // leave after user closes the success toast
    }
    return;
  }
  if (s_hasCapture) {
    saveCurrentKey();
    return;
  }
  finishEarly();
}

static void handleTouchNav() {
  if (!featureHasTouchNavBar()) return;

  // Labels: left, down, center, up, right
  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    skipKey();
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    redoKey();
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    doSaveAction();
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    // "Done" while waiting — jump to save screen.
    if (!allSlotsDone() && !s_hasCapture) finishEarly();
  }
}

static void handlePhysicalButtons() {
  const uint32_t now = millis();
  const bool leftNow = isPhysicalButtonPressed(BTN_LEFT);
  const bool rightNow = isPhysicalButtonPressed(BTN_RIGHT);
  const bool upNow = isPhysicalButtonPressed(BTN_UP);
  const bool downNow = isPhysicalButtonPressed(BTN_DOWN);
  const bool selectNow = isPhysicalButtonPressed(BTN_SELECT);

  if ((uint32_t)(now - s_lastBtnMs) > kBtnDebounceMs) {
    if (selectNow && !s_prevSelect) {
      feature_exit_requested = true;
      s_lastBtnMs = now;
    } else if (leftNow && !s_prevLeft) {
      skipKey();
      s_lastBtnMs = now;
    } else if (upNow && !s_prevUp) {
      redoKey();
      s_lastBtnMs = now;
    } else if (rightNow && !s_prevRight) {
      doSaveAction();
      s_lastBtnMs = now;
    } else if (downNow && !s_prevDown) {
      if (!allSlotsDone() && !s_hasCapture) {
        finishEarly();
        s_lastBtnMs = now;
      }
    }
  }

  s_prevLeft = leftNow;
  s_prevRight = rightNow;
  s_prevUp = upNow;
  s_prevDown = downNow;
  s_prevSelect = selectNow;
}

static void handleToolbarAndScreenTouch() {
  static bool touchWasDown = false;
  static uint32_t lastTouchMs = 0;
  const uint32_t now = millis();
  const bool touchNow = isTouchDownDismiss();

  if (touchNow && !touchWasDown) {
    touchWasDown = true;
    int x, y;
    if (!readTouchXY(x, y)) return;
    if ((uint32_t)(now - lastTouchMs) < kBtnDebounceMs) return;
    lastTouchMs = now;

    // Toolbar back
    if (y > kToolbarY && y < kToolbarBottom &&
        x > kIconBackX && x < (kIconBackX + kIconSize)) {
      feature_exit_requested = true;
      return;
    }

    if (!featureHasTouchNavBar()) {
      layoutActionButtons();
      const int hit = FeatureUI::hit(s_actionBtns, 3, x, y);
      if (hit == 0) skipKey();
      else if (hit == 1) redoKey();
      else if (hit == 2) doSaveAction();
    }
  }
  if (!touchNow) touchWasDown = false;
}

void setup() {
  setTouchButtonInputEnabled(false);

  OnScreenKeyboardConfig cfg;
  cfg.titleLine1 = "Enter controller name";
  cfg.titleLine2 = "(max 20 chars)";
  osKeyboardUseStandardLayout(cfg);
  cfg.maxLen = 20;
  cfg.buttonsY = 195;
  cfg.backLabel = "Cancel";
  cfg.middleLabel = "Clear";
  cfg.okLabel = "OK";
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg = "Name cannot be empty!";
  const char* shuffleNames[] = {"TV", "AC", "Lamp", "Receiver", "Projector", "Soundbar"};
  cfg.shuffleNames = shuffleNames;
  cfg.shuffleCount = 6;
  cfg.enableShuffle = true;

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");
  if (!r.accepted) {
    feature_exit_requested = true;
    return;
  }
  s_controllerName = r.text;

  pinMode(IR_RX_PIN, INPUT_PULLUP);
  resetState();
  s_recv.enableIRIn();

  setTouchButtonInputEnabled(true);
  drawUI();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  // Notifications must be dismissed before other input (same pattern as SubGHz/GPS).
  if (isNotificationVisible()) {
    maintainTouchNavBar();
    int nx, ny;
    bool dismissed = false;
    if (readTouchXY(nx, ny)) {
      dismissed = (notificationHandleTouch(nx, ny) != NotificationAction::None);
    } else if (isPhysicalButtonPressed(BTN_SELECT) ||
               isTouchNavButtonPressedEdge(BTN_SELECT) ||
               isTouchNavButtonPressedEdge(BTN_LEFT)) {
      hideNotification();
      dismissed = true;
    }
    if (dismissed) {
      // hideNotification only clears a rect — redraw the full feature UI.
      drawUI();
      if (s_exitAfterNotif) feature_exit_requested = true;
    }
    delay(10);
    return;
  }
  if (s_exitAfterNotif) {
    feature_exit_requested = true;
    return;
  }

  if (!s_uiDrawn) drawUI();

  // Poll IR before nav chrome so label changes from a new capture are painted
  // in the same frame by maintainTouchNavBar().
  if (!allSlotsDone() && !s_hasCapture) {
    pollCapture();
  }

  maintainTouchNavBar();

  handleTouchNav();
  handlePhysicalButtons();
  handleToolbarAndScreenTouch();
  delay(10);
}

}  // namespace IRCopyController

namespace IRUniversalAC {

static constexpr const char* AC_DIR    = "/ir_ac";
static constexpr const char* AC_RECENT = "/ir_ac/recent.txt";

static constexpr int16_t kToolbarY = 20;
static constexpr int16_t kToolbarH = 16;
static constexpr int16_t kIconSize = 16;
static constexpr int kToolbarBottom = kToolbarY + kToolbarH;  // 36
static constexpr int kBodyTop = kToolbarBottom + 1;           // 37
static constexpr int kPadX = 10;
static constexpr int kIconBackX = 10;
static constexpr int kIconReloadX = 210;
static constexpr int LIST_ROW_H = 20;

static IRLgAc s_lg(IR_TX_PIN);
static IRsend s_rawSend(IR_TX_PIN);   // for direct commands (swing by position, light)
static int s_swingIdx = -1;           // -1 = nenhuma posicao enviada ainda

static uint32_t parseHex32(const char* s) {
  if (!s) return 0;
  String t(s); t.trim();
  if (t.startsWith("0x") || t.startsWith("0X")) return (uint32_t)strtoul(t.c_str() + 2, nullptr, 16);
  return (uint32_t)strtoul(t.c_str(), nullptr, 16);
}
static decode_type_t protoFromStr(const String& s, decode_type_t def) {
  String u = s; u.toUpperCase();
  if (u == "LG")  return decode_type_t::LG;
  if (u == "LG2") return decode_type_t::LG2;
  return def;
}

// ---------- modelos LG ----------
static const lg_ac_remote_model_t kLgModels[] = {
  AKB75215403, AKB74955603, AKB73757604, GE6711AR2853M, LG6711A20083V
};
static constexpr int kLgModelCount = sizeof(kLgModels) / sizeof(kLgModels[0]);

static const char* lgModelName(lg_ac_remote_model_t m) {
  switch (m) {
    case AKB75215403:   return "AKB75215403";
    case AKB74955603:   return "AKB74955603";
    case AKB73757604:   return "AKB73757604";
    case GE6711AR2853M: return "GE6711AR2853M";
    case LG6711A20083V: return "LG6711A20083V";
    default:            return "AKB75215403";
  }
}

static lg_ac_remote_model_t lgModelFromString(const String& in) {
  String s = in; s.toUpperCase();
  if (s.indexOf("GE6711") >= 0)   return GE6711AR2853M;
  if (s.indexOf("20083") >= 0)    return LG6711A20083V;
  if (s.indexOf("74955603") >= 0) return AKB74955603;
  if (s.indexOf("73757604") >= 0) return AKB73757604;
  if (s.indexOf("75215403") >= 0) return AKB75215403;
  return AKB75215403;  // default (inclui o AKB75215424 do usuario)
}

static const char* lgProtoName(lg_ac_remote_model_t m) {
  return (m == GE6711AR2853M || m == LG6711A20083V) ? "LG" : "LG2";
}

// ---------- perfis ----------
struct AcProfile {
  String name;
  String brand;
  String model;
  bool fromSd = false;
  lg_ac_remote_model_t lgModel = AKB75215403;
  std::vector<uint32_t> swingCodes;               // vane positions, in order
  decode_type_t swingProto = decode_type_t::LG2;  // protocolo p/ enviar swing
  uint32_t lightCode = 0x88C00A6;                 // display light toggle
  decode_type_t lightProto = decode_type_t::LG2;
};

// Fills in default (library) swing/light codes; the SD card can override them.
static void setAcDefaults(AcProfile& p) {
  static const uint32_t def[] = {
    kLgAcSwingVLowest, kLgAcSwingVLow, kLgAcSwingVMiddle,
    kLgAcSwingVUpperMiddle, kLgAcSwingVHigh, kLgAcSwingVHighest,
    kLgAcSwingVSwing, kLgAcSwingVOff
  };
  p.swingCodes.assign(def, def + (sizeof(def) / sizeof(def[0])));
  p.swingProto = decode_type_t::LG2;
  p.lightCode = kLgAcLightToggle;
  p.lightProto = decode_type_t::LG2;
}
static std::vector<AcProfile> s_profs;
static int s_profIdx = 0;
static int s_sdCount = 0;
static bool s_loadedSd = false;
static String s_lastErr;

static void loadBuiltinAc() {
  auto add = [&](const char* n, const char* model) {
    AcProfile p;
    p.name = n; p.brand = "LG"; p.model = model; p.fromSd = false;
    p.lgModel = lgModelFromString(model);
    setAcDefaults(p);
    s_profs.push_back(p);
  };
  add("LG Dual Inverter",        "AKB75215403");
  add("LG (AKB75215403)",        "AKB75215403");
  add("LG (AKB74955603)",        "AKB74955603");
  add("LG (AKB73757604)",        "AKB73757604");
  add("LG (GE6711AR2853M)",      "GE6711AR2853M");
  add("LG (LG6711A20083V)",      "LG6711A20083V");
}

static bool loadAcFromSd() {
  s_sdCount = 0;
  if (!isSDCardAvailable()) return false;
  if (!SD.exists(AC_DIR)) return false;
  File d = SD.open(AC_DIR);
  if (!d) return false;
  for (;;) {
    File f = d.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String name = String(f.name());
      if (name.endsWith(".json")) {
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, f)) {
          JsonObject o = doc.as<JsonObject>();
          String brand = String((const char*)(o["brand"] | ""));
          brand.toUpperCase();
          if (brand.length() == 0 || brand == "LG") {  // so LG por enquanto
            AcProfile p;
            p.name  = String((const char*)(o["name"]  | ""));
            p.brand = "LG";
            p.model = String((const char*)(o["model"] | "AKB75215403"));
            p.fromSd = true;
            p.lgModel = lgModelFromString(p.model);
            if (!p.name.length()) p.name = String("LG ") + p.model;
            setAcDefaults(p);
            p.swingProto = protoFromStr(String((const char*)(o["swing_proto"] | "")), p.swingProto);
            p.lightProto = protoFromStr(String((const char*)(o["light_proto"] | "")), p.lightProto);
            if (o.containsKey("light")) { uint32_t lc = parseHex32(o["light"] | ""); if (lc) p.lightCode = lc; }
            JsonArray sw = o["swing"].as<JsonArray>();
            if (!sw.isNull()) {
              std::vector<uint32_t> codes;
              for (JsonVariant v : sw) { uint32_t c = parseHex32(v.as<const char*>()); if (c) codes.push_back(c); }
              if (!codes.empty()) p.swingCodes = codes;
            }
            s_profs.push_back(p);
            s_sdCount++;
          }
        }
      }
    }
    f.close();
    if ((int)s_profs.size() >= 200) break;
  }
  d.close();
  return s_sdCount > 0;
}

static void refreshAc() {
  s_profs.clear();
  loadBuiltinAc();
  s_loadedSd = loadAcFromSd();
  s_lastErr = s_loadedSd ? (String(s_sdCount) + " do SD") : String("SD: nenhum");
  if (s_profIdx < 0 || s_profIdx >= (int)s_profs.size()) s_profIdx = 0;
}

// ---------- estado do A/C ----------
struct AcState {
  bool power = false;
  uint8_t mode = kLgAcCool;
  uint8_t temp = 22;
  uint8_t fan = kLgAcFanAuto;
  bool swingV = false;
  bool light = true;
};
static AcState s_st;

static const uint8_t kFanCycle[] = { kLgAcFanAuto, kLgAcFanLow, kLgAcFanMedium, kLgAcFanMax };
static constexpr int kFanCycleCount = sizeof(kFanCycle) / sizeof(kFanCycle[0]);

static const char* fanName(uint8_t f) {
  switch (f) {
    case kLgAcFanAuto:   return "Auto";
    case kLgAcFanLow:    return "Low";
    case kLgAcFanMedium: return "Med";
    case kLgAcFanMax:    return "Max";
    case kLgAcFanLowest: return "Min";
    case kLgAcFanHigh:   return "High";
    default:             return "?";
  }
}
static const char* modeName(uint8_t m) {
  switch (m) {
    case kLgAcCool: return "Cool";
    case kLgAcDry:  return "Dry";
    case kLgAcFan:  return "Fan";
    case kLgAcAuto: return "Auto";
    case kLgAcHeat: return "Heat";
    default:        return "?";
  }
}

static void acApplyAndSend() {
  if (s_profs.empty()) return;
  const AcProfile& p = s_profs[s_profIdx];
  s_lg.setModel(p.lgModel);
  s_lg.setPower(s_st.power);
  s_lg.setMode(s_st.mode);
  s_lg.setTemp(s_st.temp);
  s_lg.setFan(s_st.fan);
  // Swing and light are sent as a direct command (see acButton), not here.
  s_lg.send();
}

// ---------- botoes do controle ----------
enum AcBtn { B_PWR, B_MODE, B_TEMPUP, B_TEMPDN, B_SWING, B_SPEED, B_LIGHT, B_COUNT };
static FeatureUI::Button s_btn[B_COUNT];

static void acButton(int b) {
  switch (b) {
    case B_PWR:    s_st.power = !s_st.power; break;
    case B_MODE:   s_st.power = true; s_st.mode = (uint8_t)((s_st.mode + 1) % 5); break;
    case B_TEMPUP: s_st.power = true; if (s_st.temp < kLgAcMaxTemp) s_st.temp++; break;
    case B_TEMPDN: s_st.power = true; if (s_st.temp > kLgAcMinTemp) s_st.temp--; break;
    case B_SWING: {
      s_st.power = true;
      const AcProfile& p = s_profs[s_profIdx];
      if (p.swingCodes.empty()) return;
      s_swingIdx = (s_swingIdx + 1) % (int)p.swingCodes.size();
      s_rawSend.send(p.swingProto, p.swingCodes[s_swingIdx], kLgBits);
      return;  // direct command, doesn't resend the full state
    }
    case B_SPEED: {
      s_st.power = true;
      int i = 0; for (; i < kFanCycleCount; i++) if (kFanCycle[i] == s_st.fan) break;
      if (i >= kFanCycleCount) i = -1;
      s_st.fan = kFanCycle[(i + 1) % kFanCycleCount];
      break;
    }
    case B_LIGHT: {
      const AcProfile& p = s_profs[s_profIdx];
      if (p.lightCode) s_rawSend.send(p.lightProto, p.lightCode, kLgBits);
      return;  // direct toggle of the display light
    }
  }
  acApplyAndSend();
}

// ---------- telas: estado de lista ----------
enum class AScreen : uint8_t { List, Details, Control };
static AScreen s_scr = AScreen::List;

static std::vector<int> s_order;
static std::vector<String> s_recentNames;
static std::vector<int> s_recent;
struct Row { int idx; bool header; const char* label; };
static std::vector<Row> s_rows;
static int s_sel = 0, s_top = 0, s_selProf = 0;
static int s_navSel = 0;   // botao do controle selecionado (tela Control)
static bool s_drawn = false;

static int listTopY()   { return kBodyTop + 6; }
static int listBottom() { return irContentBottom() - 4; }
static int listRows()   { int r = (listBottom() - listTopY()) / LIST_ROW_H; return r < 1 ? 1 : r; }

static String acTrunc(const String& s, int maxPx) {
  if (maxPx <= 0 || !s.length()) return s;
  if ((int)tft.textWidth(s, 1) <= maxPx) return s;
  const int ell = tft.textWidth("...", 1);
  String o = s;
  while (o.length() && (int)tft.textWidth(o, 1) + ell > maxPx) o.remove(o.length() - 1);
  return o + "...";
}

// ---------- recentes ----------
static void loadRecentNames() {
  s_recentNames.clear();
  if (!isSDCardAvailable()) return;
  File f = SD.open(AC_RECENT, FILE_READ);
  if (!f) return;
  while (f.available() && (int)s_recentNames.size() < 3) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) s_recentNames.push_back(line);
  }
  f.close();
}
static void saveRecentNames() {
  if (!isSDCardAvailable()) return;
  if (!SD.exists(AC_DIR)) SD.mkdir(AC_DIR);
  if (SD.exists(AC_RECENT)) SD.remove(AC_RECENT);
  File f = SD.open(AC_RECENT, FILE_WRITE);
  if (!f) return;
  for (auto& n : s_recentNames) f.println(n);
  f.close();
}
static void pushRecent(const String& name) {
  if (!name.length()) return;
  for (int i = 0; i < (int)s_recentNames.size(); i++)
    if (s_recentNames[i] == name) { s_recentNames.erase(s_recentNames.begin() + i); break; }
  s_recentNames.insert(s_recentNames.begin(), name);
  while ((int)s_recentNames.size() > 3) s_recentNames.pop_back();
  saveRecentNames();
}

static void rebuildOrder() {
  s_order.clear();
  for (int i = 0; i < (int)s_profs.size(); i++) s_order.push_back(i);
  std::sort(s_order.begin(), s_order.end(), [](int a, int b) {
    const AcProfile& pa = s_profs[a];
    const AcProfile& pb = s_profs[b];
    auto up = [](String s) { s.toUpperCase(); return s; };
    String x, y;
    x = up(pa.brand); y = up(pb.brand); if (x != y) return x < y;
    x = up(pa.model); y = up(pb.model); if (x != y) return x < y;
    x = up(pa.name);  y = up(pb.name);  return x < y;
  });
}
static void rebuildRecent() {
  s_recent.clear();
  for (auto& n : s_recentNames) {
    for (int i = 0; i < (int)s_profs.size(); i++)
      if (s_profs[i].name == n) { s_recent.push_back(i); break; }
    if ((int)s_recent.size() >= 3) break;
  }
}
static int firstSelectable() {
  for (int i = 0; i < (int)s_rows.size(); i++) if (!s_rows[i].header) return i;
  return 0;
}
static void rebuildRows() {
  s_rows.clear();
  if (!s_recent.empty()) {
    s_rows.push_back({ -1, true, "Recentes" });
    for (int idx : s_recent) s_rows.push_back({ idx, false, nullptr });
    s_rows.push_back({ -1, true, "Todos" });
  }
  for (int idx : s_order) s_rows.push_back({ idx, false, nullptr });
  if (s_sel < 0 || s_sel >= (int)s_rows.size() || s_rows[s_sel].header) s_sel = firstSelectable();
}
static void moveSel(int dir) {
  int n = (int)s_rows.size();
  if (n == 0) return;
  int i = s_sel + dir;
  while (i >= 0 && i < n && s_rows[i].header) i += dir;
  if (i < 0 || i >= n) return;
  s_sel = i;
}
static void ensureVisible() {
  int rows = listRows();
  if (s_sel < s_top) s_top = s_sel;
  if (s_sel >= s_top + rows) s_top = s_sel - rows + 1;
  if (s_top < 0) s_top = 0;
}

// ---------- desenho ----------
static void drawToolbar() {
  tft.fillRect(0, kToolbarY, 240, kToolbarH, UI_FG);
  tft.drawBitmap(kIconBackX, kToolbarY, bitmap_icon_go_back, kIconSize, kIconSize, UI_ICON);
  if (s_scr != AScreen::Details)
    tft.drawBitmap(kIconReloadX, kToolbarY, bitmap_icon_undo, kIconSize, kIconSize, UI_ICON);
  tft.drawFastHLine(0, kToolbarBottom, 240, UI_LINE);
}

static void drawList() {
  tft.fillRect(0, kBodyTop, 240, irContentBottom() - kBodyTop, FEATURE_BG);
  tft.setTextFont(1); tft.setTextSize(1);
  if (s_rows.empty()) {
    tft.setTextColor(UI_WARN, FEATURE_BG);
    tft.setCursor(kPadX, listTopY() + 4);
    tft.print("Sem controles A/C");
    return;
  }
  ensureVisible();
  const int rows = listRows();
  const int baseY = listTopY();
  const int innerW = 240 - 2 * kPadX;
  for (int r = 0; r < rows; r++) {
    const int ri = s_top + r;
    if (ri >= (int)s_rows.size()) break;
    const Row& row = s_rows[ri];
    const int ry = baseY + r * LIST_ROW_H;
    if (row.header) {
      tft.setTextColor(UI_ICON, FEATURE_BG);
      tft.setCursor(kPadX, ry + 6);
      tft.print(row.label);
      tft.drawFastHLine(kPadX, ry + LIST_ROW_H - 2, innerW, UI_LINE);
      continue;
    }
    const AcProfile& p = s_profs[row.idx];
    const bool sel = (ri == s_sel);
    const uint16_t bg = sel ? UI_FG : FEATURE_BG;
    const uint16_t fg = sel ? UI_ICON : UI_TEXT;
    tft.fillRect(kPadX, ry, innerW, LIST_ROW_H - 1, bg);
    const char* tag = p.fromSd ? "SD" : "BI";
    const int tagW = tft.textWidth(tag, 1);
    tft.setTextColor(p.fromSd ? UI_OK : UI_DIM_TEXT, bg);
    tft.setCursor(kPadX + innerW - tagW - 2, ry + 6);
    tft.print(tag);
    tft.setTextColor(fg, bg);
    tft.setCursor(kPadX + 4, ry + 6);
    tft.print(acTrunc(p.name, innerW - tagW - 14));
  }
}

static void drawDetails() {
  tft.fillRect(0, kBodyTop, 240, irContentBottom() - kBodyTop, FEATURE_BG);
  if (s_selProf < 0 || s_selProf >= (int)s_profs.size()) return;
  const AcProfile& p = s_profs[s_selProf];
  tft.setTextFont(1); tft.setTextSize(1);
  int y = kBodyTop + 8;
  const int step = 18;
  const int valX = kPadX + 80;
  auto line = [&](const char* label, const String& val, uint16_t vc) {
    tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    tft.setCursor(kPadX, y); tft.print(label);
    tft.setTextColor(vc, FEATURE_BG);
    tft.setCursor(valX, y);
    tft.print(acTrunc(val.length() ? val : String("-"), 240 - valX - kPadX));
    y += step;
  };
  line("Nome:",     p.name, UI_TEXT);
  line("Marca:",    p.brand, UI_TEXT);
  line("Modelo:",   String(lgModelName(p.lgModel)), UI_OK);
  line("Protocolo:",String(lgProtoName(p.lgModel)), UI_TEXT);
  line("Temp:",     String(kLgAcMinTemp) + "-" + String(kLgAcMaxTemp) + "C", UI_TEXT);
  line("Modos:",    "Cool/Dry/Fan/Auto/Heat", UI_TEXT);
  line("Fonte:",    p.fromSd ? String("SD card") : String("Built-in"), p.fromSd ? UI_OK : UI_DIM_TEXT);
}

// Desenha um botao do controle AC; selecionado = Primary (vermelho, como o PWR).
static void acDrawBtn(int i, bool sel) {
  const FeatureUI::Button& b = s_btn[i];
  FeatureUI::drawButtonRect(b.x, b.y, b.w, b.h, b.label,
                            sel ? FeatureUI::ButtonStyle::Primary : FeatureUI::ButtonStyle::Secondary,
                            false, b.disabled, 2);
}

static void acLayout() {
  const int top = kBodyTop + 24;
  const int bottom = irContentBottom() - 6;
  const int unit = (bottom - top) / 5;
  const int bh = unit - 6;
  const int sideW = 80, centerW = 96;
  const int xL = 8, xR = 240 - 8 - sideW, xC = (240 - centerW) / 2;
  int y = top;
  s_btn[B_PWR]    = { (int16_t)xL,(int16_t)y,(int16_t)sideW,(int16_t)bh,"PWR", FeatureUI::ButtonStyle::Primary,  false };
  s_btn[B_MODE]   = { (int16_t)xR,(int16_t)y,(int16_t)sideW,(int16_t)bh,"MODE",FeatureUI::ButtonStyle::Secondary,false };
  y += unit;
  s_btn[B_TEMPUP] = { (int16_t)xC,(int16_t)y,(int16_t)centerW,(int16_t)bh,"TEMP +",FeatureUI::ButtonStyle::Secondary,false };
  y += unit;
  s_btn[B_TEMPDN] = { (int16_t)xC,(int16_t)y,(int16_t)centerW,(int16_t)bh,"TEMP -",FeatureUI::ButtonStyle::Secondary,false };
  y += unit;
  s_btn[B_SWING]  = { (int16_t)xL,(int16_t)y,(int16_t)sideW,(int16_t)bh,"SWING",FeatureUI::ButtonStyle::Secondary,false };
  s_btn[B_SPEED]  = { (int16_t)xR,(int16_t)y,(int16_t)sideW,(int16_t)bh,"SPEED",FeatureUI::ButtonStyle::Secondary,false };
  y += unit;
  s_btn[B_LIGHT]  = { (int16_t)xC,(int16_t)y,(int16_t)centerW,(int16_t)bh,"LIGHT",FeatureUI::ButtonStyle::Secondary,false };
}

static void drawControl() {
  tft.fillRect(0, kBodyTop, 240, irContentBottom() - kBodyTop, FEATURE_BG);
  if (s_profIdx < 0 || s_profIdx >= (int)s_profs.size()) return;
  const AcProfile& p = s_profs[s_profIdx];
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(kPadX, kBodyTop + 3);
  tft.print(acTrunc(p.name, 240 - 2 * kPadX));
  String stline = s_st.power
      ? (String(modeName(s_st.mode)) + "  " + String(s_st.temp) + "C  Fan:" + fanName(s_st.fan)
         + (s_swingIdx >= 0 ? (String("  Aba#") + String(s_swingIdx + 1)) : String("")))
      : String("Desligado");
  tft.setTextColor(s_st.power ? UI_OK : UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kPadX, kBodyTop + 13);
  tft.print(acTrunc(stline, 240 - 2 * kPadX));
  acLayout();
  for (int i = 0; i < B_COUNT; i++) acDrawBtn(i, i == s_navSel);
}

static void drawFooter() {
  if (s_scr == AScreen::List)
    setTouchNavLabels("Voltar", "Baixo", "Load", "Cima", "Detalhes");
  else if (s_scr == AScreen::Details)
    setTouchNavLabels("Voltar", "", "", "", "");
  else
    setTouchNavLabels("Voltar", "", "OK", "", "");
  irRedrawNavChrome();
}

static void drawAll() {
  irClearBody(FEATURE_BG);
  drawStatusBar(readBatteryVoltage(), true);
  drawToolbar();
  if (s_scr == AScreen::List)         drawList();
  else if (s_scr == AScreen::Details) drawDetails();
  else                                drawControl();
  drawFooter();
  s_drawn = true;
}

// ---------- acoes ----------
static void doReload() {
  refreshAc();
  rebuildOrder();
  rebuildRecent();
  rebuildRows();
  s_top = 0;
  s_drawn = false;
}
static void goList()  { s_scr = AScreen::List; s_drawn = false; }
static void openDetails() {
  if (s_rows.empty()) return;
  const Row& row = s_rows[s_sel];
  if (row.header || row.idx < 0) return;
  s_selProf = row.idx;
  s_scr = AScreen::Details;
  s_drawn = false;
}
static void loadSelected() {
  if (s_rows.empty()) return;
  const Row& row = s_rows[s_sel];
  if (row.header || row.idx < 0) return;
  s_profIdx = row.idx;
  pushRecent(s_profs[s_profIdx].name);
  rebuildRecent(); rebuildRows();
  // reinicia estado ao abrir um controle
  s_st = AcState();
  s_swingIdx = -1;
  s_lg.stateReset();
  s_lg.setModel(s_profs[s_profIdx].lgModel);
  s_navSel = 0;   // PWR
  s_scr = AScreen::Control;
  s_drawn = false;
}

static void ctrlGo(int ni) {
  if (ni < 0 || ni == s_navSel) return;
  const int o = s_navSel;
  s_navSel = ni;
  acDrawBtn(o, false);
  acDrawBtn(s_navSel, true);
}

static void ctrlNav(int dir) {
  ctrlGo(irNavSelect(s_btn, (int)B_COUNT, s_navSel, dir));
}

static void goList();
static void ctrlLeft() {
  const int ni = irNavSelect(s_btn, (int)B_COUNT, s_navSel, 2);
  if (ni != s_navSel) { ctrlGo(ni); return; }
  if (irIsLeftEdge(s_btn, (int)B_COUNT, s_navSel)) goList();  // so volta na coluna mais a esq
}

static void ctrlActivate() {
  if (s_navSel < 0 || s_navSel >= (int)B_COUNT) return;
  acDrawBtn(s_navSel, true);
  acButton(s_navSel);
  delay(90);
  drawControl();   // atualiza linha de estado + mantem selecao
}

static bool handleToolbarTouch(int x, int y) {
  if (y <= kToolbarY || y >= kToolbarBottom) return false;
  if (x > kIconBackX && x < kIconBackX + kIconSize) {
    if (s_scr == AScreen::List) feature_exit_requested = true;
    else goList();
    return true;
  }
  if (s_scr != AScreen::Details && x > kIconReloadX && x < kIconReloadX + kIconSize) {
    doReload();
    return true;
  }
  return false;
}

void setup() {
  setTouchButtonInputEnabled(true);
  s_lg.begin();
  s_rawSend.begin();
  s_scr = AScreen::List;
  s_profIdx = 0;
  s_drawn = false;
  loadRecentNames();
  refreshAc();
  rebuildOrder();
  rebuildRecent();
  rebuildRows();
  s_sel = firstSelectable();
  s_top = 0;
  drawAll();
}

void loop() {
  if (feature_active && feature_exit_requested) {
    return;
  }
  if (!s_drawn) drawAll();
  maintainTouchNavBar();

  if (s_scr == AScreen::List) {
    if (isButtonPressedEdge(BTN_UP))     { moveSel(-1); drawList(); }
    if (isButtonPressedEdge(BTN_DOWN))   { moveSel(+1); drawList(); }
    if (isButtonPressedEdge(BTN_LEFT))   { feature_exit_requested = true; }   // volta/sai
    if (isButtonPressedEdge(BTN_RIGHT))  { openDetails(); }
    if (isButtonPressedEdge(BTN_SELECT)) { loadSelected(); }                  // meio = Load
  } else if (s_scr == AScreen::Details) {
    if (isButtonPressedEdge(BTN_LEFT))   { goList(); }
  } else { // Control: setas movem (espacial); esq no canto = Voltar; meio aciona
    const bool nUp = isButtonPressedEdge(BTN_UP);
    const bool nDn = isButtonPressedEdge(BTN_DOWN);
    const bool nRt = isButtonPressedEdge(BTN_RIGHT);
    const bool nLf = isButtonPressedEdge(BTN_LEFT);
    const bool nOk = isButtonPressedEdge(BTN_SELECT);
    static uint32_t lastNavMs = 0;
    const uint32_t nowNav = millis();
    if ((uint32_t)(nowNav - lastNavMs) > 160) {   // anti-bounce (1 acao por toque)
      if      (nUp) { ctrlNav(0);     lastNavMs = nowNav; }
      else if (nDn) { ctrlNav(1);     lastNavMs = nowNav; }
      else if (nRt) { ctrlNav(3);     lastNavMs = nowNav; }
      else if (nLf) { ctrlLeft();     lastNavMs = nowNav; }
      else if (nOk) { ctrlActivate(); lastNavMs = nowNav; }
    }
  }

  static uint32_t lastTouchMs = 0;
  static bool touchWas = false;
  const uint32_t now = millis();
  const bool touchNow = isTouchDownDismiss();
  constexpr uint32_t kTouchDebounceMs = 280;
  if (touchNow && !touchWas) {
    int x, y;
    touchWas = true;
    if (!readTouchXY(x, y)) return;
    if ((uint32_t)(now - lastTouchMs) < kTouchDebounceMs) return;
    lastTouchMs = now;
    if (handleToolbarTouch(x, y)) return;

    if (s_scr == AScreen::List) {
      const int rows = listRows();
      const int ty = listTopY();
      if (y >= ty && y < ty + rows * LIST_ROW_H) {
        const int ri = s_top + (y - ty) / LIST_ROW_H;
        if (ri >= 0 && ri < (int)s_rows.size() && !s_rows[ri].header) {
          if (ri == s_sel) loadSelected();
          else { s_sel = ri; drawList(); }
        }
      }
    } else if (s_scr == AScreen::Control) {
      const int k = FeatureUI::hit(s_btn, (int)B_COUNT, x, y);
      if (k >= 0) {
        s_navSel = k;
        acDrawBtn(k, true);
        acButton(k);
        delay(90);
        drawControl();   // atualiza linha de estado + botoes
      }
    }
  }
  if (!touchNow) touchWas = false;

  delay(10);
}

}  // namespace IRUniversalAC

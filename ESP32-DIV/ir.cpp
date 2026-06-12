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
  setTouchNavLabels("Prev", "Browse", "Exit", "Reload", "Next");
}

static void irSetUniversalBrowseNavLabels() {
  setTouchNavLabels("Back", "Next", "Exit", "Prev", "Select");
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

namespace IRUniversalController {

static constexpr const char* PROFILES_PATH = "/ir_profiles.json";
static constexpr const char* PROFILES_DIR  = "/ir_profiles";

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
    default:    return "?";
  }
}

struct Profile {
  String name;
  String category;
  String brand;
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

  return false;
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
    p.proto = decode_type_t::SAMSUNG36;
    p.bits = 32;
    p.code[Power] = 0xE0E040BFu; p.has[Power] = true;
    p.code[Mute]  = 0xE0E0F00Fu; p.has[Mute]  = true;
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
    p.proto = decode_type_t::NEC;
    p.bits = 32;
    p.code[Power] = 0x20DF10EFu; p.has[Power] = true;
    p.code[Mute]  = 0x20DF906Fu; p.has[Mute]  = true;
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

  {
    Profile p{};
    p.name = "Sony TV (common)";
    p.proto = decode_type_t::SONY;
    p.bits = 12;
    p.code[Power] = 0x0A90u; p.has[Power] = true;
    p.code[Mute]  = 0x0290u; p.has[Mute]  = true;
    p.code[VolUp] = 0x0490u; p.has[VolUp] = true;
    p.code[VolDn] = 0x0C90u; p.has[VolDn] = true;
    p.code[ChUp]  = 0x0090u; p.has[ChUp]  = true;
    p.code[ChDn]  = 0x0890u; p.has[ChDn]  = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Xbox 360";
    p.proto = decode_type_t::RC6;
    p.bits = 36;
    p.code[Power] = 0x0C800F740Cu; p.has[Power] = true;  
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Epson Projector";
    p.proto = decode_type_t::EPSON;
    p.bits = 32;
    p.code[Power] = 0xC1AA09F6u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "JVC VCR";
    p.proto = decode_type_t::JVC;
    p.bits = 16;
    p.code[Power] = 0xC2B8u; p.has[Power] = true; 
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Denon AVR";
    p.proto = decode_type_t::DENON;
    p.bits = 15;
    p.code[Power] = 0x2278u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Denon AVR";
    p.proto = decode_type_t::DENON;
    p.bits = 48;
    p.code[Power] = 0x2A4C028D6CE3ULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Panasonic";
    p.proto = decode_type_t::PANASONIC;
    p.bits = 48;
    p.code[Power] = 0x40040190ED7CULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "Pioneer";
    p.proto = decode_type_t::PIONEER;
    p.bits = 64;
    p.code[Power] = 0x55FF00AAAA00FF55ULL; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "DISH";
    p.proto = decode_type_t::DISH;
    p.bits = 16;
    p.code[Power] = 0x9C00u; p.has[Power] = true;
    s_profiles.push_back(p);
  }

  {
    Profile p{};
    p.name = "GI Cable";
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

static bool parseProfilesJson(File& f, size_t docCapacity, String* errOut = nullptr) {

  if (docCapacity < 512) docCapacity = 512;
  DynamicJsonDocument doc(docCapacity);
  DeserializationError err = deserializeJson(doc, f);
  if (err) { if (errOut) *errOut = "JSON parse failed"; return false; }

  JsonArray arr = doc["profiles"].as<JsonArray>();
  if (!arr.isNull()) {
    for (JsonVariant v : arr) {
      JsonObject o = v.as<JsonObject>();
      if (o.isNull()) continue;

      const char* nm = o["name"] | "";
      const char* pr = o["protocol"] | "";
      const char* cat = o["category"] | "";
      const char* br  = o["brand"] | "";
      decode_type_t proto = decode_type_t::UNKNOWN;
      if (!protoFromToken(String(pr), proto)) continue;

      Profile p{};
      p.name = String(nm);
      if (!p.name.length()) p.name = String(pr);
      p.category = String(cat);
      p.brand = String(br);
      if (!p.category.length()) p.category = inferCategoryFromName(p.name);
      if (!p.brand.length()) p.brand = inferBrandFromName(p.name);
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

      s_profiles.push_back(p);
      if ((int)s_profiles.size() >= (int)kMaxProfiles) break;
    }
    return !s_profiles.empty();
  }

  JsonObject o = doc.as<JsonObject>();
  if (!o.isNull() && (o.containsKey("name") || o.containsKey("protocol"))) {
    const char* nm = o["name"] | "";
    const char* pr = o["protocol"] | "";
    const char* cat = o["category"] | "";
    const char* br  = o["brand"] | "";
    decode_type_t proto = decode_type_t::UNKNOWN;
    if (!protoFromToken(String(pr), proto)) { if (errOut) *errOut = "Bad protocol"; return false; }

    Profile p{};
    p.name = String(nm);
    if (!p.name.length()) p.name = String(pr);
    p.category = String(cat);
    p.brand = String(br);
    if (!p.category.length()) p.category = inferCategoryFromName(p.name);
    if (!p.brand.length()) p.brand = inferBrandFromName(p.name);
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
    s_profiles.push_back(p);
    return true;
  }

  if (errOut) *errOut = "No profiles";
  return false;
}

static bool loadProfilesFromSd(String* errOut = nullptr) {
  if (!isSDCardAvailable()) { if (errOut) *errOut = "SD not available"; return false; }
  s_profiles.clear();

  if (SD.exists(PROFILES_PATH)) {
    File f = SD.open(PROFILES_PATH, FILE_READ);
    if (!f) { if (errOut) *errOut = "Open /ir_profiles.json failed"; return false; }
    String perr;

    bool ok = parseProfilesJson(f, 8192, &perr);
    f.close();
    if (!ok) { if (errOut) *errOut = perr; return false; }
    if (errOut) *errOut = String("Loaded ") + String((int)s_profiles.size()) + " profiles";
    return true;
  }

  if (!SD.exists(PROFILES_DIR)) { if (errOut) *errOut = "No /ir_profiles.json or /ir_profiles/"; return false; }
  File d = SD.open(PROFILES_DIR);
  if (!d) { if (errOut) *errOut = "Open /ir_profiles failed"; return false; }

  uint16_t loaded = 0;
  for (;;) {
    File f = d.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String name = String(f.name());
      if (endsWith(name, ".json")) {
        String perr;
        if (parseProfilesJson(f, 2048, &perr)) {
          loaded++;
          if ((int)s_profiles.size() >= (int)kMaxProfiles) { f.close(); break; }
        }
      }
    }
    f.close();
  }
  d.close();

  if (s_profiles.empty()) { if (errOut) *errOut = "No profiles in /ir_profiles/"; return false; }
  if (errOut) *errOut = String("Loaded ") + String((int)s_profiles.size()) + " profiles";
  return true;
}

static void refreshProfiles() {
  s_lastErr = "";
  s_loadedFromSd = false;

  String err;
  if (loadProfilesFromSd(&err)) {
    s_loadedFromSd = true;
    s_lastErr = String("SD: ") + (err.length() ? err : String((int)s_profiles.size()));
  } else {
    loadBuiltinProfiles();
    s_loadedFromSd = false;
    if (err.length()) s_lastErr = "Built-in (SD: " + err + ")";
    else s_lastErr = "Built-in";
  }

  if (s_profileIdx < 0) s_profileIdx = 0;
  if (s_profileIdx >= (int)s_profiles.size()) s_profileIdx = 0;
}

static void browseBack();
static void openCategoryBrowser();
static void reloadUniversalProfiles();

static void drawToolbar() {
  tft.fillRect(0, kToolbarY, 240, kToolbarH, UI_FG);
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  tft.drawBitmap(kIconBackX, kToolbarY, bitmap_icon_go_back, kIconSize, kIconSize, UI_ICON);

  if (s_screen == Screen::Remote) {
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
  tft.setTextColor(s_loadedFromSd ? UI_OK : UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(kDetailValueX, kInfoTextY + (kDetailLineStep * 2));
  tft.print(s_loadedFromSd ? "SD card" : "Built-in");
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

  const int gap = 6;
  const int topBtnH = 34;
  const int topBtnW = (kInfoBoxW - 2 * gap) / 3;
  const int yTop = topY;
  const int x0 = kInfoBoxX;
  const int x1 = x0 + topBtnW + gap;
  const int x2 = kInfoBoxRight - topBtnW;
  s_keyBtns[(int)Power] = {(int16_t)x0,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Power),FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Mute]  = {(int16_t)x1,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Mute), FeatureUI::ButtonStyle::Secondary,true};
  s_keyBtns[(int)Back]  = {(int16_t)x2,(int16_t)yTop,(int16_t)topBtnW,(int16_t)topBtnH,keyLabel(Back), FeatureUI::ButtonStyle::Secondary,true};

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
  int n = (int)s_profiles.size();
  s_profileIdx = (s_profileIdx + delta) % n;
  if (s_profileIdx < 0) s_profileIdx += n;
  drawInfoPanel();
  drawKeys();
}

static void openCategoryBrowser() {
  s_screen = Screen::Category;
  s_listSel = 0;
  rebuildCategoriesAndBrands();
  s_uiDrawn = false;
}

static void reloadUniversalProfiles() {
  refreshProfiles();
  rebuildCategoriesAndBrands();
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
      openCategoryBrowser();
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

  if (!s_uiDrawn) drawAll();
  maintainTouchNavBar();
  handleUniversalNavButtons();

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
}

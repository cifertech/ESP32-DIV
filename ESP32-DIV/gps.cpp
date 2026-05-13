#include <Arduino.h>
#include <HardwareSerial.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "BleCompat.h"

#include "gps.h"
#include "icon.h"
#include "KeyboardUI.h"
#include "shared.h"
#include "Touchscreen.h"
#include "utils.h"

#ifndef GPS_UART_NUM
#define GPS_UART_NUM 2
#endif
#ifndef GPS_UART_BAUD
#define GPS_UART_BAUD 9600
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static HardwareSerial gpsSerial(GPS_UART_NUM);
static TFT_eSprite gScanPanel(&tft);
static bool gScanPanelReady = false;
static int gScanPanelAllocW = 0;
static int gScanPanelAllocH = 0;

static constexpr int kScanSpriteHMin = 200;

static bool ensureScanPanelSprite(int panelW, int panelH) {
  if (panelW <= 0 || panelH <= 32) {
    return false;
  }
  if (gScanPanelReady && panelW == gScanPanelAllocW && gScanPanelAllocH <= panelH &&
      gScanPanelAllocH >= kScanSpriteHMin) {
    return true;
  }

  if (gScanPanelReady) {
    gScanPanel.deleteSprite();
    gScanPanelReady = false;
    gScanPanelAllocW = gScanPanelAllocH = 0;
  }

  for (int h = panelH; h >= kScanSpriteHMin; h -= 18) {
    for (uint8_t bpp : {8, 16}) {
      gScanPanel.setColorDepth(bpp);
      if (gScanPanel.createSprite(panelW, h)) {
        gScanPanelReady = true;
        gScanPanelAllocW = panelW;
        gScanPanelAllocH = h;
        return true;
      }
      delay(20);
    }
  }

  gScanPanel.setColorDepth(16);
  gScanPanelAllocW = gScanPanelAllocH = 0;
  return false;
}

static bool gWardSuppressBottomTouchExit = false;

namespace {

constexpr size_t kLineCap = 128;
constexpr uint32_t kRedrawMs = 450;

constexpr int kGfxTop = 22;
constexpr int kMaxSatRows = 48;
constexpr int kSnrBarMax = 50;

constexpr int kSkyRDefault = 52;
constexpr int kSkyCyDefault = 110;
constexpr int kSkyCyBig = 118;
constexpr int kSkyRBig = 70;
constexpr int kListRowsVisible = 9;

constexpr int kUiPad = 5;
constexpr int kUiGap = 3;
constexpr int kHeaderY = 2;
constexpr int kTabsY = 24;
constexpr int kCardsY = 46;
constexpr int kCardH = 29;
constexpr int kBodyTop = 82;

constexpr uint32_t kNavCooldownMs = 520;

char lineBuf[kLineCap];
size_t lineLen = 0;

enum : uint8_t {
  SYS_GPS = 0,
  SYS_GLO,
  SYS_GAL,
  SYS_BDS,
  SYS_QZS,
  SYS_UNK,
};

struct SatRow {
  uint8_t prn;
  uint8_t system;
  int16_t elev;
  int16_t azim;
  int16_t snr;
  bool inSolution;
};

SatRow rows[kMaxSatRows];
int rowCount = 0;
uint32_t lastGsvMs = 0;

enum class ViewMode : uint8_t { Combined = 0, SkyOnly, ListOnly };
ViewMode viewMode = ViewMode::Combined;
int listScroll = 0;
uint32_t lastViewBtnMs = 0;

static bool prevHwLeft = false;
static bool prevHwRight = false;
static bool prevHwUp = false;
static bool prevHwDown = false;
static bool touchNavArm = true;

char utcStr[12] = "--:--:--";
char dateStr[12] = "--/--/--";
float hdopLive = -1.f;
float pdopLive = -1.f;
uint8_t fixQuality = 0;
uint8_t gsaFixMode = 1;
uint8_t satsUsedGga = 0;
double navLat = NAN;
double navLon = NAN;
float navAltM = NAN;
bool rmcNavValid = false;
uint32_t lastNavMs = 0;

void stripChecksum(char* s) {
  char* star = strchr(s, '*');
  if (star) {
    *star = '\0';
  }
  char* cr = strchr(s, '\r');
  if (cr) {
    *cr = '\0';
  }
}

int splitCommaFields(char* s, const char** fields, int maxFields) {
  int nf = 0;
  fields[nf++] = s;
  for (; *s && nf < maxFields; ++s) {
    if (*s == ',') {
      *s = '\0';
      fields[nf++] = s + 1;
    }
  }
  return nf;
}

uint8_t talkerSystem(char c1, char c2) {
  if (c1 == 'G' && c2 == 'P') {
    return SYS_GPS;
  }
  if (c1 == 'G' && c2 == 'L') {
    return SYS_GLO;
  }
  if (c1 == 'G' && c2 == 'A') {
    return SYS_GAL;
  }
  if (c1 == 'G' && c2 == 'B') {
    return SYS_BDS;
  }
  if (c1 == 'G' && c2 == 'Q') {
    return SYS_QZS;
  }
  if (c1 == 'G' && c2 == 'N') {
    return SYS_GPS;
  }
  return SYS_UNK;
}

const char* systemShortTag(uint8_t sys) {
  switch (sys) {
    case SYS_GPS:
      return "GPS";
    case SYS_GLO:
      return "GLO";
    case SYS_GAL:
      return "GAL";
    case SYS_BDS:
      return "BDS";
    case SYS_QZS:
      return "QZS";
    default:
      return "?";
  }
}

uint16_t baseColorForSystem(uint8_t sys) {
  switch (sys) {
    case SYS_GPS:
      return 0x07FF;
    case SYS_GLO:
      return 0xFA20;
    case SYS_GAL:
      return 0xAFE5;
    case SYS_BDS:
      return ORANGE;
    case SYS_QZS:
      return 0xFC9F;
    default:
      return LIGHT_GRAY;
  }
}

static uint16_t scaleRgb565(uint16_t c, int num, int den) {
  if (den <= 0) {
    return c;
  }
  uint16_t r = ((c >> 11) & 0x1F);
  uint16_t g = ((c >> 5) & 0x3F);
  uint16_t b = (c & 0x1F);
  r = (uint16_t)((r * num) / den);
  g = (uint16_t)((g * num) / den);
  b = (uint16_t)((b * num) / den);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t snrTint(uint16_t base, int16_t snr) {
  if (snr < 0) {
    return scaleRgb565(base, 10, 16);
  }
  int k = snr;
  if (k > 45) {
    k = 45;
  }
  uint8_t bump = (uint8_t)(k * 3 / 2);
  uint16_t r = ((base >> 11) & 0x1F);
  uint16_t g = ((base >> 5) & 0x3F);
  uint16_t b = (base & 0x1F);
  r = (uint16_t)min(31, (int)r + (bump >> 3));
  g = (uint16_t)min(63, (int)g + (bump >> 2));
  b = (uint16_t)min(31, (int)b + (bump >> 3));
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void parseOptionalInt16(const char* p, int16_t& out) {
  if (!p || !*p) {
    out = -1;
    return;
  }
  out = (int16_t)strtol(p, nullptr, 10);
}

double dmToDeg(const char* dm, char dir) {
  if (!dm || !*dm || (dir != 'N' && dir != 'S' && dir != 'E' && dir != 'W')) {
    return NAN;
  }
  double v = strtod(dm, nullptr);
  int deg = (int)(v / 100.0);
  double minutes = v - (double)deg * 100.0;
  double dec = (double)deg + minutes / 60.0;
  if (dir == 'S' || dir == 'W') {
    dec = -dec;
  }
  return dec;
}

void formatUtcFromField(const char* field) {
  if (!field || strlen(field) < 6) {
    strncpy(utcStr, "--:--:--", sizeof(utcStr));
    return;
  }
  int h = (field[0] - '0') * 10 + (field[1] - '0');
  int m = (field[2] - '0') * 10 + (field[3] - '0');
  int s = (field[4] - '0') * 10 + (field[5] - '0');
  snprintf(utcStr, sizeof(utcStr), "%02d:%02d:%02d", h, m, s);
}

void formatDateFromRmc(const char* field) {
  if (!field || strlen(field) < 6) {
    strncpy(dateStr, "--/--/--", sizeof(dateStr));
    return;
  }
  char dd[3] = {field[0], field[1], '\0'};
  char mm[3] = {field[2], field[3], '\0'};
  char yy[3] = {field[4], field[5], '\0'};
  snprintf(dateStr, sizeof(dateStr), "%s/%s/%s", dd, mm, yy);
}

void purgeConstellation(uint8_t sys) {
  int w = 0;
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i].system != sys) {
      rows[w++] = rows[i];
    }
  }
  rowCount = w;
}

void clearSolutionFlags(uint8_t sys) {
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i].system == sys) {
      rows[i].inSolution = false;
    }
  }
}

void markSolution(uint8_t sys, int prn) {
  if (prn <= 0 || prn > 255) {
    return;
  }
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i].system == sys && rows[i].prn == (uint8_t)prn) {
      rows[i].inSolution = true;
      break;
    }
  }
}

bool handleGsvSentence(char* sentence) {
  if (sentence[0] != '$' || strlen(sentence) < 12) {
    return false;
  }
  /* $xxGSV,t,m,n,... after talker IDs (GP, GN, GL, ...) */
  if (!(sentence[3] == 'G' && sentence[4] == 'S' && sentence[5] == 'V' &&
        sentence[6] == ',')) {
    return false;
  }

  stripChecksum(sentence);
  uint8_t sys = talkerSystem(sentence[1], sentence[2]);

  const char* fields[28];
  int nf = splitCommaFields(sentence, fields, (int)ARRAY_SIZE(fields));
  if (nf < 4) {
    return false;
  }

  int totalMsgs = (int)strtol(fields[1], nullptr, 10);
  int msgNum = (int)strtol(fields[2], nullptr, 10);
  (void)strtol(fields[3], nullptr, 10);

  if (totalMsgs >= 1 && msgNum == 1 && sys != SYS_UNK) {
    purgeConstellation(sys);
  }

  for (int i = 4; i + 3 < nf; i += 4) {
    int prn = (int)strtol(fields[i], nullptr, 10);
    if (prn <= 0 || prn > 255) {
      continue;
    }

    int16_t elev = -1, azim = -1, snr = -1;
    parseOptionalInt16(fields[i + 1], elev);
    parseOptionalInt16(fields[i + 2], azim);
    if (fields[i + 3][0] != '\0') {
      parseOptionalInt16(fields[i + 3], snr);
    }

    int idx = rowCount;
    for (int j = 0; j < rowCount; ++j) {
      if (rows[j].system == sys && rows[j].prn == (uint8_t)prn) {
        idx = j;
        break;
      }
    }
    if (idx == rowCount && rowCount < kMaxSatRows) {
      rowCount++;
    }
    if (idx < kMaxSatRows) {
      rows[idx].prn = (uint8_t)prn;
      rows[idx].system = sys;
      rows[idx].elev = elev;
      rows[idx].azim = azim;
      rows[idx].snr = snr;
    }
  }

  lastGsvMs = millis();
  return true;
}

bool handleGsaSentence(char* sentence) {
  if (sentence[0] != '$' || strlen(sentence) < 10) {
    return false;
  }
  if (!(sentence[3] == 'G' && sentence[4] == 'S' && sentence[5] == 'A' &&
        sentence[6] == ',')) {
    return false;
  }

  stripChecksum(sentence);
  uint8_t sys = talkerSystem(sentence[1], sentence[2]);

  const char* fields[24];
  int nf = splitCommaFields(sentence, fields, (int)ARRAY_SIZE(fields));
  if (nf < 7) {
    return false;
  }

  gsaFixMode = (uint8_t)strtol(fields[2], nullptr, 10);
  if (gsaFixMode > 3) {
    gsaFixMode = 1;
  }

  int dopBase = nf - 3;
  if (dopBase < 3) {
    return false;
  }

  clearSolutionFlags(sys);

  for (int i = 3; i < dopBase; ++i) {
    if (!fields[i][0]) {
      continue;
    }
    int prn = (int)strtol(fields[i], nullptr, 10);
    markSolution(sys, prn);
  }

  if (fields[dopBase][0]) {
    pdopLive = strtof(fields[dopBase], nullptr);
  }
  if (fields[dopBase + 1][0]) {
    hdopLive = strtof(fields[dopBase + 1], nullptr);
  }

  lastNavMs = millis();
  return true;
}

bool handleGgaSentence(char* sentence) {
  if (sentence[0] != '$' || strlen(sentence) < 14) {
    return false;
  }
  if (!(sentence[3] == 'G' && sentence[4] == 'G' && sentence[5] == 'A' &&
        sentence[6] == ',')) {
    return false;
  }

  stripChecksum(sentence);

  const char* fields[20];
  int nf = splitCommaFields(sentence, fields, (int)ARRAY_SIZE(fields));
  if (nf < 10) {
    return false;
  }

  formatUtcFromField(fields[1]);

  char ns = fields[3][0];
  char ew = fields[5][0];
  navLat = dmToDeg(fields[2], ns);
  navLon = dmToDeg(fields[4], ew);

  fixQuality = (uint8_t)strtol(fields[6], nullptr, 10);
  satsUsedGga = (uint8_t)strtol(fields[7], nullptr, 10);
  if (fields[8][0]) {
    float h = strtof(fields[8], nullptr);
    if (h >= 0.f && h < 99.f) {
      hdopLive = h;
    }
  }
  navAltM = NAN;
  if (fields[9][0]) {
    navAltM = strtof(fields[9], nullptr);
  }

  lastNavMs = millis();
  return true;
}

bool handleRmcSentence(char* sentence) {
  if (sentence[0] != '$' || strlen(sentence) < 12) {
    return false;
  }
  if (!(sentence[3] == 'R' && sentence[4] == 'M' && sentence[5] == 'C' &&
        sentence[6] == ',')) {
    return false;
  }

  stripChecksum(sentence);

  const char* fields[14];
  int nf = splitCommaFields(sentence, fields, (int)ARRAY_SIZE(fields));
  if (nf < 10) {
    return false;
  }

  formatUtcFromField(fields[1]);
  rmcNavValid = (fields[2][0] == 'A');

  char ns = fields[4][0];
  char ew = fields[6][0];
  navLat = dmToDeg(fields[3], ns);
  navLon = dmToDeg(fields[5], ew);

  formatDateFromRmc(fields[9]);

  lastNavMs = millis();
  return true;
}

void dispatchNmeaLine(char* line) {
  if (line[0] != '$' || strlen(line) < 7) {
    return;
  }
  char t4 = line[3];
  char t5 = line[4];
  char t6 = line[5];

  if (t4 == 'G' && t5 == 'S' && t6 == 'V') {
    handleGsvSentence(line);
    return;
  }
  if (t4 == 'G' && t5 == 'S' && t6 == 'A') {
    handleGsaSentence(line);
    return;
  }
  if (t4 == 'G' && t5 == 'G' && t6 == 'A') {
    handleGgaSentence(line);
    return;
  }
  if (t4 == 'R' && t5 == 'M' && t6 == 'C') {
    handleRmcSentence(line);
    return;
  }
}

static int snrSortKey(int16_t snr) {
  return snr < 0 ? -999 : (int)snr;
}

void sortRowsBySnr() {
  for (int i = 0; i + 1 < rowCount; ++i) {
    for (int j = i + 1; j < rowCount; ++j) {
      if (snrSortKey(rows[j].snr) > snrSortKey(rows[i].snr)) {
        SatRow t = rows[i];
        rows[i] = rows[j];
        rows[j] = t;
      }
    }
  }
}

static int countSnrKnown() {
  int n = 0;
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i].snr >= 0) {
      n++;
    }
  }
  return n;
}

int countInSolutionTotal() {
  int n = 0;
  for (int i = 0; i < rowCount; ++i) {
    if (rows[i].inSolution) {
      n++;
    }
  }
  return n;
}

void feedSerial() {
  while (gpsSerial.available()) {
    char c = (char)gpsSerial.read();
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) {
        dispatchNmeaLine(lineBuf);
      }
      lineLen = 0;
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (lineLen + 1 < kLineCap) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;
    }
  }
}

template<typename Gfx>
void drawFixBadgeGx(Gfx& g, const int PW, int x, int y) {
  const char* tag = "NOFIX";
  uint16_t fg = UI_WARN;
  if (rmcNavValid && fixQuality > 0 && gsaFixMode >= 2) {
    fg = UI_OK;
    tag = (gsaFixMode == 3) ? "FIX 3D" : "FIX 2D";
  } else if (fixQuality > 0 && gsaFixMode >= 2) {
    fg = UI_OK;
    tag = (gsaFixMode == 3) ? "FIX 3D" : "FIX 2D";
  }

  g.setTextFont(1);
  g.setTextSize(1);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(fg, FEATURE_BG);
  g.drawString(tag, x, y);
}

template<typename Gfx>
void drawSkyPlotGx(Gfx& g, int cx, int cy, int R) {
  g.drawCircle(cx, cy, R, UI_LINE);
  for (int el = 30; el <= 60; el += 30) {
    int rr = R * (90 - el) / 90;
    if (rr > 2) {
      g.drawCircle(cx, cy, rr, LINE_Dark);
    }
  }

  g.drawLine(cx, cy - R, cx, cy + R, LINE_Dark);
  g.drawLine(cx - R, cy, cx + R, cy, LINE_Dark);

  const int triGap = 9;
  const int triHalf = 4;
  const int triLen = 7;
  const int nTipY = cy - R - triGap;
  const int sTipY = cy + R + triGap;
  const int wTipX = cx - R - triGap;
  const int eTipX = cx + R + triGap;

  // Compass markers are intentionally symmetric; keep labels farther out.
  g.fillTriangle(cx, nTipY, cx - triHalf, nTipY + triLen,
                 cx + triHalf, nTipY + triLen, ORANGE);
  g.fillTriangle(cx, sTipY, cx - triHalf, sTipY - triLen,
                 cx + triHalf, sTipY - triLen, ORANGE);
  g.fillTriangle(wTipX, cy, wTipX + triLen, cy - triHalf,
                 wTipX + triLen, cy + triHalf, ORANGE);
  g.fillTriangle(eTipX, cy, eTipX - triLen, cy - triHalf,
                 eTipX - triLen, cy + triHalf, ORANGE);

  g.setTextFont(1);
  g.setTextSize(1);
  g.setTextDatum(MC_DATUM);
  g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  const int labelInset = 9;
  g.drawString("N", cx, cy - R + labelInset);
  g.drawString("S", cx, cy + R - labelInset);
  g.drawString("W", cx - R + labelInset, cy);
  g.drawString("E", cx + R - labelInset, cy);

  g.setTextDatum(TC_DATUM);
  g.drawString("Zenith", cx, cy - 10);

  for (int i = 0; i < rowCount; ++i) {
    int16_t el = rows[i].elev;
    int16_t az = rows[i].azim;
    bool posEstimated = false;

    if (az == 360) {
      az = 0;
    }

    /* Many receivers omit elev/az until the SV is tracked; still show a dot */
    if (el < 0 || el > 90 || az < 0 || az > 359) {
      uint32_t seed =
          ((uint32_t)rows[i].prn * 251u + 17u) ^
          ((uint32_t)rows[i].system * 131071u + (uint32_t)i * 65537u);
      seed ^= seed >> 16;
      seed *= 2654435761u;
      az = (int16_t)(seed % 360u);
      el = (int16_t)(12 + ((seed >> 11) % 34u)); // 12°–45° (inside horizon ring)
      posEstimated = true;
    }

    double azRad = (double)az * M_PI / 180.0;
    float rr = (float)R * (float)(90 - el) / 90.f;
    int sx = cx + (int)(rr * sin(azRad));
    int sy = cy - (int)(rr * cos(azRad));

    uint16_t bc = baseColorForSystem(rows[i].system);
    uint16_t fill = snrTint(bc, rows[i].snr);
    /* Elev/az missing: keep hue, wash slightly darker (not flat grey) */
    if (posEstimated) {
      fill = scaleRgb565(fill, 12, 16);
    }

    int mr = 4;
    if (!posEstimated && rows[i].snr >= 0) {
      mr += rows[i].snr / 20;
      if (mr > 7) {
        mr = 7;
      }
    }

    g.fillCircle(sx, sy, mr, fill);
    g.drawCircle(sx, sy, mr, posEstimated ? UI_DIM_TEXT : (uint16_t)BLACK);

    if (rows[i].inSolution) {
      g.drawCircle(sx, sy, mr + 2, WHITE);
    }
    // PRNs are listed in SNR strip — avoids overlapping labels on sky plot.
  }
}

template<typename Gfx>
void drawLegendStripGx(Gfx& g, const int PW, int y) {
  g.setTextFont(1);
  g.setTextSize(1);
  g.setTextDatum(TL_DATUM);

  struct {
    uint8_t sys;
    const char* name;
  } items[] = {{SYS_GPS, "GPS"}, {SYS_GLO, "GL"}, {SYS_GAL, "GA"},
               {SYS_BDS, "BD"}, {SYS_QZS, "QZ"}};

  int x = 4;
  for (unsigned i = 0; i < ARRAY_SIZE(items); ++i) {
    uint16_t c = baseColorForSystem(items[i].sys);
    g.fillRect(x, y + 1, 7, 7, c);
    g.drawRect(x, y + 1, 7, 7, UI_LINE);
    g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    g.drawString(items[i].name, x + 9, y);
    x += 36;
    if (x > PW - 50) {
      break;
    }
  }

  g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  g.drawString("ring=fix", x + 2, y);
}

template<typename Gfx>
void drawSnrTableGx(Gfx& g, const int PW, const int PH, int yStart, int maxRows,
                    int startIdx, int footerReserve) {
  sortRowsBySnr();
  const int barX = 54;
  const int barW = PW - barX - 30;
  const int rowH = 13;

  int y = yStart;
  int drawn = 0;
  const int ymax = PH - footerReserve;
  for (int i = startIdx; i < rowCount && drawn < maxRows; ++i) {
    if (y + rowH > ymax) {
      break;
    }

    char tag[14];
    snprintf(tag, sizeof(tag), "%s%02u", systemShortTag(rows[i].system),
             (unsigned)rows[i].prn);

    g.setTextFont(1);
    g.setTextSize(1);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(snrTint(baseColorForSystem(rows[i].system), rows[i].snr),
                   FEATURE_BG);
    g.drawString(tag, 2, y);

    int snr = rows[i].snr < 0 ? 0 : rows[i].snr;
    if (snr > kSnrBarMax) {
      snr = kSnrBarMax;
    }
    int fill = (barW * snr) / kSnrBarMax;

    g.fillRect(barX, y + 2, barW, 8, UI_LINE);
    if (fill > 0) {
      uint16_t bc = baseColorForSystem(rows[i].system);
      g.fillRect(barX, y + 2, fill, 8, snrTint(bc, rows[i].snr));
      if (rows[i].inSolution) {
        g.drawRect(barX, y + 2, fill, 8, WHITE);
      }
    }

    g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    char sb[8];
    if (rows[i].snr >= 0) {
      snprintf(sb, sizeof(sb), "%2d", rows[i].snr);
    } else {
      strncpy(sb, "--", sizeof(sb));
      sb[sizeof(sb) - 1] = '\0';
    }
    g.drawString(sb, barX + barW + 4, y);

    y += rowH;
    drawn++;
  }
}

template<typename Gfx>
void drawPanelFrameGx(Gfx& g, int x, int y, int w, int h, uint16_t outline) {
  g.fillRoundRect(x, y, w, h, 6, UI.bg == BG_Dark ? BLACK : UI_BG);
  g.drawRoundRect(x, y, w, h, 6, outline);
}

template<typename Gfx>
void drawMetricCardGx(Gfx& g, int x, int y, int w, const char* label,
                      const char* value, uint16_t valueColor,
                      const char* value2 = nullptr) {
  drawPanelFrameGx(g, x, y, w, kCardH, UI_LINE);
  g.setTextFont(1);
  g.setTextSize(1);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  g.drawString(label, x + 4, y + 2);
  g.setTextColor(valueColor, FEATURE_BG);
  if (value2 != nullptr && value2[0] != '\0') {
    g.drawString(value, x + 4, y + 11);
    g.drawString(value2, x + 4, y + 19);
  } else {
    g.drawString(value, x + 4, y + 15);
  }
}

/** Same x positions and width as drawMetricCardGx columns so tabs align with cards. */
template<typename Gfx>
void drawModeTabsGx(Gfx& g, int x0, int x1, int x2, int colW, int y) {
  const char* labels[] = {"Split", "Sky", "Signals"};
  const int xs[3] = {x0, x1, x2};
  const int active = (int)viewMode;
  constexpr int tabH = 15;

  for (int i = 0; i < 3; ++i) {
    const int x = xs[i];
    const bool selected = (i == active);
    uint16_t fill =
        selected ? UI_ACCENT : (UI.bg == BG_Dark ? DARK_GRAY : LIGHT_GRAY);
    uint16_t fg = selected ? WHITE : UI_DIM_TEXT;
    g.fillRoundRect(x, y, colW, tabH, 5, fill);
    g.drawRoundRect(x, y, colW, tabH, 5, selected ? UI_OK : UI_LINE);
    g.setTextFont(1);
    g.setTextSize(1);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(fg, fill);
    g.drawString(labels[i], x + colW / 2, y + tabH / 2);
  }
}

template<typename Gfx>
void drawFooterHintGx(Gfx& g, const int PW, const int PH, int footerH) {
  const int y = PH - footerH;
  g.fillRect(0, y, PW, footerH, UI.bg == BG_Dark ? DARK_GRAY : UI_LABLE);
  g.drawLine(0, y, PW, y, UI_ACCENT);

  g.setTextFont(1);
  g.setTextSize(1);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(UI_TEXT, UI.bg == BG_Dark ? DARK_GRAY : UI_LABLE);

  if (viewMode == ViewMode::ListOnly) {
    g.drawString("Touch: L/R view  center scroll", 5, y + 5);
  } else {
    g.drawString("Touch: edge L/R switches view", 5, y + 5);
  }

  g.setTextColor(UI_DIM_TEXT, UI.bg == BG_Dark ? DARK_GRAY : UI_LABLE);
  g.drawString("SELECT or bottom tap exits", 5, y + 18);
}

void clampListScroll() {
  if (listScroll < 0) {
    listScroll = 0;
  }
  int maxS = rowCount - kListRowsVisible;
  if (maxS < 0) {
    maxS = 0;
  }
  if (listScroll > maxS) {
    listScroll = maxS;
  }
}

static const char* viewModeTag(ViewMode m) {
  switch (m) {
    case ViewMode::Combined:
      return "Split";
    case ViewMode::SkyOnly:
      return "Sky";
    case ViewMode::ListOnly:
      return "Signals";
    default:
      return "?";
  }
}

static inline bool navCooldownElapsed(uint32_t t) {
  return ((uint32_t)(t - lastViewBtnMs)) >= kNavCooldownMs;
}

static void resetSatScannerNavInputState() {
  prevHwLeft = isButtonPressed(BTN_LEFT);
  prevHwRight = isButtonPressed(BTN_RIGHT);
  prevHwUp = isButtonPressed(BTN_UP);
  prevHwDown = isButtonPressed(BTN_DOWN);
  touchNavArm = !ts.touched();
}

/** @return True if redraw should occur immediately */
bool pollViewNavigation() {
  uint32_t t = millis();
  bool redraw = false;

  auto bumpMode = [&](int d) {
    int v = (int)viewMode + d;
    v = (v % 3 + 3) % 3;
    viewMode = (ViewMode)(uint8_t)v;
    listScroll = 0;
    lastViewBtnMs = t;
    redraw = true;
  };

  const bool L = isButtonPressed(BTN_LEFT);
  const bool R = isButtonPressed(BTN_RIGHT);
  const bool U = isButtonPressed(BTN_UP);
  const bool D = isButtonPressed(BTN_DOWN);

  const bool edgeL = L && !prevHwLeft;
  const bool edgeR = R && !prevHwRight;
  const bool edgeU = U && !prevHwUp;
  const bool edgeD = D && !prevHwDown;

  prevHwLeft = L;
  prevHwRight = R;
  prevHwUp = U;
  prevHwDown = D;

  if (navCooldownElapsed(t)) {
    if (edgeR) {
      bumpMode(1);
    } else if (edgeL) {
      bumpMode(-1);
    } else if (viewMode == ViewMode::ListOnly && edgeU) {
      if (listScroll > 0) {
        listScroll--;
      }
      lastViewBtnMs = t;
      redraw = true;
    } else if (viewMode == ViewMode::ListOnly && edgeD) {
      clampListScroll();
      int maxS = rowCount - kListRowsVisible;
      if (maxS < 0) {
        maxS = 0;
      }
      if (listScroll < maxS) {
        listScroll++;
      }
      lastViewBtnMs = t;
      redraw = true;
    }
  }

  if (!ts.touched()) {
    touchNavArm = true;
    return redraw;
  }

  int tx = 0, ty = 0;
  if (!readTouchXY(tx, ty)) {
    return redraw;
  }

  if (ty < kGfxTop || ty >= (int)tft.height() - 52) {
    return redraw;
  }

  if (!touchNavArm || !navCooldownElapsed(t)) {
    return redraw;
  }

  const int PW = tft.width();
  const int PH = gScanPanelReady ? gScanPanel.height() : (tft.height() - kGfxTop);
  const int px = tx;
  const int py = ty - kGfxTop;
  if (py < 0 || py >= PH) {
    return redraw;
  }

  /* Avoid bottom strip (exit hint overlaps panel bottom). */
  if (py >= PH - 40) {
    return redraw;
  }

  const int xThird = PW / 3;

  touchNavArm = false;

  if (px < xThird) {
    bumpMode(-1);
  } else if (px >= xThird * 2) {
    bumpMode(1);
  } else if (viewMode == ViewMode::ListOnly) {
    lastViewBtnMs = t;
    const int mid = PH / 2;
    if (py < mid) {
      if (listScroll > 0) {
        listScroll--;
      }
    } else {
      clampListScroll();
      int maxS = rowCount - kListRowsVisible;
      if (maxS < 0) {
        maxS = 0;
      }
      if (listScroll < maxS) {
        listScroll++;
      }
    }
    redraw = true;
  } else {
    /* Center tap Split/Sky: no-op; finger up will re-arm touchNavArm */
    touchNavArm = true;
  }

  return redraw;
}

/** All Y coordinates relative to sprite top (below status bar). */
template<typename Gfx>
void renderPanelGx(Gfx& g) {
  const int PW = g.width();
  const int PH = g.height();

  constexpr int footerH = 34;
  const int cardW = (PW - (kUiPad * 2) - (kUiGap * 2)) / 3;
  const int cardX0 = kUiPad;
  const int cardX1 = cardX0 + cardW + kUiGap;
  const int cardX2 = cardX1 + cardW + kUiGap;
  g.fillScreen(FEATURE_BG);

  int skyCy = kSkyCyDefault;
  int skyR = kSkyRDefault;
  uint32_t now = millis();
  const bool staleGsv =
      (lastGsvMs != 0) && (((uint32_t)(now - lastGsvMs)) > 8000);

  g.setTextFont(2);
  g.setTextSize(1);
  g.setTextDatum(TL_DATUM);
  g.setTextColor(UI_TEXT, FEATURE_BG);
  g.drawString("GNSS Satellites", kUiPad, kHeaderY);

  drawModeTabsGx(g, cardX0, cardX1, cardX2, cardW, kTabsY);

  drawFixBadgeGx(g, PW, PW - 48, 5);

  if (staleGsv) {
    g.setTextDatum(TR_DATUM);
    g.setTextColor(UI_WARN, FEATURE_BG);
    g.drawString("no GSV", PW - 5, 16);
    g.setTextDatum(TL_DATUM);
  }

  if (viewMode == ViewMode::SkyOnly) {
    skyCy = 139;
    skyR = 74;
    g.setTextDatum(TL_DATUM);
    g.setTextFont(1);
    g.setTextSize(1);
    g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    g.drawString("Center=overhead  Edge=horizon", kUiPad, kBodyTop - 39);

    drawSkyPlotGx(g, PW / 2, skyCy, skyR);
    drawLegendStripGx(g, PW, skyCy + skyR + 16);

  } else if (viewMode == ViewMode::ListOnly) {
    clampListScroll();

    char lineUtc[42];
    snprintf(lineUtc, sizeof(lineUtc), "%s", utcStr);
    char dopLine1[16];
    char dopLine2[16];
    int inFix = countInSolutionTotal();
    if (hdopLive >= 0.f && pdopLive >= 0.f) {
      snprintf(dopLine1, sizeof(dopLine1), "H%.1f", hdopLive);
      snprintf(dopLine2, sizeof(dopLine2), "P%.1f", pdopLive);
    } else {
      dopLine2[0] = '\0';
      snprintf(dopLine1, sizeof(dopLine1), "SV%02d S%02d", rowCount,
               countSnrKnown());
    }

    drawMetricCardGx(g, cardX0, kCardsY, cardW, "UTC", lineUtc, UI_TEXT);
    drawMetricCardGx(g, cardX1, kCardsY, cardW, "DOP", dopLine1, UI_OK,
                     dopLine2[0] ? dopLine2 : nullptr);
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "U%u F%d", (unsigned)satsUsedGga,
             inFix);
    drawMetricCardGx(g, cardX2, kCardsY, cardW, "FIX", countBuf, UI_ACCENT);

    g.setTextFont(1);
    g.setTextSize(1);
    g.setTextDatum(TL_DATUM);
    g.setTextColor(UI_TEXT, FEATURE_BG);
    g.drawString("Strongest signals", kUiPad, kBodyTop);

    drawSnrTableGx(g, PW, PH, kBodyTop + 15, kListRowsVisible - 2,
                   listScroll, footerH);

  } else {
    char lineUtc[42];
    snprintf(lineUtc, sizeof(lineUtc), "%s", utcStr);
    int inFix = countInSolutionTotal();
    char dopLine1[16];
    char dopLine2[16];
    dopLine2[0] = '\0';
    if (hdopLive >= 0.f && pdopLive >= 0.f) {
      snprintf(dopLine1, sizeof(dopLine1), "H%.1f", hdopLive);
      snprintf(dopLine2, sizeof(dopLine2), "P%.1f", pdopLive);
    } else if (hdopLive >= 0.f) {
      snprintf(dopLine1, sizeof(dopLine1), "H%.1f U%u", hdopLive,
               (unsigned)satsUsedGga);
    } else {
      snprintf(dopLine1, sizeof(dopLine1), "SV%02d S%02d", rowCount,
               countSnrKnown());
    }

    char fixBuf[12];
    snprintf(fixBuf, sizeof(fixBuf), "U%u F%d", (unsigned)satsUsedGga, inFix);
    drawMetricCardGx(g, cardX0, kCardsY, cardW, "UTC", lineUtc, UI_TEXT);
    drawMetricCardGx(g, cardX1, kCardsY, cardW, "DOP", dopLine1, UI_OK,
                     dopLine2[0] ? dopLine2 : nullptr);
    drawMetricCardGx(g, cardX2, kCardsY, cardW, "FIX", fixBuf, UI_ACCENT);

    if (!isnan(navLat) && !isnan(navLon)) {
      char posBuf[40];
      snprintf(posBuf, sizeof(posBuf), "%.4f  %.4f", navLat, navLon);
      g.setTextFont(1);
      g.setTextSize(1);
      g.setTextDatum(TL_DATUM);
      g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
      g.drawString(posBuf, kUiPad, kBodyTop - 3);
    } else {
      g.setTextFont(1);
      g.setTextSize(1);
      g.setTextDatum(TL_DATUM);
      g.setTextColor(UI_DIM_TEXT, FEATURE_BG);
      g.drawString("Waiting for position / GGA", kUiPad, kBodyTop - 3);
    }

    skyCy = 151;
    skyR = kSkyRDefault;

    drawSkyPlotGx(g, PW / 2, skyCy, skyR);

    int legendY = skyCy + skyR + 18;
    if (legendY > PH - 75) {
      legendY = PH - 74;
    }
    drawLegendStripGx(g, PW, legendY);
    drawSnrTableGx(g, PW, PH, legendY + 13, 2, 0, footerH);
  }

  drawFooterHintGx(g, PW, PH, footerH);
}

void redrawFeaturePanel(bool statusBarForceFull) {
  float v = readBatteryVoltage();
  drawStatusBar(v, statusBarForceFull);

  static uint32_t s_lastSpriteRetryMs = 0;
  if (!gScanPanelReady) {
    const uint32_t tn = millis();
    if ((uint32_t)(tn - s_lastSpriteRetryMs) >= 800u) {
      s_lastSpriteRetryMs = tn;
      (void)ensureScanPanelSprite(tft.width(), tft.height() - kGfxTop);
    }
  }

  if (gScanPanelReady) {
    renderPanelGx(gScanPanel);
    gScanPanel.pushSprite(0, kGfxTop);
    const int sh = gScanPanel.height();
    const int fullH = tft.height() - kGfxTop;
    if (sh < fullH) {
      tft.fillRect(0, kGfxTop + sh, tft.width(), fullH - sh, FEATURE_BG);
    }
    return;
  }

  tft.fillRect(0, kGfxTop, tft.width(), tft.height() - kGfxTop,
               FEATURE_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(UI_WARN, FEATURE_BG);
  tft.drawString("Display buffer failed", tft.width() / 2,
                 kGfxTop + (tft.height() - kGfxTop) / 2);
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
}

bool touchRequestsExit() {
  if (gWardSuppressBottomTouchExit) {
    return false;
  }
  if (!ts.touched()) {
    return false;
  }
  int x = 0, y = 0;
  if (!readTouchXY(x, y)) {
    return false;
  }
  return y >= (int)tft.height() - 48;
}

bool shouldExit() {
  return feature_exit_requested || isButtonPressed(BTN_SELECT) ||
         touchRequestsExit();
}

void drainButtons() {
  delay(180);
  for (int i = 0; i < 80; i++) {
    if (!isButtonPressed(BTN_SELECT)) {
      break;
    }
    delay(10);
  }
}

} // namespace

namespace GpsSatelliteScanner {

void session() {
  GpsWardriver::stopBackgroundIfRunning();
  lineLen = 0;
  rowCount = 0;
  lastGsvMs = 0;
  viewMode = ViewMode::Combined;
  listScroll = 0;
  lastViewBtnMs = 0;
  memset(rows, 0, sizeof(rows));

  resetSatScannerNavInputState();

  strncpy(utcStr, "--:--:--", sizeof(utcStr));
  strncpy(dateStr, "--/--/--", sizeof(dateStr));
  hdopLive = pdopLive = -1.f;
  fixQuality = 0;
  gsaFixMode = 1;
  satsUsedGga = 0;
  navLat = navLon = NAN;
  navAltM = NAN;
  rmcNavValid = false;

  gpsSerial.end();
  gpsSerial.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);

  const int panelH = tft.height() - kGfxTop;
  (void)ensureScanPanelSprite(tft.width(), panelH);

  uint32_t lastDraw = 0;
  uint32_t lastStatusForced = 0;
  bool firstPanelPaint = true;

  while (!shouldExit()) {
    feedSerial();

    uint32_t now = millis();
    const bool forced = pollViewNavigation();
    if (forced || (now - lastDraw >= kRedrawMs)) {
      lastDraw = now;

      const bool statusFull =
          firstPanelPaint ||
          (((uint32_t)(now - lastStatusForced)) >= 2200u);
      if (statusFull) {
        lastStatusForced = now;
      }
      redrawFeaturePanel(statusFull);
      firstPanelPaint = false;
    }

    delay(2);
  }

  // Do not delete gScanPanel here — releasing ~140 KiB fragments heap and often
  // prevents createSprite() from succeeding when re-entering this screen.

  gpsSerial.end();
  drainButtons();
}

} // namespace GpsSatelliteScanner

namespace GpsWardriver {

#ifndef WARDRIVE_SCAN_INTERVAL_MS
#define WARDRIVE_SCAN_INTERVAL_MS 4200
#endif
#ifndef WARDRIVE_UI_INTERVAL_MS
#define WARDRIVE_UI_INTERVAL_MS 120
#endif
#ifndef WARDRIVE_MAX_APS_PER_SCAN
#define WARDRIVE_MAX_APS_PER_SCAN 48
#endif

/** Same top strip style as the other features: 20px status bar + 16px DARK_GRAY icon row. */
static constexpr int kWardIconRowY = 20;
static constexpr int kWardIconRowH = 16;
static constexpr int kWardIconSz = 16;
static constexpr int kWardBodyY = kWardIconRowY + kWardIconRowH + 4;

/** Settings body layout (must match `wardLayoutSettingsButtons` + draw). */
static constexpr int kWSetBtnH = 26;
static constexpr int kWSetPad = 8;
static constexpr int kWSetGap = 6;
/** Thin header (icon + Setup); extra vertical gaps between each block (no guide strip). */
static constexpr int kWSetHeaderY = kWardBodyY + 3;
static constexpr int kWSetTopRowH = 22;
static constexpr int kWSetGapAfterHeader = 12;
static constexpr int kWSetGapModeToRadio = 16;
static constexpr int kWSetGapRadioToInterval = 16;
/** Space from interval label line to +/- row (28px card from lblIntY-4; +36 => 12px under card). */
static constexpr int kWSetIntervalBtnDown = 36;
static constexpr int kWSetGapIntBtnsToMaxCard = 16;
/** Space from ROW CAP label line to Rows +/- (28px card; +36 => 12px under card). */
static constexpr int kWSetMaxBtnDown = 36;
static constexpr int kWSetGapMaxBtnsToWigle = 18;

static constexpr int kWSetLblSrcY = kWSetHeaderY + kWSetTopRowH + kWSetGapAfterHeader;
static constexpr int kWSetYRadio = kWSetLblSrcY + kWSetGapModeToRadio;
static constexpr int kWSetLblIntY = kWSetYRadio + kWSetBtnH + kWSetGapRadioToInterval;
static constexpr int kWSetYIntBtns = kWSetLblIntY + kWSetIntervalBtnDown;
static constexpr int kWSetLblMaxY = kWSetYIntBtns + kWSetBtnH + kWSetGapIntBtnsToMaxCard;
static constexpr int kWSetYMaxBtns = kWSetLblMaxY + kWSetMaxBtnDown;
static constexpr int kWSetWigleRowY = kWSetYMaxBtns + kWSetBtnH + kWSetGapMaxBtnsToWigle;

static constexpr int kWardSetBtnCount = 9;
static FeatureUI::Button s_wardSetBtns[kWardSetBtnCount];

static volatile uint32_t s_cfgScanMs = WARDRIVE_SCAN_INTERVAL_MS;
static volatile int s_cfgMaxAps = WARDRIVE_MAX_APS_PER_SCAN;

/** WiFi-only, BLE-only, or both each scan interval (stored as uint8_t for volatile). */
enum class WardRadioMode : uint8_t { WiFi = 0, Ble = 1, Both = 2 };
static volatile uint8_t s_cfgRadioMode = (uint8_t)WardRadioMode::WiFi;

static TaskHandle_t s_bgTask = nullptr;
/** True only inside `GpsWardriver::session` main loop (not background handoff return). */
static volatile bool s_wardFgSessionForStatusBar = false;
static volatile bool s_bgStop = false;
static volatile bool s_bgLog = true;
static volatile uint32_t s_bgLines = 0;
static volatile uint32_t s_bgScans = 0;
static char s_bgPath[48] = "";

static TaskHandle_t s_fgScanTask = nullptr;
static volatile bool s_fgScanStop = false;
static volatile bool s_fgScanPaused = false;
static volatile uint32_t s_fgLines = 0;
static volatile uint32_t s_fgScans = 0;

enum class WardPage : uint8_t { Main = 0, Settings, WigleCfg };
static uint32_t s_editScanMs = WARDRIVE_SCAN_INTERVAL_MS;
static int s_editMaxAps = WARDRIVE_MAX_APS_PER_SCAN;
static WardRadioMode s_editRadioMode = WardRadioMode::WiFi;
static uint32_t s_lastTbMs = 0;
static uint32_t s_lastWardInputMs = 0;

/** Avoid full-screen clears every UI tick (reduces SPI TFT flicker). */
struct WardTbSnap {
  bool valid;
  WardPage page;
  bool bgOn;
  bool logOn;
};
static WardTbSnap s_wardTbSnap = {false, WardPage::Main, false, false};

struct WardMainSnap {
  bool valid;
  uint32_t lines;
  uint32_t scans;
  char path[48];
  bool logOn;
  bool bgMode;
  uint8_t radio;
  uint32_t scanMs;
  int maxAps;
  int fixQ;
  unsigned sats;
  int hdopCenti;
  int32_t latE5;
  int32_t lonE5;
  int16_t altM;
};
static WardMainSnap s_wardMainSnap = {};

struct WardSetSnap {
  bool valid;
  uint32_t scanMs;
  int maxAps;
  WardRadioMode radio;
};
static WardSetSnap s_wardSetSnap = {false, 0, 0, WardRadioMode::WiFi};

/** Bottom panel on main ward screen: rolling lines (newest at bottom). */
static constexpr int kWardLogLines = 8;
static constexpr int kWardLogCols = 34;
static char s_wardLog[kWardLogLines][kWardLogCols + 1];
static bool s_wardLogDirty = false;
static portMUX_TYPE s_wardLogMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_wardLogGen = 0;
/** GPS fix edge logging in ward session (-1 = re-arm after clear). */
static int8_t s_wardGpsFixLog = -1;

static void wardLogClear() {
  taskENTER_CRITICAL(&s_wardLogMux);
  for (int i = 0; i < kWardLogLines; ++i) {
    s_wardLog[i][0] = '\0';
  }
  s_wardLogDirty = true;
  s_wardLogGen++;
  s_wardGpsFixLog = -1;
  taskEXIT_CRITICAL(&s_wardLogMux);
}

static void wardLogPush(const char* line) {
  const char* s = line ? line : "";
  char one[kWardLogCols + 1];
  snprintf(one, sizeof(one), "%s", s);
  one[kWardLogCols] = '\0';
  taskENTER_CRITICAL(&s_wardLogMux);
  for (int i = 0; i < kWardLogLines - 1; ++i) {
    memcpy(s_wardLog[i], s_wardLog[i + 1], sizeof(s_wardLog[0]));
  }
  memcpy(s_wardLog[kWardLogLines - 1], one, sizeof(s_wardLog[0]));
  s_wardLogDirty = true;
  s_wardLogGen++;
  taskEXIT_CRITICAL(&s_wardLogMux);
}

static void wardLogPushf(const char* fmt, ...) {
  char b[kWardLogCols + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  b[kWardLogCols] = '\0';
  wardLogPush(b);
}

/** Append one line to the ward log and show the usual modal (for errors / WiGLE). */
static void wardNotify(const char* title, const char* msg) {
  char b[kWardLogCols + 1];
  snprintf(b, sizeof(b), "%s: %s", title ? title : "", msg ? msg : "");
  b[kWardLogCols] = '\0';
  wardLogPush(b);
  showNotification(title, msg);
}

static void wardUiInvalidateAll() {
  s_wardTbSnap.valid = false;
  s_wardMainSnap.valid = false;
  s_wardSetSnap.valid = false;
}

static bool wardTbDirty(WardPage page, bool bgOn, bool logOn) {
  if (!s_wardTbSnap.valid) {
    return true;
  }
  return page != s_wardTbSnap.page || bgOn != s_wardTbSnap.bgOn || logOn != s_wardTbSnap.logOn;
}

static void wardTbSave(WardPage page, bool bgOn, bool logOn) {
  s_wardTbSnap = {true, page, bgOn, logOn};
}

static bool wardMainDirty(uint32_t dispL, uint32_t dispS, const char* dispP, bool logOn, bool bgMode) {
  if (!s_wardMainSnap.valid) {
    return true;
  }
  if (dispL != s_wardMainSnap.lines || dispS != s_wardMainSnap.scans) {
    return true;
  }
  if (logOn != s_wardMainSnap.logOn || bgMode != s_wardMainSnap.bgMode) {
    return true;
  }
  const char* p = dispP ? dispP : "";
  if (strncmp(p, s_wardMainSnap.path, sizeof(s_wardMainSnap.path)) != 0) {
    return true;
  }
  if ((uint8_t)s_cfgRadioMode != s_wardMainSnap.radio || s_cfgScanMs != s_wardMainSnap.scanMs ||
      s_cfgMaxAps != s_wardMainSnap.maxAps) {
    return true;
  }
  if (fixQuality != s_wardMainSnap.fixQ || satsUsedGga != s_wardMainSnap.sats) {
    return true;
  }
  int hc = -1;
  if (hdopLive >= 0.f) {
    hc = (int)lroundf(hdopLive * 100.f);
  }
  if (hc != s_wardMainSnap.hdopCenti) {
    return true;
  }
  int32_t le = INT32_MIN;
  int32_t ln = INT32_MIN;
  if (!isnan(navLat)) {
    le = (int32_t)lroundf((float)navLat * 1e5f);
  }
  if (!isnan(navLon)) {
    ln = (int32_t)lroundf((float)navLon * 1e5f);
  }
  if (le != s_wardMainSnap.latE5 || ln != s_wardMainSnap.lonE5) {
    return true;
  }
  int16_t am = INT16_MIN;
  if (!isnan(navAltM)) {
    am = (int16_t)lroundf((float)navAltM);
  }
  if (s_wardLogDirty) {
    return true;
  }
  return am != s_wardMainSnap.altM;
}

static void wardMainSave(uint32_t dispL, uint32_t dispS, const char* dispP, bool logOn, bool bgMode) {
  s_wardMainSnap.valid = true;
  s_wardMainSnap.lines = dispL;
  s_wardMainSnap.scans = dispS;
  strncpy(s_wardMainSnap.path, dispP && dispP[0] ? dispP : "", sizeof(s_wardMainSnap.path));
  s_wardMainSnap.path[sizeof(s_wardMainSnap.path) - 1] = '\0';
  s_wardMainSnap.logOn = logOn;
  s_wardMainSnap.bgMode = bgMode;
  s_wardMainSnap.radio = (uint8_t)s_cfgRadioMode;
  s_wardMainSnap.scanMs = s_cfgScanMs;
  s_wardMainSnap.maxAps = s_cfgMaxAps;
  s_wardMainSnap.fixQ = fixQuality;
  s_wardMainSnap.sats = satsUsedGga;
  if (hdopLive >= 0.f) {
    s_wardMainSnap.hdopCenti = (int)lroundf(hdopLive * 100.f);
  } else {
    s_wardMainSnap.hdopCenti = -1;
  }
  if (!isnan(navLat)) {
    s_wardMainSnap.latE5 = (int32_t)lroundf((float)navLat * 1e5f);
  } else {
    s_wardMainSnap.latE5 = INT32_MIN;
  }
  if (!isnan(navLon)) {
    s_wardMainSnap.lonE5 = (int32_t)lroundf((float)navLon * 1e5f);
  } else {
    s_wardMainSnap.lonE5 = INT32_MIN;
  }
  if (!isnan(navAltM)) {
    s_wardMainSnap.altM = (int16_t)lroundf((float)navAltM);
  } else {
    s_wardMainSnap.altM = INT16_MIN;
  }
}

static bool wardSetDirty() {
  if (!s_wardSetSnap.valid) {
    return true;
  }
  return s_editScanMs != s_wardSetSnap.scanMs || s_editMaxAps != s_wardSetSnap.maxAps ||
         s_editRadioMode != s_wardSetSnap.radio;
}

static void wardSetSave() {
  s_wardSetSnap.valid = true;
  s_wardSetSnap.scanMs = s_editScanMs;
  s_wardSetSnap.maxAps = s_editMaxAps;
  s_wardSetSnap.radio = s_editRadioMode;
}

static const char* wardAuthLabel(wifi_auth_mode_t t) {
  switch (t) {
    case WIFI_AUTH_OPEN:
      return "OPEN";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2_ENT";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2_WPA3_PSK";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI_PSK";
    default:
      return "OTHER";
  }
}

static const char* wardRadioModeLabel(WardRadioMode m) {
  switch (m) {
    case WardRadioMode::WiFi:
      return "WiFi only";
    case WardRadioMode::Ble:
      return "BLE only";
    case WardRadioMode::Both:
      return "WiFi+BLE";
    default:
      return "?";
  }
}

/** Short label for tight UI rows (avoids clipping on 240px). */
static const char* wardRadioModeShort(WardRadioMode m) {
  switch (m) {
    case WardRadioMode::WiFi:
      return "WiFi";
    case WardRadioMode::Ble:
      return "BLE";
    case WardRadioMode::Both:
      return "W+B";
    default:
      return "?";
  }
}

static void wardAppendCsvString(File& f, const char* s) {
  f.print('"');
  if (s) {
    for (const char* p = s; *p; ++p) {
      if (*p == '"') {
        f.print("\"\"");
      } else if ((uint8_t)*p < 32) {
        f.print(' ');
      } else {
        f.print(*p);
      }
    }
  }
  f.print('"');
}

static void wardDrawPill(int x, int y, int w, int h, const char* text, uint16_t fill, uint16_t txt) {
  tft.fillRoundRect(x, y, w, h, h / 2, fill);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(txt, fill);
  tft.drawString(text ? text : "", x + w / 2, y + h / 2);
  tft.setTextDatum(TL_DATUM);
}

/** Main: back left; gear, play, RSS flush-right. Settings/WiGLE: back left, save right (no −/+ strip). */
static void wardToolbarIconXs(WardPage page, int16_t xs[4]) {
  const int W = tft.width();
  const int sz = (int)kWardIconSz;
  const int margin = 6;
  const int gap = 10;

  if (page == WardPage::Settings || page == WardPage::WigleCfg) {
    xs[0] = (int16_t)margin;
    xs[3] = (int16_t)(W - margin - sz);
    xs[1] = -1;
    xs[2] = -1;
    return;
  }

  xs[3] = (int16_t)margin;
  xs[2] = (int16_t)(W - margin - sz);
  xs[1] = (int16_t)(xs[2] - gap - sz);
  xs[0] = (int16_t)(xs[1] - gap - sz);
  const int backRight = (int)xs[3] + sz;
  if ((int)xs[0] < backRight + gap) {
    const int clusterW = 3 * sz + 2 * gap;
    int c0 = W - margin - clusterW;
    const int minLeft = backRight + gap;
    if (c0 < minLeft) {
      c0 = minLeft;
    }
    xs[0] = (int16_t)c0;
    xs[1] = (int16_t)(c0 + gap + sz);
    xs[2] = (int16_t)(c0 + 2 * (gap + sz));
  }
}

static void wardDrawIconToolbar(WardPage page, bool bgActive, bool logOn) {
  const int scrW = tft.width();
  int16_t ix[4];
  wardToolbarIconXs(page, ix);
  tft.drawLine(0, 19, scrW, 19, TFT_WHITE);
  tft.fillRect(0, kWardIconRowY, scrW, kWardIconRowH, DARK_GRAY);
  const int iy = kWardIconRowY;
  if (page == WardPage::Settings || page == WardPage::WigleCfg) {
    tft.drawBitmap(ix[0], iy, bitmap_icon_go_back, kWardIconSz, kWardIconSz, TFT_WHITE);
    tft.drawBitmap(ix[3], iy, bitmap_icon_checks, kWardIconSz, kWardIconSz, TFT_WHITE);
    tft.drawLine(0, kWardIconRowY + kWardIconRowH, scrW, kWardIconRowY + kWardIconRowH, ORANGE);
    return;
  }
  const unsigned char* i0 = bitmap_icon_document_gear;
  const unsigned char* i1 = logOn ? bitmap_icon_sign_forbidden : bitmap_icon_start;
  const unsigned char* i2 = bgActive ? bitmap_icon_power : bitmap_icon_rss;
  const unsigned char* i3 = bitmap_icon_go_back;
  const unsigned char* const icons[4] = {i0, i1, i2, i3};
  for (int i = 0; i < 4; i++) {
    uint16_t col = TFT_WHITE;
    if (i == 1 && logOn) {
      col = RED;
    } else if (i == 2 && bgActive) {
      col = ORANGE;
    }
    tft.drawBitmap(ix[i], iy, icons[i], kWardIconSz, kWardIconSz, col);
  }
  tft.drawLine(0, kWardIconRowY + kWardIconRowH, scrW, kWardIconRowY + kWardIconRowH, ORANGE);
}

static void wardDrawBody(uint32_t linesWr, uint32_t scans, const char* path, bool logOn,
                         bool bgMode) {
  const int scrW = tft.width();
  const int scrH = tft.height();
  const int pad = 8;
  const int cardW = scrW - 2 * pad;
  const int heroY = kWardBodyY + 4;
  const int statY = heroY + 32;
  const int csvY = statY + 40;
  const int gpsY = csvY + 34;
  const int radioY = gpsY + 60;
  const int hintY = radioY + 40;
  const int hintH = scrH - hintY - 4;
  char buf[128];

  const bool fullPaint = !s_wardMainSnap.valid;
  const char* safePath = path ? path : "";
  const bool heroDirty = fullPaint || logOn != s_wardMainSnap.logOn || bgMode != s_wardMainSnap.bgMode;
  const bool statsDirty = fullPaint || linesWr != s_wardMainSnap.lines || scans != s_wardMainSnap.scans;
  const bool csvDirty = fullPaint || strncmp(safePath, s_wardMainSnap.path, sizeof(s_wardMainSnap.path)) != 0;
  const WardRadioMode rm = (WardRadioMode)s_cfgRadioMode;
  const bool radioDirty = fullPaint || (uint8_t)rm != s_wardMainSnap.radio ||
                          s_cfgScanMs != s_wardMainSnap.scanMs || s_cfgMaxAps != s_wardMainSnap.maxAps;
  const bool fix = (fixQuality > 0) && !isnan(navLat) && !isnan(navLon);
  int hcNow = -1;
  if (hdopLive >= 0.f) {
    hcNow = (int)lroundf(hdopLive * 100.f);
  }
  int32_t latNow = INT32_MIN;
  int32_t lonNow = INT32_MIN;
  if (!isnan(navLat)) {
    latNow = (int32_t)lroundf((float)navLat * 1e5f);
  }
  if (!isnan(navLon)) {
    lonNow = (int32_t)lroundf((float)navLon * 1e5f);
  }
  int16_t altNow = INT16_MIN;
  if (!isnan(navAltM)) {
    altNow = (int16_t)lroundf((float)navAltM);
  }
  const bool gpsDirty = fullPaint || fixQuality != s_wardMainSnap.fixQ ||
                        satsUsedGga != s_wardMainSnap.sats || hcNow != s_wardMainSnap.hdopCenti ||
                        latNow != s_wardMainSnap.latE5 || lonNow != s_wardMainSnap.lonE5 ||
                        altNow != s_wardMainSnap.altM;
  if (fullPaint) {
    tft.fillRect(0, kWardBodyY, scrW, scrH - kWardBodyY, FEATURE_BG);
  }
  tft.setTextDatum(TL_DATUM);

  if (heroDirty) {
  tft.fillRoundRect(pad, heroY, cardW, 28, 8, UI_FG);
  tft.drawRoundRect(pad, heroY, cardW, 28, 8, UI_LINE);
  tft.fillRoundRect(pad + 3, heroY + 6, 3, 16, 2, ORANGE);
  tft.drawBitmap(pad + 10, heroY + 6, bitmap_icon_satellite, 16, 16, UI_ICON);
  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString("Wardrive", pad + 28, heroY + 6);
  /** Same left X as GPS WAIT pill (40px wide): `scrW - pad - 44`. */
  constexpr int kWardStatusPillW = 40;
  const int statusPillX = scrW - pad - 44;
  if (logOn) {
    wardDrawPill(statusPillX, heroY + 5, kWardStatusPillW, 18, "REC", RED, WHITE);
  } else {
    wardDrawPill(statusPillX, heroY + 5, kWardStatusPillW, 18, "OFF", DARK_GRAY, UI_DIM_TEXT);
  }
  if (bgMode) {
    tft.setTextFont(1);
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    tft.drawString("BG", statusPillX - 22, heroY + 9);
  }
  }

  if (statsDirty) {
  const int colGap = 6;
  const int colW = (cardW - colGap) / 2;
  tft.fillRoundRect(pad, statY, colW, 34, 8, UI_FG);
  tft.drawRoundRect(pad, statY, colW, 34, 8, UI_LINE);
  tft.setTextFont(1);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("ROWS", pad + 6, statY + 5);
  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, UI_FG);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)linesWr);
  tft.drawString(buf, pad + 6, statY + 16);

  const int x2 = pad + colW + colGap;
  tft.fillRoundRect(x2, statY, colW, 34, 8, UI_FG);
  tft.drawRoundRect(x2, statY, colW, 34, 8, UI_LINE);
  tft.setTextFont(1);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("SCANS", x2 + 6, statY + 5);
  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, UI_FG);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)scans);
  tft.drawString(buf, x2 + 6, statY + 16);
  }

  if (csvDirty) {
  tft.fillRoundRect(pad, csvY, cardW, 28, 8, UI_FG);
  tft.drawRoundRect(pad, csvY, cardW, 28, 8, UI_LINE);
  tft.drawBitmap(pad + 6, csvY + 6, bitmap_icon_document_tag, 16, 16, UI_ICON);
  tft.setTextFont(1);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("CSV", pad + 26, csvY + 4);
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  if (!safePath[0]) {
    tft.drawString("no file", pad + 26, csvY + 16);
  } else {
    const size_t L = strlen(safePath);
    if (L > 18u) {
      snprintf(buf, sizeof(buf), "%.8s..%.8s", safePath, safePath + L - 8);
    } else {
      snprintf(buf, sizeof(buf), "%s", safePath);
    }
    tft.drawString(buf, pad + 26, csvY + 16);
  }
  }

  if (gpsDirty) {
  tft.fillRoundRect(pad, gpsY, cardW, 54, 8, UI_FG);
  tft.drawRoundRect(pad, gpsY, cardW, 54, 8, UI_LINE);
  tft.drawBitmap(pad + 6, gpsY + 6, bitmap_icon_satellite_dish, 16, 16, UI_ICON);
  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString("GPS", pad + 26, gpsY + 8);
  /** Right edges align at `scrW - pad - 4` (34px FIX vs 40px WAIT/REC). */
  if (fix) {
    wardDrawPill(scrW - pad - 38, gpsY + 4, 34, 16, "FIX", GREEN, BLACK);
  } else {
    wardDrawPill(scrW - pad - 44, gpsY + 4, 40, 16, "WAIT", ORANGE, FEATURE_BG);
  }
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  snprintf(buf, sizeof(buf), "S%u H%.1f", (unsigned)satsUsedGga, hdopLive >= 0.f ? (double)hdopLive : -1.0);
  tft.drawString(buf, pad + 8, gpsY + 24);
  if (fix) {
    snprintf(buf, sizeof(buf), "%.4f %.4f", navLat, navLon);
    tft.drawString(buf, pad + 8, gpsY + 36);
    if (!isnan(navAltM)) {
      snprintf(buf, sizeof(buf), "Alt %.0fm", (double)navAltM);
      tft.drawString(buf, pad + 132, gpsY + 24);
    }
  } else {
    tft.drawString("No fix yet", pad + 8, gpsY + 36);
  }
  }

  if (radioDirty) {
  tft.fillRoundRect(pad, radioY, cardW, 34, 8, UI_FG);
  tft.drawRoundRect(pad, radioY, cardW, 34, 8, UI_LINE);
  const int radioIconX = pad + 10;
  const int radioTextX = pad + 62;
  int ix = radioIconX;
  if (rm == WardRadioMode::WiFi || rm == WardRadioMode::Both) {
    tft.drawBitmap(ix, radioY + 9, bitmap_icon_wifi, 16, 16, rm == WardRadioMode::WiFi ? ORANGE : UI_DIM_TEXT);
    ix += 22;
  }
  if (rm == WardRadioMode::Ble || rm == WardRadioMode::Both) {
    tft.drawBitmap(ix, radioY + 9, bitmap_icon_ble, 16, 16, rm == WardRadioMode::Ble ? ORANGE : UI_DIM_TEXT);
    ix += 22;
  }
  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString(wardRadioModeShort(rm), radioTextX, radioY + 8);
  snprintf(buf, sizeof(buf), "%ums  cap %d", (unsigned)s_cfgScanMs, (int)s_cfgMaxAps);
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  tft.drawString(buf, radioTextX, radioY + 21);
  }

  const bool logPanelDirty = fullPaint || s_wardLogDirty;
  if (logPanelDirty && hintH >= 20) {
    char snap[kWardLogLines][kWardLogCols + 1];
    uint32_t genSnap = 0;
    taskENTER_CRITICAL(&s_wardLogMux);
    memcpy(snap, s_wardLog, sizeof(snap));
    genSnap = s_wardLogGen;
    taskEXIT_CRITICAL(&s_wardLogMux);

    tft.fillRoundRect(pad, hintY, cardW, hintH, 8, UI_FG);
    tft.drawRoundRect(pad, hintY, cardW, hintH, 8, UI_LINE);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    const int lx = pad + 4;
    const int lineH = 10;
    int ty = hintY + 3;
    for (int i = 0; i < kWardLogLines && ty + lineH <= hintY + hintH - 2; ++i, ty += lineH) {
      tft.drawString(snap[i], lx, ty);
    }
    taskENTER_CRITICAL(&s_wardLogMux);
    if (s_wardLogGen == genSnap) {
      s_wardLogDirty = false;
    }
    taskEXIT_CRITICAL(&s_wardLogMux);
  }
}

static void wardLayoutSettingsButtons() {
  using BS = FeatureUI::ButtonStyle;
  const int16_t W = tft.width();
  const int16_t bw3 = (int16_t)((W - 2 * kWSetPad - 2 * kWSetGap) / 3);
  const int16_t bw2 = (int16_t)((W - 2 * kWSetPad - kWSetGap) / 2);
  const int16_t yR = (int16_t)kWSetYRadio;
  s_wardSetBtns[0] = {(int16_t)kWSetPad,
                      yR,
                      bw3,
                      (int16_t)kWSetBtnH,
                      "WiFi",
                      (s_editRadioMode == WardRadioMode::WiFi) ? BS::Primary : BS::Secondary,
                      false};
  s_wardSetBtns[1] = {(int16_t)(kWSetPad + bw3 + kWSetGap),
                      yR,
                      bw3,
                      (int16_t)kWSetBtnH,
                      "BLE",
                      (s_editRadioMode == WardRadioMode::Ble) ? BS::Primary : BS::Secondary,
                      false};
  s_wardSetBtns[2] = {(int16_t)(kWSetPad + 2 * (bw3 + kWSetGap)),
                      yR,
                      bw3,
                      (int16_t)kWSetBtnH,
                      "Both",
                      (s_editRadioMode == WardRadioMode::Both) ? BS::Primary : BS::Secondary,
                      false};

  const int16_t yI = (int16_t)kWSetYIntBtns;
  s_wardSetBtns[3] = {(int16_t)kWSetPad, yI, bw2, (int16_t)kWSetBtnH, "-500ms", BS::Secondary, false};
  s_wardSetBtns[4] = {(int16_t)(kWSetPad + bw2 + kWSetGap),
                      yI,
                      bw2,
                      (int16_t)kWSetBtnH,
                      "+500ms",
                      BS::Secondary,
                      false};

  const int16_t yM = (int16_t)kWSetYMaxBtns;
  s_wardSetBtns[5] = {(int16_t)kWSetPad, yM, bw2, (int16_t)kWSetBtnH, "Rows -", BS::Secondary, false};
  s_wardSetBtns[6] = {(int16_t)(kWSetPad + bw2 + kWSetGap),
                      yM,
                      bw2,
                      (int16_t)kWSetBtnH,
                      "Rows +",
                      BS::Secondary,
                      false};

  const int16_t yW = (int16_t)kWSetWigleRowY;
  const int16_t halfW = (int16_t)((W - 2 * kWSetPad - kWSetGap) / 2);
  s_wardSetBtns[7] = {(int16_t)kWSetPad,
                      yW,
                      halfW,
                      (int16_t)kWSetBtnH,
                      "Upload",
                      BS::Primary,
                      false};
  s_wardSetBtns[8] = {(int16_t)(kWSetPad + halfW + kWSetGap),
                      yW,
                      halfW,
                      (int16_t)kWSetBtnH,
                      "WiGLE cfg",
                      BS::Secondary,
                      false};
}

static void wardDrawSettingsPage() {
  const int scrW = tft.width();
  const int scrH = tft.height();
  const int pad = 8;
  const int cardW = scrW - 2 * pad;
  char buf[96];
  const bool fullPaint = !s_wardSetSnap.valid;
  const bool radioDirty = fullPaint || s_editRadioMode != s_wardSetSnap.radio;
  const bool intervalDirty = fullPaint || s_editScanMs != s_wardSetSnap.scanMs;
  const bool maxDirty = fullPaint || s_editMaxAps != s_wardSetSnap.maxAps;

  if (fullPaint) {
    tft.fillRect(0, kWardBodyY, scrW, scrH - kWardBodyY, FEATURE_BG);
  }
  tft.setTextDatum(TL_DATUM);

  wardLayoutSettingsButtons();

  if (fullPaint) {
    const int topY = kWSetHeaderY;
    tft.fillRoundRect(pad, topY, cardW, kWSetTopRowH, 5, UI_FG);
    tft.drawRoundRect(pad, topY, cardW, kWSetTopRowH, 5, UI_LINE);
    tft.fillRoundRect(pad + 3, topY + 4, 2, 14, 1, ORANGE);
    tft.drawBitmap(pad + 6, topY + 3, bitmap_icon_document_gear, 16, 16, UI_ICON);
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, UI_FG);
    tft.drawString("Setup", pad + 24, topY + 6);

    tft.setTextColor(UI_ICON, FEATURE_BG);
    tft.drawString("MODE", pad, kWSetLblSrcY);
    tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    tft.drawString("tap row", pad + 52, kWSetLblSrcY);
  }

  if (intervalDirty) {
  tft.fillRoundRect(pad, kWSetLblIntY - 4, cardW, 28, 6, UI_FG);
  tft.drawRoundRect(pad, kWSetLblIntY - 4, cardW, 28, 6, UI_LINE);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("INTERVAL", pad + 6, kWSetLblIntY);
  snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)s_editScanMs);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString(buf, pad + 86, kWSetLblIntY);
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  tft.drawString("or +/- row below", pad + 6, kWSetLblIntY + 10);
  FeatureUI::drawButton(s_wardSetBtns[3]);
  FeatureUI::drawButton(s_wardSetBtns[4]);
  }

  if (maxDirty) {
  tft.fillRoundRect(pad, kWSetLblMaxY - 4, cardW, 28, 6, UI_FG);
  tft.drawRoundRect(pad, kWSetLblMaxY - 4, cardW, 28, 6, UI_LINE);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("ROW CAP", pad + 6, kWSetLblMaxY);
  snprintf(buf, sizeof(buf), "%d / scan", s_editMaxAps);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString(buf, pad + 86, kWSetLblMaxY);
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  tft.drawString("WiFi APs + BLE dev", pad + 6, kWSetLblMaxY + 10);
  FeatureUI::drawButton(s_wardSetBtns[5]);
  FeatureUI::drawButton(s_wardSetBtns[6]);
  }

  if (radioDirty) {
  for (int i = 0; i < 3; ++i) {
    FeatureUI::drawButton(s_wardSetBtns[i]);
  }
  }

  FeatureUI::drawButton(s_wardSetBtns[7]);
  FeatureUI::drawButton(s_wardSetBtns[8]);
}

#ifndef WARD_WIGLE_CFG_PATH
#define WARD_WIGLE_CFG_PATH "/config/wigle.txt"
#endif
#ifndef WARD_WIGLE_TMP_CSV
#define WARD_WIGLE_TMP_CSV "/wd_wigle_upload.csv"
#endif

static void wardTrimEnd(char* s) {
  if (!s) {
    return;
  }
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

// ---- WiGLE on-device editor (uses KeyboardUI; writes WARD_WIGLE_CFG_PATH) ----
static const char* const kWardWigleKbRows[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl_",
    "zxcvbnm<-",
};

static char s_wigUser[96];
static char s_wigTok[160];
static char s_wigSsid[80];
static char s_wigPass[80];
static bool s_wigUseSavedWifi = false;
static bool s_wigleCfgDirty = true;

static void wardWigleSanitizeOneLine(char* s) {
  if (!s) {
    return;
  }
  for (char* p = s; *p; ++p) {
    if (*p == '\r' || *p == '\n') {
      *p = ' ';
    }
  }
}

static void wardWigleLoadEditBuffers() {
  memset(s_wigUser, 0, sizeof(s_wigUser));
  memset(s_wigTok, 0, sizeof(s_wigTok));
  memset(s_wigSsid, 0, sizeof(s_wigSsid));
  memset(s_wigPass, 0, sizeof(s_wigPass));
  s_wigUseSavedWifi = false;
  File f = SD.open(WARD_WIGLE_CFG_PATH, FILE_READ);
  if (!f) {
    s_wigleCfgDirty = true;
    return;
  }
  auto rdLine = [&](char* buf, size_t cap) {
    if (!f.available() || cap < 2u) {
      buf[0] = '\0';
      return;
    }
    size_t n = f.readBytesUntil('\n', buf, cap - 1);
    buf[n] = '\0';
    wardTrimEnd(buf);
  };
  auto rdDataLine = [&](char* buf, size_t cap) {
    buf[0] = '\0';
    while (f.available()) {
      rdLine(buf, cap);
      const char* p = buf;
      while (*p == ' ' || *p == '\t') {
        ++p;
      }
      if (*p == '\0') {
        continue;
      }
      if (*p == '#') {
        continue;
      }
      break;
    }
  };
  rdDataLine(s_wigUser, sizeof(s_wigUser));
  rdDataLine(s_wigTok, sizeof(s_wigTok));
  rdDataLine(s_wigSsid, sizeof(s_wigSsid));
  rdDataLine(s_wigPass, sizeof(s_wigPass));
  f.close();
  bool autoWifi = !s_wigSsid[0] || strcmp(s_wigSsid, "-") == 0;
  if (!autoWifi) {
    const char* p = s_wigSsid;
    const char* q = "auto";
    for (; *p && *q; ++p, ++q) {
      if (tolower((unsigned char)*p) != tolower((unsigned char)*q)) {
        break;
      }
    }
    autoWifi = (*p == '\0' && *q == '\0');
  }
  if (autoWifi) {
    s_wigUseSavedWifi = true;
    s_wigSsid[0] = '\0';
    s_wigPass[0] = '\0';
  }
  wardWigleSanitizeOneLine(s_wigUser);
  wardWigleSanitizeOneLine(s_wigTok);
  wardWigleSanitizeOneLine(s_wigSsid);
  wardWigleSanitizeOneLine(s_wigPass);
  s_wigleCfgDirty = true;
}

static bool wardWigleEnsureConfigDir() {
  if (!isSDCardAvailable()) {
    return false;
  }
  if (SD.exists("/config")) {
    return true;
  }
  return SD.mkdir("/config");
}

static bool wardWigleSaveCfgToSd() {
  wardWigleSanitizeOneLine(s_wigUser);
  wardWigleSanitizeOneLine(s_wigTok);
  wardWigleSanitizeOneLine(s_wigSsid);
  wardWigleSanitizeOneLine(s_wigPass);
  if (!s_wigUser[0] || !s_wigTok[0]) {
    wardNotify("WiGLE", "API name and token are required.");
    return false;
  }
  if (!wardWigleEnsureConfigDir()) {
    wardNotify("WiGLE", "Cannot create /config on SD.");
    return false;
  }
  File out = SD.open(WARD_WIGLE_CFG_PATH, FILE_WRITE);
  if (!out) {
    wardNotify("WiGLE", "Cannot write wigle.txt");
    return false;
  }
  out.println(s_wigUser);
  out.println(s_wigTok);
  if (s_wigUseSavedWifi) {
    out.println("AUTO");
    out.println("");
  } else {
    out.println(s_wigSsid);
    out.println(s_wigPass);
  }
  out.close();
  wardNotify("WiGLE", "Saved /config/wigle.txt");
  return true;
}

static void wardWigleRunFieldEditor(const char* title, char* buf, size_t cap, uint8_t maxKbLen, bool requireNonEmpty) {
  s_fgScanPaused = true;
  delay(200);
  OnScreenKeyboardConfig cfg{};
  cfg.titleLine1 = title;
  cfg.titleLine2 = "OK saves, Back cancels";
  cfg.rows = kWardWigleKbRows;
  cfg.rowCount = 4;
  cfg.maxLen = maxKbLen;
  cfg.shuffleNames = nullptr;
  cfg.shuffleCount = 0;
  cfg.buttonsY = 195;
  cfg.backLabel = "Back";
  cfg.middleLabel = "Del";
  cfg.okLabel = "OK";
  cfg.enableShuffle = false;
  cfg.requireNonEmpty = requireNonEmpty;
  cfg.emptyErrorMsg = requireNonEmpty ? "Cannot be empty" : "";
  const String init = String(buf);
  const OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, init);
  s_fgScanPaused = false;
  wardUiInvalidateAll();
  if (r.accepted) {
    strncpy(buf, r.text.c_str(), cap - 1);
    buf[cap - 1] = '\0';
    s_wigleCfgDirty = true;
  }
}

static constexpr int kWigleRowH = 28;
static constexpr int kWigleRowGap = 5;
static constexpr int kWigleFirstRowY = kWardBodyY + 34;

static int wardWigleHitBodyRow(int x, int y) {
  if (x < 8 || x >= tft.width() - 8) {
    return -1;
  }
  const int ry = y - kWigleFirstRowY;
  const int step = kWigleRowH + kWigleRowGap;
  if (ry < 0 || step <= 0) {
    return -1;
  }
  const int idx = ry / step;
  if (idx < 0 || idx > 4) {
    return -1;
  }
  const int within = ry - idx * step;
  if (within > kWigleRowH) {
    return -1;
  }
  return idx;
}

static void wardDrawWigleCfgPage() {
  const int scrW = tft.width();
  const int scrH = tft.height();
  const int pad = 8;
  tft.fillRect(0, kWardBodyY, scrW, scrH - kWardBodyY, FEATURE_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.drawString("WiGLE config", pad, kWardBodyY + 2);
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.drawString("Back  Check = save to SD", pad, kWardBodyY + 18);

  auto rowCard = [&](int idx, const char* label, const char* val, bool dim) {
    const int y = kWigleFirstRowY + idx * (kWigleRowH + kWigleRowGap);
    tft.fillRoundRect(pad, y, scrW - 2 * pad, kWigleRowH, 6, UI_FG);
    tft.drawRoundRect(pad, y, scrW - 2 * pad, kWigleRowH, 6, UI_LINE);
    tft.setTextColor(dim ? UI_DIM_TEXT : UI_ICON, UI_FG);
    tft.drawString(label, pad + 6, y + 4);
    tft.setTextColor(dim ? UI_DIM_TEXT : UI_TEXT, UI_FG);
    char el[80];
    const char* show = (val && val[0]) ? val : "(empty)";
    snprintf(el, sizeof(el), "%.32s", show);
    tft.drawString(el, pad + 6, y + 14);
  };

  rowCard(0, "API name", s_wigUser, false);
  rowCard(1, "API token", s_wigTok, false);
  rowCard(2, "Wi-Fi SSID", s_wigSsid, s_wigUseSavedWifi);
  rowCard(3, "Wi-Fi password", s_wigPass, s_wigUseSavedWifi);
  const int y4 = kWigleFirstRowY + 4 * (kWigleRowH + kWigleRowGap);
  tft.fillRoundRect(pad, y4, scrW - 2 * pad, kWigleRowH, 6, UI_FG);
  tft.drawRoundRect(pad, y4, scrW - 2 * pad, kWigleRowH, 6, UI_LINE);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawString(s_wigUseSavedWifi ? "Auto Wi-Fi ON (tap to edit SSID)" : "Auto Wi-Fi OFF (tap for saved NVS)",
                 pad + 6, y4 + 8);
}

static int wardPollWigleCfgCombinedHit() {
  int x = 0, y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return -1;
  }
  const uint32_t t = millis();
  if ((uint32_t)(t - s_lastWardInputMs) < 120u) {
    return -1;
  }
  s_lastWardInputMs = t;
  if (y > kWardIconRowY && y < kWardIconRowY + kWardIconRowH) {
    int16_t ix[4];
    wardToolbarIconXs(WardPage::WigleCfg, ix);
    if (x > ix[0] && x < ix[0] + kWardIconSz) {
      s_lastTbMs = t;
      return 0;
    }
    if (x > ix[3] && x < ix[3] + kWardIconSz) {
      s_lastTbMs = t;
      return 3;
    }
    return -1;
  }
  const int row = wardWigleHitBodyRow(x, y);
  if (row >= 0) {
    return 10 + row;
  }
  return -1;
}

static int wardWifiChannelToMHz(int ch) {
  if (ch >= 1 && ch <= 13) {
    return 2412 + (ch - 1) * 5;
  }
  if (ch == 14) {
    return 2484;
  }
  if (ch >= 32) {
    return 5000 + ch * 5;
  }
  return 0;
}

static void wardMacToLowerColons(char* mac) {
  if (!mac) {
    return;
  }
  for (char* p = mac; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = (char)(*p - 'A' + 'a');
    }
  }
}

static void wardAuthToWigleCaps(const char* auth, char* out, size_t outSz) {
  if (!out || outSz < 8u) {
    return;
  }
  out[0] = '\0';
  if (!auth || !auth[0]) {
    snprintf(out, outSz, "[ESS]");
    return;
  }
  if (!strcmp(auth, "OPEN")) {
    snprintf(out, outSz, "[ESS]");
  } else if (!strcmp(auth, "WEP")) {
    snprintf(out, outSz, "[WEP][ESS]");
  } else if (!strcmp(auth, "WPA_PSK")) {
    snprintf(out, outSz, "[WPA-PSK-CCMP][ESS]");
  } else if (!strcmp(auth, "WPA2_PSK")) {
    snprintf(out, outSz, "[WPA2-PSK-CCMP][ESS]");
  } else if (!strcmp(auth, "WPA_WPA2_PSK")) {
    snprintf(out, outSz, "[WPA2-PSK-CCMP][WPA-PSK-CCMP][ESS]");
  } else if (!strcmp(auth, "WPA2_ENT")) {
    snprintf(out, outSz, "[WPA2-EAP-CCMP][ESS]");
  } else if (!strcmp(auth, "WPA3_PSK")) {
    snprintf(out, outSz, "[WPA3-SAE-CCMP][ESS]");
  } else if (!strcmp(auth, "WPA2_WPA3_PSK")) {
    snprintf(out, outSz, "[WPA2-PSK-CCMP][WPA3-SAE-CCMP][ESS]");
  } else {
    snprintf(out, outSz, "[%s][ESS]", auth);
  }
}

/** Split ward CSV line into fields (handles quoted SSID/name with commas). Returns field count. */
static int wardSplitWardCsvLine(char* line, char* fields[], int maxFields) {
  bool inq = false;
  char* start = line;
  int nf = 0;
  for (char* p = line; *p && nf < maxFields; ++p) {
    if (*p == '"') {
      if (inq && p[1] == '"') {
        ++p;
        continue;
      }
      inq = !inq;
      continue;
    }
    if (*p == ',' && !inq) {
      *p = '\0';
      fields[nf++] = start;
      start = p + 1;
    }
  }
  fields[nf++] = start;
  return nf;
}

/** Strip RFC4180 outer quotes and `""` escapes (field points into split line buffer). */
static void wardStripCsvQuotes(char* s) {
  if (!s || !s[0]) {
    return;
  }
  size_t n = strlen(s);
  if (n >= 2u && s[0] == '"' && s[n - 1u] == '"') {
    memmove(s, s + 1, n - 2u);
    s[n - 2u] = '\0';
    char* w = s;
    for (const char* p = s; *p;) {
      if (*p == '"' && p[1] == '"') {
        *w++ = '"';
        p += 2;
      } else {
        *w++ = *p++;
      }
    }
    *w = '\0';
  }
}

/** `dd/mm/yy` from GPS log -> `yyyy-mm-dd` (UTC assumption). */
static bool wardDdMmYyToIso(const char* ddmmyy, char* iso, size_t isoSz) {
  if (!ddmmyy || !iso || isoSz < 12u) {
    return false;
  }
  int d0 = 0, m0 = 0, y0 = 0;
  if (sscanf(ddmmyy, "%d/%d/%d", &d0, &m0, &y0) != 3) {
    return false;
  }
  int yFull = y0;
  if (yFull < 70) {
    yFull += 2000;
  } else if (yFull < 100) {
    yFull += 1900;
  }
  snprintf(iso, isoSz, "%04d-%02d-%02d", yFull, m0, d0);
  return true;
}

static bool wardConvertWardCsvToWigle(const char* srcPath, File& out) {
  File in = SD.open(srcPath, FILE_READ);
  if (!in) {
    return false;
  }
  out.print("WigleWifi-1.6,appRelease=1.0,model=ESP32-DIV,release=1.0,device=wardrive,display=,board=ESP32,"
             "brand=ESP32-DIV,star=Sol,body=3,subBody=0\r\n");
  out.print("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,"
            "AccuracyMeters,RCOIs,MfgrId,Type\r\n");

  char line[400];
  while (in.available()) {
    size_t n = in.readBytesUntil('\n', line, sizeof(line) - 1u);
    line[n] = '\0';
    wardTrimEnd(line);
    if (!line[0]) {
      continue;
    }
    if (strncmp(line, "epoch_ms", 8) == 0) {
      continue;
    }
    char* fld[20];
    int nf = wardSplitWardCsvLine(line, fld, 20);
    if (nf < 14) {
      continue;
    }
    const char* utc = fld[1];
    const char* date = fld[2];
    const char* lat = fld[3];
    const char* lon = fld[4];
    const char* alt = fld[5];
    const char* hdop = fld[8];
    const char* radio = fld[9];
    const char* bssid = fld[11];
    const char* chs = fld[12];
    const char* rssi = fld[13];
    const char* auth = (nf >= 15) ? fld[14] : "";
    wardStripCsvQuotes(fld[10]);

    char isoDate[16];
    if (!wardDdMmYyToIso(date, isoDate, sizeof(isoDate))) {
      snprintf(isoDate, sizeof(isoDate), "1970-01-01");
    }
    char firstSeen[40];
    snprintf(firstSeen, sizeof(firstSeen), "%s %s", isoDate, utc);

    double acc = 25.0;
    if (hdop && hdop[0]) {
      double h = strtod(hdop, nullptr);
      if (h > 0.0 && h < 200.0) {
        acc = h * 4.0;
      }
    }
    int altM = 0;
    if (alt && alt[0]) {
      altM = (int)lroundf((float)strtod(alt, nullptr));
    }

    const char* latS = (lat && lat[0]) ? lat : "0.0";
    const char* lonS = (lon && lon[0]) ? lon : "0.0";

    if (!strcmp(radio, "WIFI")) {
      char caps[96];
      wardAuthToWigleCaps(auth, caps, sizeof(caps));
      char mac[24];
      snprintf(mac, sizeof(mac), "%s", bssid);
      wardMacToLowerColons(mac);
      int ch = (int)strtol(chs, nullptr, 10);
      int mhz = wardWifiChannelToMHz(ch);
      out.print(mac);
      out.print(',');
      wardAppendCsvString(out, fld[10]);
      out.print(',');
      wardAppendCsvString(out, caps);
      out.printf(",%s,%d,%d,%s,%s,%s,%d,%.2f,,,WIFI\r\n", firstSeen, ch, mhz, rssi, latS, lonS, altM, acc);
    } else if (!strcmp(radio, "BLE")) {
      char mac[24];
      snprintf(mac, sizeof(mac), "%s", bssid);
      wardMacToLowerColons(mac);
      out.print(mac);
      out.print(',');
      wardAppendCsvString(out, fld[10]);
      out.print(',');
      wardAppendCsvString(out, "Misc [LE]");
      out.printf(",%s,0,,%s,%s,%s,%d,%.2f,,,BLE\r\n", firstSeen, rssi, latS, lonS, altM, acc);
    }
  }
  in.close();
  return true;
}

/** If line 3 is empty, `-`, or `auto`, use `WiFi.begin()` (ESP32 NVS credentials from a prior connect). */
static bool wardReadWigleConfig(char* apiUser, size_t userSz, char* apiTok, size_t tokSz, char* staSsid,
                                size_t ssidSz, char* staPass, size_t passSz, bool* useStoredWifi) {
  if (!apiUser || !apiTok || !staSsid || !staPass) {
    return false;
  }
  if (useStoredWifi) {
    *useStoredWifi = false;
  }
  apiUser[0] = apiTok[0] = staSsid[0] = staPass[0] = '\0';
  File f = SD.open(WARD_WIGLE_CFG_PATH, FILE_READ);
  if (!f) {
    return false;
  }
  auto rdLine = [&](char* buf, size_t cap) {
    if (!f.available() || cap < 2u) {
      buf[0] = '\0';
      return;
    }
    size_t n = f.readBytesUntil('\n', buf, cap - 1);
    buf[n] = '\0';
    wardTrimEnd(buf);
  };
  /** Next non-empty line that is not a # comment (for hand-edited templates). */
  auto rdDataLine = [&](char* buf, size_t cap) {
    buf[0] = '\0';
    while (f.available()) {
      rdLine(buf, cap);
      const char* p = buf;
      while (*p == ' ' || *p == '\t') {
        ++p;
      }
      if (*p == '\0') {
        continue;
      }
      if (*p == '#') {
        continue;
      }
      break;
    }
  };
  rdDataLine(apiUser, userSz);
  rdDataLine(apiTok, tokSz);
  rdDataLine(staSsid, ssidSz);
  rdDataLine(staPass, passSz);
  f.close();
  if (!apiUser[0] || !apiTok[0]) {
    return false;
  }
  bool autoWifi = !staSsid[0] || strcmp(staSsid, "-") == 0;
  if (!autoWifi) {
    const char* p = staSsid;
    const char* q = "auto";
    for (; *p && *q; ++p, ++q) {
      if (tolower((unsigned char)*p) != tolower((unsigned char)*q)) {
        break;
      }
    }
    autoWifi = (*p == '\0' && *q == '\0');
  }
  if (autoWifi) {
    if (useStoredWifi) {
      *useStoredWifi = true;
    }
    staSsid[0] = '\0';
    staPass[0] = '\0';
  }
  return true;
}

static bool wardBuildBasicAuth(const char* user, const char* token, char* b64, size_t b64Cap) {
  char plain[320];
  snprintf(plain, sizeof(plain), "%s:%s", user, token);
  size_t plainLen = strlen(plain);
  size_t olen = 0;
  int err = mbedtls_base64_encode((unsigned char*)b64, b64Cap, &olen, (const unsigned char*)plain, plainLen);
  if (err != 0 || olen == 0 || olen >= b64Cap) {
    return false;
  }
  b64[olen] = '\0';
  return true;
}

static bool wardWigleStreamUpload(File& csv, const char* uploadName, const char* b64Auth, char* errBuf, size_t errCap) {
  if (errBuf && errCap) {
    errBuf[0] = '\0';
  }
  WiFiClientSecure cli;
  cli.setInsecure();
  if (!cli.connect("api.wigle.net", 443)) {
    if (errBuf && errCap) {
      snprintf(errBuf, errCap, "TLS connect failed");
    }
    return false;
  }

  const char* boundary = "----esp32divWigle7aF3";
  char partDonate[160];
  snprintf(partDonate, sizeof(partDonate),
           "--%s\r\n"
           "Content-Disposition: form-data; name=\"donate\"\r\n\r\n"
           "false\r\n",
           boundary);

  String head2 = String("--") + boundary + "\r\n" + "Content-Disposition: form-data; name=\"file\"; filename=\"" +
                 String(uploadName) + "\"\r\n" + "Content-Type: text/csv\r\n\r\n";
  String tail = String("\r\n--") + boundary + "--\r\n";

  const size_t part1Len = strlen(partDonate);
  const size_t head2Len = head2.length();
  const size_t tailLen = tail.length();
  const size_t fileLen = csv.size();
  const size_t contentLen = part1Len + head2Len + fileLen + tailLen;

  cli.printf("POST /api/v2/file/upload HTTP/1.1\r\nHost: api.wigle.net\r\n");
  cli.printf("User-Agent: ESP32-DIV-Wardrive/1\r\n");
  cli.printf("Authorization: Basic %s\r\n", b64Auth);
  cli.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
  cli.printf("Content-Length: %u\r\n\r\n", (unsigned)contentLen);

  cli.write((const uint8_t*)partDonate, part1Len);
  cli.print(head2);

  csv.seek(0);
  uint8_t buf[512];
  while (csv.available()) {
    size_t n = csv.read(buf, sizeof(buf));
    if (n) {
      cli.write(buf, n);
    }
    yield();
  }
  cli.print(tail);

  unsigned long t0 = millis();
  int code = -1;
  while (cli.connected() && (millis() - t0) < 45000UL) {
    if (cli.available()) {
      String ln = cli.readStringUntil('\n');
      ln.trim();
      if (ln.length() > 5 && strncmp(ln.c_str(), "HTTP/", 5) == 0) {
        int c0 = -1;
        if (sscanf(ln.c_str(), "HTTP/%*d.%*d %d", &c0) == 1) {
          code = c0;
        }
      }
      if (code > 0 && ln.length() == 0) {
        break;
      }
    } else {
      delay(2);
    }
  }
  String body;
  while (cli.available() && body.length() < 512) {
    body += (char)cli.read();
  }
  cli.stop();

  if (code < 200 || code > 299) {
    if (errBuf && errCap) {
      if (code < 0) {
        snprintf(errBuf, errCap, "No HTTP status %.80s", body.c_str());
      } else {
        snprintf(errBuf, errCap, "HTTP %d %.80s", code, body.c_str());
      }
    }
    return false;
  }
  (void)body;
  return true;
}

/** Pause scans, join Wi‑Fi, convert CSV to WiGLE v1.6, upload, disconnect. */
static void wardPerformWigleUpload(const char* srcCsvPath, File* liveLog) {
  char apiUser[96];
  char apiTok[128];
  char staSsid[80];
  char staPass[80];
  bool useStoredWifi = false;
  if (!wardReadWigleConfig(apiUser, sizeof(apiUser), apiTok, sizeof(apiTok), staSsid, sizeof(staSsid), staPass,
                           sizeof(staPass), &useStoredWifi)) {
    wardNotify("WiGLE", "SD:/config/wigle.txt — line1: WiGLE API name, line2: token. If this ESP32 already "
                        "joined Wi‑Fi once, stop at line 2. Else add line3=SSID line4=password. Optional "
                        "line3 AUTO or - uses saved Wi‑Fi only.");
    return;
  }

  char b64[512];
  if (!wardBuildBasicAuth(apiUser, apiTok, b64, sizeof(b64))) {
    wardNotify("WiGLE", "Auth encode failed.");
    return;
  }

  wardLogPush("WiGLE: start");
  s_fgScanPaused = true;
  delay(450);

  if (liveLog) {
    liveLog->flush();
  }

  WiFi.scanDelete();
  WiFi.disconnect();
  delay(150);
  WiFi.mode(WIFI_STA);
  if (useStoredWifi) {
    WiFi.begin();
  } else {
    WiFi.begin(staSsid, staPass);
  }

  bool gotIp = false;
  for (int i = 0; i < 70; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      gotIp = true;
      break;
    }
    delay(400);
  }
  if (!gotIp) {
    wardNotify("WiGLE", useStoredWifi ? "WiFi failed. Connect this device to Wi‑Fi once in another tool, "
                                        "or add SSID + password on lines 3–4 of wigle.txt."
                                      : "WiFi connect failed (check wigle.txt SSID/pass).");
    WiFi.disconnect();
    delay(100);
    s_fgScanPaused = false;
    return;
  }

  wardLogPush("WiGLE: WiFi OK");
  File out = SD.open(WARD_WIGLE_TMP_CSV, FILE_WRITE);
  if (!out) {
    wardNotify("WiGLE", "Could not write temp CSV on SD.");
    WiFi.disconnect();
    delay(100);
    s_fgScanPaused = false;
    return;
  }
  wardLogPush("WiGLE: convert");
  if (!wardConvertWardCsvToWigle(srcCsvPath, out)) {
    out.close();
    SD.remove(WARD_WIGLE_TMP_CSV);
    wardNotify("WiGLE", "Could not read ward CSV or convert.");
    WiFi.disconnect();
    delay(100);
    s_fgScanPaused = false;
    return;
  }
  out.flush();
  out.close();

  File up = SD.open(WARD_WIGLE_TMP_CSV, FILE_READ);
  if (!up || up.size() < 64) {
    if (up) {
      up.close();
    }
    SD.remove(WARD_WIGLE_TMP_CSV);
    wardNotify("WiGLE", "Converted file empty or missing.");
    WiFi.disconnect();
    delay(100);
    s_fgScanPaused = false;
    return;
  }

  const char* slash = strrchr(srcCsvPath, '/');
  const char* base = slash ? slash + 1 : srcCsvPath;
  char uploadName[48];
  snprintf(uploadName, sizeof(uploadName), "%s", base);
  for (char* p = uploadName; *p; ++p) {
    if (*p == '"' || *p == '\\' || *p == '\r' || *p == '\n') {
      *p = '_';
    }
  }

  wardLogPush("WiGLE: POST");
  char err[160];
  bool ok = wardWigleStreamUpload(up, uploadName[0] ? uploadName : "wardrive.csv", b64, err, sizeof(err));
  up.close();
  SD.remove(WARD_WIGLE_TMP_CSV);

  WiFi.disconnect();
  delay(120);

  s_fgScanPaused = false;

  if (ok) {
    wardNotify("WiGLE", "Upload OK. Check wigle.net uploads.");
    } else {
    wardNotify("WiGLE", err[0] ? err : "Upload failed.");
    }
}

static void wardPrintCsvGpsPrefix(File& logf, uint32_t now) {
    logf.print((unsigned long)now);
    logf.print(',');
    logf.print(utcStr);
    logf.print(',');
    logf.print(dateStr);
    logf.print(',');
    if (!isnan(navLat) && !isnan(navLon)) {
      logf.printf("%.7f,%.7f,", navLat, navLon);
    } else {
      logf.print(",,");
    }
    if (!isnan(navAltM)) {
      logf.printf("%.1f,", (double)navAltM);
    } else {
      logf.print(',');
    }
    logf.print((int)fixQuality);
    logf.print(',');
    logf.print((unsigned)satsUsedGga);
    logf.print(',');
    if (hdopLive >= 0.f) {
      logf.printf("%.2f,", (double)hdopLive);
    } else {
      logf.print(',');
    }
}

static void wardDoWifiScanLog(File& logf, uint32_t now, uint32_t* linesWr, uint32_t* scanCt) {
  WiFi.scanDelete();
  const int n = WiFi.scanNetworks(false, true);
  (*scanCt)++;
  if (n <= 0) {
    wardLogPush("WiFi: 0 AP");
    WiFi.scanDelete();
    return;
  }
  int cap = n;
  if (cap > (int)s_cfgMaxAps) {
    cap = (int)s_cfgMaxAps;
  }
  const int best = WiFi.RSSI(0);
  const int ch0 = WiFi.channel(0);
  wardLogPushf("WiFi %d>%d ch%d %ddBm", n, cap, ch0, best);
  for (int i = 0; i < cap; ++i) {
    const String ss = WiFi.SSID(i);
    const char* ssidc = ss.c_str();
    const uint8_t* bssid = WiFi.BSSID(i);
    char mac[20];
    if (bssid) {
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
               bssid[3], bssid[4], bssid[5]);
    } else {
      mac[0] = '\0';
    }
    wardPrintCsvGpsPrefix(logf, now);
    logf.print("WIFI,");
    wardAppendCsvString(logf, ssidc);
    logf.print(',');
    logf.print(mac);
    logf.print(',');
    logf.print(WiFi.channel(i));
    logf.print(',');
    logf.print(WiFi.RSSI(i));
    logf.print(',');
    logf.println(wardAuthLabel(WiFi.encryptionType(i)));
    (*linesWr)++;
  }
  logf.flush();
  WiFi.scanDelete();
}

static bool s_wardBleScanReady = false;

static void wardEnsureBleScan() {
  if (s_wardBleScanReady) {
    return;
  }
  BLEScan* s = BLEDevice::getScan();
  if (!s) {
    return;
  }
  s->setActiveScan(true);
  s->setInterval(134);
  s->setWindow(99);
  s_wardBleScanReady = true;
}

static void wardDoBleScanLog(File& logf, uint32_t now, uint32_t* linesWr, uint32_t* scanCt) {
  wardEnsureBleScan();
  BLEScan* scan = BLEDevice::getScan();
  if (!scan) {
    (*scanCt)++;
    wardLogPush("BLE: no scan");
    return;
  }
  BLEScanResults results = scan->start(2, false);
  (*scanCt)++;
  const int n = results.getCount();
  if (n <= 0) {
    wardLogPush("BLE: 0 dev");
    logf.flush();
    return;
  }
  int cap = n;
  if (cap > (int)s_cfgMaxAps) {
    cap = (int)s_cfgMaxAps;
  }
  const int bleBest = results.getDevice(0).getRSSI();
  wardLogPushf("BLE %d>%d %ddBm", n, cap, bleBest);
  for (int i = 0; i < cap; ++i) {
    BLEAdvertisedDevice d = results.getDevice(i);
    char mac[24];
    snprintf(mac, sizeof(mac), "%s", d.getAddress().toString().c_str());
    const std::string bleName = d.getName();
    const char* namec = bleName.empty() ? "" : bleName.c_str();
    wardPrintCsvGpsPrefix(logf, now);
    logf.print("BLE,");
    wardAppendCsvString(logf, namec);
    logf.print(',');
    logf.print(mac);
    logf.print(',');
    logf.print(0);
    logf.print(',');
    logf.print(d.getRSSI());
    logf.print(',');
    logf.println("");
    (*linesWr)++;
  }
  logf.flush();
}

static void wardRunScansForMode(File& logf, uint32_t now, uint32_t* linesWr, uint32_t* scanCt) {
  const WardRadioMode mode = (WardRadioMode)s_cfgRadioMode;
  if (mode == WardRadioMode::WiFi || mode == WardRadioMode::Both) {
    wardDoWifiScanLog(logf, now, linesWr, scanCt);
  }
  if (mode == WardRadioMode::Ble || mode == WardRadioMode::Both) {
    wardDoBleScanLog(logf, now, linesWr, scanCt);
  }
}

static void wardFgScanTask(void* param) {
  File* logf = (File*)param;
  uint32_t lines = 0;
  uint32_t scans = 0;
  uint32_t lastScan = millis();
  s_fgLines = 0;
  s_fgScans = 0;

  while (!s_fgScanStop) {
    const uint32_t now = millis();
    if (!s_fgScanPaused && logf && (uint32_t)(now - lastScan) >= s_cfgScanMs) {
      lastScan = now;
      uint32_t addL = 0;
      uint32_t oneScan = 0;
      wardRunScansForMode(*logf, now, &addL, &oneScan);
      lines += addL;
      scans += oneScan;
      s_fgLines = lines;
      s_fgScans = scans;
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }

  s_fgScanTask = nullptr;
  vTaskDelete(nullptr);
}

static bool wardStartForegroundScanTask(File& logf) {
  if (s_fgScanTask) {
    return true;
  }
  s_fgScanStop = false;
  s_fgScanPaused = false;
  s_fgLines = 0;
  s_fgScans = 0;
  BaseType_t ok = xTaskCreatePinnedToCore(wardFgScanTask, "wardfg", 10240, &logf, 1, &s_fgScanTask, 0);
  return ok == pdPASS;
}

static void wardStopForegroundScanTask() {
  if (!s_fgScanTask) {
    return;
  }
  s_fgScanStop = true;
  for (int i = 0; i < 1200 && s_fgScanTask; i++) {
    delay(10);
  }
  if (!s_fgScanTask) {
    s_fgScanStop = false;
  }
}

static void wardBgTask(void* /*param*/) {
  lineLen = 0;
  rowCount = 0;
  lastGsvMs = 0;
  lastNavMs = 0;
  strncpy(utcStr, "--:--:--", sizeof(utcStr));
  strncpy(dateStr, "--/--/--", sizeof(dateStr));
  hdopLive = pdopLive = -1.f;
  fixQuality = 0;
  gsaFixMode = 1;
  satsUsedGga = 0;
  navLat = navLon = NAN;
  navAltM = NAN;
  rmcNavValid = false;

  gpsSerial.end();
  gpsSerial.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(80);

  char path[48];
  snprintf(path, sizeof(path), "/wd_%lu.csv", (unsigned long)millis());
  strncpy(s_bgPath, path, sizeof(s_bgPath));

  File logf = SD.open(path, FILE_WRITE);
  if (!logf) {
    s_bgPath[0] = '\0';
    gpsSerial.end();
    s_bgTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  logf.println(
      "epoch_ms,utc,date,lat,lon,alt_m,fix,sats,hdop,radio,ssid,bssid,ch,rssi_dbm,auth");
  logf.flush();

  uint32_t lines = 0;
  uint32_t scans = 0;
  uint32_t lastScan = 0;
  s_bgLines = 0;
  s_bgScans = 0;

  while (!s_bgStop) {
    feedSerial();
    const uint32_t now = millis();
    if (s_bgLog && (uint32_t)(now - lastScan) >= s_cfgScanMs) {
      lastScan = now;
      uint32_t addL = 0;
      uint32_t oneScan = 0;
      wardRunScansForMode(logf, now, &addL, &oneScan);
      lines += addL;
      scans += oneScan;
      s_bgLines = lines;
      s_bgScans = scans;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  logf.flush();
  logf.close();
  WiFi.scanDelete();
  WiFi.disconnect();
  gpsSerial.end();
  s_bgPath[0] = '\0';
  s_bgTask = nullptr;
  vTaskDelete(nullptr);
}

bool statusBarGpsIconActive() {
  return (s_bgTask != nullptr) || s_wardFgSessionForStatusBar;
}

void stopBackgroundIfRunning() {
  if (!s_bgTask) {
    return;
  }
  s_bgStop = true;
  for (int i = 0; i < 200; i++) {
    delay(15);
    if (!s_bgTask) {
      break;
    }
  }
  s_bgStop = false;
  drawStatusBar(readBatteryVoltage(), true);
}

static void wardStartBackground() {
  if (s_bgTask) {
    wardNotify("Wardriver", "Background run already active.");
    return;
  }
  if (!isSDCardAvailable()) {
    wardNotify("Wardriver", "SD required for background log.");
    return;
  }
  s_bgStop = false;
  s_bgLog = true;
  s_bgLines = 0;
  s_bgScans = 0;
  s_bgPath[0] = '\0';
  BaseType_t ok = xTaskCreatePinnedToCore(wardBgTask, "wardrive", 10240, nullptr, 1, &s_bgTask, 0);
  if (ok != pdPASS) {
    s_bgTask = nullptr;
    wardNotify("Wardriver", "Could not start background task.");
    return;
  }
  wardNotify("Wardriver", "Running in background. Open Wardriver to stop.");
}

static int wardPollToolbarHit() {
  int x = 0, y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return -1;
  }
  const uint32_t t = millis();
  if ((uint32_t)(t - s_lastTbMs) < 120u) {
    return -1;
  }
  if (!(y > kWardIconRowY && y < kWardIconRowY + kWardIconRowH)) {
    return -1;
  }
  int16_t ix[4];
  wardToolbarIconXs(WardPage::Main, ix);
  for (int i = 0; i < 4; i++) {
    if (x > ix[i] && x < ix[i] + kWardIconSz) {
      s_lastTbMs = t;
      s_lastWardInputMs = t;
      return i;
    }
  }
  return -1;
}

static int wardPollSettingsCombinedHit() {
  int x = 0, y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return -1;
  }
  const uint32_t t = millis();
  if ((uint32_t)(t - s_lastWardInputMs) < 120u) {
    return -1;
  }
  s_lastWardInputMs = t;

  if (y > kWardIconRowY && y < kWardIconRowY + kWardIconRowH) {
    int16_t ix[4];
    wardToolbarIconXs(WardPage::Settings, ix);
    if (x > ix[0] && x < ix[0] + kWardIconSz) {
      s_lastTbMs = t;
      return 0;
    }
    if (x > ix[3] && x < ix[3] + kWardIconSz) {
      s_lastTbMs = t;
      return 3;
    }
    return -1;
  }

  wardLayoutSettingsButtons();
  const int h = FeatureUI::hit(s_wardSetBtns, kWardSetBtnCount, x, y);
  if (h < 0) {
    return -1;
  }
  return 10 + h;
}

void session() {
  feature_exit_requested = false;

  const bool bgWas = (s_bgTask != nullptr);

  if (!bgWas && !isSDCardAvailable()) {
    wardNotify("Wardriver", "SD card required. Insert card and try again.");
    return;
  }

  // Clear menu leftovers immediately on entry, before UART/SD setup can delay first paint.
  tft.fillScreen(FEATURE_BG);
  drawStatusBar(readBatteryVoltage(), true, true);
  wardLogClear();
  wardLogPush("Wardrive");
  wardLogPush("Strip: back left; gear play RSS on right");

  WardPage page = WardPage::Main;
  bool fgLog = true;
  uint32_t linesWritten = 0;
  uint32_t scanCount = 0;
  uint32_t lastScanMs = 0;
  uint32_t lastUiMs = 0;
  char path[48] = "";

  File logf;
  bool fgFile = false;

  if (!bgWas) {
    lineLen = 0;
    strncpy(utcStr, "--:--:--", sizeof(utcStr));
    strncpy(dateStr, "--/--/--", sizeof(dateStr));
    hdopLive = pdopLive = -1.f;
    fixQuality = 0;
    gsaFixMode = 1;
    satsUsedGga = 0;
    navLat = navLon = NAN;
    navAltM = NAN;
    rmcNavValid = false;
    rowCount = 0;
    lastGsvMs = 0;
    lastNavMs = 0;

    gpsSerial.end();
    gpsSerial.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);

    snprintf(path, sizeof(path), "/wd_%lu.csv", (unsigned long)millis());
    logf = SD.open(path, FILE_WRITE);
    if (!logf) {
      wardNotify("Wardriver", "Could not create log file on SD.");
      gpsSerial.end();
      return;
    }
    fgFile = true;
    logf.println(
        "epoch_ms,utc,date,lat,lon,alt_m,fix,sats,hdop,radio,ssid,bssid,ch,rssi_dbm,auth");
    logf.flush();
    if (!wardStartForegroundScanTask(logf)) {
      wardNotify("Wardriver", "Could not start scan worker.");
      logf.close();
      gpsSerial.end();
      return;
    }
    wardLogPushf("CSV %s", path);
  }

  s_editScanMs = s_cfgScanMs;
  s_editMaxAps = s_cfgMaxAps;
  s_editRadioMode = (WardRadioMode)s_cfgRadioMode;

  wardUiInvalidateAll();
  uint32_t lastBarMs = millis();
  lastUiMs = lastBarMs - (uint32_t)WARDRIVE_UI_INTERVAL_MS;

  s_wardFgSessionForStatusBar = true;
  drawStatusBar(readBatteryVoltage(), true, true);

  while (true) {
    gWardSuppressBottomTouchExit = (page != WardPage::Main);
    if (shouldExit()) {
      break;
    }
    if (!bgWas) {
      feedSerial();
      const bool nf = (fixQuality > 0) && !isnan(navLat) && !isnan(navLon);
      if (s_wardGpsFixLog < 0) {
        s_wardGpsFixLog = (int8_t)(nf ? 1 : 0);
      } else {
        const int8_t pf = s_wardGpsFixLog;
        const int8_t nowSt = (int8_t)(nf ? 1 : 0);
        if (nowSt != pf) {
          s_wardGpsFixLog = nowSt;
          wardLogPush(nf ? "GPS: fix" : "GPS: lost");
        }
      }
    }

    const uint32_t now = millis();

    if (isNotificationVisible()) {
      int nx = 0, ny = 0;
      if (readTouchXYDismiss(nx, ny)) {
        s_lastWardInputMs = millis();
        const NotificationAction na = notificationHandleTouch(nx, ny);
        if (na != NotificationAction::None) {
          wardUiInvalidateAll();
          if (page == WardPage::WigleCfg) {
            s_wigleCfgDirty = true;
          }
          lastUiMs = now - (uint32_t)WARDRIVE_UI_INTERVAL_MS;
        }
        delay(4);
        continue;
      }
    }

    int hit = -1;
    if (page == WardPage::Settings) {
      hit = wardPollSettingsCombinedHit();
    } else if (page == WardPage::WigleCfg) {
      hit = wardPollWigleCfgCombinedHit();
    } else {
      hit = wardPollToolbarHit();
    }
    if (hit >= 0) {
      lastUiMs = now - (uint32_t)WARDRIVE_UI_INTERVAL_MS;
      if (page == WardPage::WigleCfg) {
        if (hit >= 10) {
          const int bh = hit - 10;
          if (bh == 0) {
            wardWigleRunFieldEditor("API name", s_wigUser, sizeof(s_wigUser), 63, true);
          } else if (bh == 1) {
            wardWigleRunFieldEditor("API token", s_wigTok, sizeof(s_wigTok), 127, true);
          } else if (bh == 2) {
            if (s_wigUseSavedWifi) {
              wardNotify("WiGLE", "Turn Auto Wi-Fi off (tap bottom row) to edit SSID.");
            } else {
              wardWigleRunFieldEditor("Wi-Fi SSID", s_wigSsid, sizeof(s_wigSsid), 31, false);
            }
          } else if (bh == 3) {
            if (s_wigUseSavedWifi) {
              wardNotify("WiGLE", "Turn Auto Wi-Fi off to edit password.");
            } else {
              wardWigleRunFieldEditor("Wi-Fi password", s_wigPass, sizeof(s_wigPass), 63, false);
            }
          } else if (bh == 4) {
            s_wigUseSavedWifi = !s_wigUseSavedWifi;
            if (s_wigUseSavedWifi) {
              s_wigSsid[0] = '\0';
              s_wigPass[0] = '\0';
            }
            wardLogPush(s_wigUseSavedWifi ? "WiGLE: auto Wi-Fi ON" : "WiGLE: auto Wi-Fi OFF");
            s_wigleCfgDirty = true;
          }
        } else if (hit == 0) {
          wardLogPush("WiGLE cfg: back");
          page = WardPage::Settings;
          wardUiInvalidateAll();
        } else if (hit == 3) {
          wardWigleSaveCfgToSd();
          wardUiInvalidateAll();
        }
      } else if (page == WardPage::Settings) {
        if (hit >= 10) {
          const int bodyHit = hit - 10;
          if (bodyHit == 0) {
            s_editRadioMode = WardRadioMode::WiFi;
            wardLogPush("Pick: WiFi");
          } else if (bodyHit == 1) {
            s_editRadioMode = WardRadioMode::Ble;
            wardLogPush("Pick: BLE");
          } else if (bodyHit == 2) {
            s_editRadioMode = WardRadioMode::Both;
            wardLogPush("Pick: WiFi+BLE");
          } else if (bodyHit == 3) {
          if (s_editScanMs > 2500u) {
            s_editScanMs -= 500u;
              wardLogPushf("Scan %lums", (unsigned long)s_editScanMs);
          }
          } else if (bodyHit == 4) {
          if (s_editScanMs < 20000u) {
            s_editScanMs += 500u;
              wardLogPushf("Scan %lums", (unsigned long)s_editScanMs);
            }
          } else if (bodyHit == 5) {
            if (s_editMaxAps > 8) {
              s_editMaxAps -= 4;
              wardLogPushf("Cap %d", s_editMaxAps);
            }
          } else if (bodyHit == 6) {
            if (s_editMaxAps < 64) {
              s_editMaxAps += 4;
              wardLogPushf("Cap %d", s_editMaxAps);
            }
          } else if (bodyHit == 7) {
            if (s_bgTask) {
              wardNotify("WiGLE", "Stop background wardriver first.");
            } else if (!path[0]) {
              wardNotify("WiGLE", "No CSV (foreground log only).");
            } else {
              wardLogPush("WiGLE: upload tap");
              if (fgFile) {
                logf.flush();
              }
              wardPerformWigleUpload(path, fgFile ? &logf : nullptr);
              wardUiInvalidateAll();
            }
          } else if (bodyHit == 8) {
            if (!isSDCardAvailable()) {
              wardNotify("WiGLE", "SD required for WiGLE config.");
            } else {
              wardWigleLoadEditBuffers();
              page = WardPage::WigleCfg;
              s_wigleCfgDirty = true;
              wardUiInvalidateAll();
              wardLogPush("WiGLE editor");
            }
          }
        } else if (hit == 0) {
          wardLogPush("Setup: back");
          page = WardPage::Main;
          wardUiInvalidateAll();
        } else if (hit == 3) {
          wardLogPush("Setup: saved");
          s_cfgScanMs = s_editScanMs;
          s_cfgMaxAps = s_editMaxAps;
          s_cfgRadioMode = (uint8_t)s_editRadioMode;
          wardLogPushf("Cfg %lums %s cap%d", (unsigned long)s_cfgScanMs,
                        wardRadioModeShort((WardRadioMode)s_cfgRadioMode), (int)s_cfgMaxAps);
          page = WardPage::Main;
          wardUiInvalidateAll();
        }
      } else {
        if (hit == 0) {
          wardLogPush("Open setup");
          s_editScanMs = s_cfgScanMs;
          s_editMaxAps = s_cfgMaxAps;
          s_editRadioMode = (WardRadioMode)s_cfgRadioMode;
          page = WardPage::Settings;
          wardUiInvalidateAll();
        } else if (hit == 1) {
          if (s_bgTask) {
            s_bgLog = !s_bgLog;
            wardLogPush(s_bgLog ? "BG log ON" : "BG log OFF");
          } else {
            fgLog = !fgLog;
            s_fgScanPaused = !fgLog;
            wardLogPush(fgLog ? "Log ON" : "Log OFF");
          }
        } else if (hit == 2) {
          if (s_bgTask) {
            wardLogPush("Stop background");
            s_bgStop = true;
            for (int w = 0; w < 200 && s_bgTask; w++) {
              delay(15);
            }
            s_bgStop = false;
          } else {
            if (!fgLog) {
              wardNotify("Wardriver", "Turn logging on (play), then tap RSS for Auto.");
            } else {
              wardLogPush("Handoff to BG");
              wardStopForegroundScanTask();
              logf.flush();
              logf.close();
              fgFile = false;
              WiFi.scanDelete();
              gpsSerial.end();
              wardStartBackground();
              s_wardFgSessionForStatusBar = false;
              drawStatusBar(readBatteryVoltage(), true);
              drainButtons();
              gWardSuppressBottomTouchExit = false;
              return;
            }
          }
        } else if (hit == 3) {
          wardLogPush("Exit");
          feature_exit_requested = true;
        }
      }
    }

    if ((uint32_t)(now - lastUiMs) >= (uint32_t)WARDRIVE_UI_INTERVAL_MS) {
      lastUiMs = now;
      if (lastBarMs == 0 || (uint32_t)(now - lastBarMs) >= 1000u) {
        lastBarMs = now;
        drawStatusBar(readBatteryVoltage(), false, true);
      }
      const bool bgOn = (s_bgTask != nullptr);
      const bool logOn = bgOn ? (bool)s_bgLog : fgLog;
      bool wardRepaintedBody = false;
      if (wardTbDirty(page, bgOn, logOn)) {
        wardDrawIconToolbar(page, bgOn, logOn);
        wardTbSave(page, bgOn, logOn);
      }
      if (page == WardPage::WigleCfg) {
        if (s_wigleCfgDirty) {
          wardDrawWigleCfgPage();
          s_wigleCfgDirty = false;
          wardRepaintedBody = true;
        }
      } else if (page == WardPage::Settings) {
        if (wardSetDirty()) {
          wardDrawSettingsPage();
          wardSetSave();
          wardRepaintedBody = true;
        }
      } else {
        const bool bg = bgOn;
        const uint32_t dispL = bg ? s_bgLines : s_fgLines;
        const uint32_t dispS = bg ? s_bgScans : s_fgScans;
        const char* dispP = bg ? s_bgPath : path;
        if (wardMainDirty(dispL, dispS, dispP, bg ? s_bgLog : fgLog, bg)) {
          wardDrawBody(dispL, dispS, dispP, bg ? s_bgLog : fgLog, bg);
          wardMainSave(dispL, dispS, dispP, bg ? s_bgLog : fgLog, bg);
          wardRepaintedBody = true;
        }
      }
      if (wardRepaintedBody && isNotificationVisible()) {
        notificationRedrawIfVisible();
      }
    }

    if (!bgWas) {
      s_fgScanPaused = !fgLog || page != WardPage::Main || (uint32_t)(now - s_lastWardInputMs) < 650u;
      linesWritten = s_fgLines;
      scanCount = s_fgScans;
      lastScanMs = now;
    }

    delay(4);
  }
  gWardSuppressBottomTouchExit = false;
  s_wardFgSessionForStatusBar = false;

  if (!bgWas && fgFile) {
    wardStopForegroundScanTask();
    logf.flush();
    logf.close();
    WiFi.scanDelete();
    WiFi.disconnect();
    gpsSerial.end();
  }

  feature_exit_requested = false;
  drainButtons();
  drawStatusBar(readBatteryVoltage(), true);
}

} 

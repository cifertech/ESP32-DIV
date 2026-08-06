/*
 * Jamming Detector — receive-only SubGHz jam monitor for ESP32-DIV.
 *
 * Watches a single frequency (default 434.42 MHz, the common EU keyless-entry
 * band) and flags a jamming attack. A real key-fob keys up in short bursts
 * (tens of milliseconds); a jammer holds the channel busy continuously. The
 * detector separates the two by looking at how LONG the channel stays busy and
 * the duty cycle over a ~1 s window, not just raw signal strength.
 *
 * Output is visual (big CLEAR / JAMMING status box + RSSI/noise-floor readout +
 * an FFT waterfall) and optionally logged to /logs/jamdet.csv on the SD card.
 * This is a RX-only tool — it never transmits.
 *
 * Hooks into the SubGHz submenu as "Jamming Detector" (see ESP32-DIV.ino) via
 * namespace jammingdetector { Setup(); Loop(); } declared in config.h.
 */

#include <SD.h>
#include "config.h"   // tft, pcf, ELECHOUSE_cc1101, arduinoFFT, CC1101 driver
#include "shared.h"   // pin map, UI_* colours, feature flags
#include "utils.h"    // nav bar / status bar / SD helpers
#include "icon.h"

namespace jammingdetector {

/*──────────────────── Tunables ────────────────────*/
static constexpr uint16_t JD_SAMPLES        = 256;     // RSSI samples per window (also FFT size)
static constexpr double   JD_SAMPLE_HZ      = 5000.0;  // RSSI sample rate → ~51 ms per window
static constexpr double   JD_RXBW           = 650.0;   // wide RX bandwidth to catch off-tune/swept jammers
static constexpr int      JD_MARGIN_DB      = 18;      // "busy" = this many dB above the noise floor
static constexpr int      JD_ABS_THRESH_DBM = -75;     // absolute busy floor, whatever the noise floor says
static constexpr float    JD_BUSY_WIN_DUTY  = 0.50f;   // a window counts as "busy" for the streak above this duty
static constexpr uint32_t JD_JAM_STREAK_MS  = 400;     // continuous busy longer than this → JAMMING
static constexpr float    JD_JAM_AVG_DUTY   = 0.80f;   // OR average duty over the ring window this high → JAMMING
static constexpr float    JD_ACTIVITY_DUTY  = 0.10f;   // above this (but not jam) = transient activity (e.g. a fob)
static constexpr uint8_t  JD_RING           = 20;      // ~1 s of windows for the duty average
static constexpr int      JD_FLOOR_INIT_DBM = -95;

/*──────────────────── Frequencies (FOB-relevant) ────────────────────*/
static const uint32_t kFreqHz[]    = { 433920000UL, 434420000UL, 315000000UL, 868350000UL };
static const char*    kFreqLabel[] = { "433.92",    "434.42",    "315.00",    "868.35"    };
static constexpr uint8_t kFreqCount = sizeof(kFreqHz) / sizeof(kFreqHz[0]);
static uint8_t freqIdx = 1;  // default → 434.42 MHz

/*──────────────────── Waterfall (FFT) state ────────────────────*/
static arduinoFFT  FFTjd = arduinoFFT();
static double      vReal[JD_SAMPLES];
static double      vImag[JD_SAMPLES];
static byte        palR[128], palG[128], palB[128];
static unsigned int samplingPeriod = 0;
static unsigned int wfEpoch = 0;
static double       wfAtten = 10.0;

/*──────────────────── Detector state ────────────────────*/
static float    noiseFloor   = JD_FLOOR_INIT_DBM;  // adaptive, tracks quiet windows
static uint32_t busyStreakMs = 0;                   // continuous busy time
static float    dutyRing[JD_RING];
static uint8_t  dutyRingPos  = 0;
static bool     jamActive    = false;
static uint32_t jamStartMs   = 0;
static int      jamPeakDbm   = -127;
static uint32_t eventCount   = 0;
static uint32_t lastEventDurMs = 0;
static int      lastEventPeak = 0;

/*──────────────────── UI / IO state ────────────────────*/
static bool logEnabled  = true;
static bool logMounted  = false;
static bool logTried    = false;
static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;

// Layout (240x320, rotation 2)
static constexpr int16_t INFO_Y   = 24;
static constexpr int16_t STATUS_Y = 74;
static constexpr int16_t STATUS_H = 46;
static constexpr int16_t WF_TOP   = 132;
static constexpr int16_t WF_CENTER_X = 120;

/*──────────────────── CC1101 helpers ────────────────────*/
static void cc1101BeginRx() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);       // ASK/OOK — energy detection
  ELECHOUSE_cc1101.setRxBW(JD_RXBW);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.setMHZ(kFreqHz[freqIdx] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
}

static void tuneTo(uint8_t idx) {
  freqIdx = idx % kFreqCount;
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(kFreqHz[freqIdx] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
}

/*──────────────────── SD logging ────────────────────*/
static void logEnsureMounted() {
  if (logMounted) return;            // already mounted; retry each call until it succeeds
  logTried = true;
  logMounted = sdMountChipSelect(SD_CS);
  if (logMounted && !SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
}

static void logEvent(uint32_t whenMs, uint32_t durMs, int peakDbm, int dutyPct) {
  if (!logEnabled) return;
  // CC1101 and the SD card share the SPI bus (pins 11/12/13). Take the bus for
  // SD, do ALL the SD work, then hand it back to the CC1101 — restoring the
  // radio before the write makes SD.open() fail silently.
  sdSpiInit();
  logEnsureMounted();
  if (logMounted) {
    if (!SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
    File f = SD.open(LOG_DIR "/jamdet.csv", FILE_APPEND);
    if (f) {
      // uptime_ms, freq_MHz, event, peak_rssi_dBm, duration_ms, duty_pct
      f.printf("%lu,%s,JAM,%d,%lu,%d\n",
               (unsigned long)whenMs, kFreqLabel[freqIdx], peakDbm,
               (unsigned long)durMs, dutyPct);
      f.close();
    }
  }
  cc1101BeginRx();  // restore CC1101 RX only after every SD operation is done
}

/*──────────────────── Drawing ────────────────────*/
static void buildPalette() {
  for (int i = 0; i < 32; i++)  { palR[i] = i / 2; palG[i] = 0;            palB[i] = i;       }
  for (int i = 32; i < 64; i++) { palR[i] = i / 2; palG[i] = 0;            palB[i] = 63 - i;  }
  for (int i = 64; i < 96; i++) { palR[i] = 31;    palG[i] = (i - 64) * 2; palB[i] = 0;       }
  for (int i = 96; i < 128; i++){ palR[i] = 31;    palG[i] = 63;           palB[i] = i - 96;  }
}

static int16_t wfBottom() {
  int16_t b = touchNavContentBottomY();
  if (b <= WF_TOP + 8) b = 300;  // fallback if nav bar height unknown
  return b;
}

static void drawStaticChrome() {
  tft.fillRect(0, 20, 240, wfBottom() - 20, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(6, INFO_Y);
  tft.print("Jamming Detector");
  tft.drawFastHLine(0, WF_TOP - 2, 240, UI_LINE);
}

static void drawInfo(int rssiNow) {
  char buf[40];
  tft.setTextSize(1);

  tft.fillRect(0, INFO_Y + 13, 240, STATUS_Y - (INFO_Y + 13), TFT_BLACK);

  snprintf(buf, sizeof(buf), "Freq %s MHz", kFreqLabel[freqIdx]);
  tft.setTextColor(UI_WARN, TFT_BLACK);
  tft.setCursor(6, INFO_Y + 14);
  tft.print(buf);

  snprintf(buf, sizeof(buf), "RSSI %d  Floor %d dBm", rssiNow, (int)noiseFloor);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(6, INFO_Y + 28);
  tft.print(buf);

  int dutyPct = (int)(dutyRing[(dutyRingPos + JD_RING - 1) % JD_RING] * 100.0f);
  snprintf(buf, sizeof(buf), "Duty %3d%%  Events %lu  Log %s",
           dutyPct, (unsigned long)eventCount, logEnabled ? "on" : "off");
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(6, INFO_Y + 42);
  tft.print(buf);
}

static void drawStatusBox(bool jam, bool activity) {
  uint16_t bg   = jam ? TFT_RED : (activity ? UI_WARN : TFT_DARKGREEN);
  const char* s = jam ? "JAMMING DETECTED" : (activity ? "ACTIVITY" : "CLEAR");
  tft.fillRoundRect(6, STATUS_Y, 228, STATUS_H, 4, bg);
  tft.drawRoundRect(6, STATUS_Y, 228, STATUS_H, 4, UI_LINE);
  tft.setTextColor(TFT_WHITE, bg);
  // Auto-fit: big text for short states, one notch smaller for the long alert.
  uint8_t sz = 3;
  tft.setTextSize(sz);
  if (tft.textWidth(s) > 216) { sz = 2; tft.setTextSize(sz); }
  int16_t tw = tft.textWidth(s);
  int16_t th = 8 * sz;
  tft.setCursor(120 - tw / 2, STATUS_Y + (STATUS_H - th) / 2);
  tft.print(s);
  tft.setTextSize(1);
}

/*──────────────────── One sample+FFT window; returns detection stats ────────────────────*/
struct WindowStat { int peakDbm; int minDbm; float duty; uint32_t elapsedMs; };

static WindowStat sampleWindow() {
  const int busyThresh = max(JD_ABS_THRESH_DBM, (int)(noiseFloor + JD_MARGIN_DB));
  int peak = -127, lo = 0;
  uint16_t busy = 0;
  float ewma = -50.0f;   // for the visual, same seed as the replay waterfall
  const float ALPHA = 0.2f;

  uint32_t t0 = millis();
  uint32_t micro_s = micros();
  for (int i = 0; i < JD_SAMPLES; i++) {
    int dbm = ELECHOUSE_cc1101.getRssi();
    if (dbm > peak) peak = dbm;
    if (dbm < lo)   lo = dbm;
    if (dbm > busyThresh) busy++;

    ewma = (ALPHA * (dbm + 100)) + ((1 - ALPHA) * ewma);
    vReal[i] = ewma * 2;
    vImag[i] = 1;

    while (micros() < micro_s + samplingPeriod) { /* pace */ }
    micro_s += samplingPeriod;
  }

  // FFT → waterfall row
  double mean = 0;
  for (uint16_t i = 0; i < JD_SAMPLES; i++) mean += vReal[i];
  mean /= JD_SAMPLES;
  for (uint16_t i = 0; i < JD_SAMPLES; i++) vReal[i] -= mean;

  FFTjd.Windowing(vReal, JD_SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFTjd.Compute(vReal, vImag, JD_SAMPLES, FFT_FORWARD);
  FFTjd.ComplexToMagnitude(vReal, vImag, JD_SAMPLES);

  const int16_t bottom = wfBottom();
  int rowY = WF_TOP + (int)wfEpoch;
  if (rowY < bottom - 1) {
    int maxk = 0;
    for (int j = 0; j < JD_SAMPLES >> 1; j++) {
      int k = vReal[j] / wfAtten;
      if (k > maxk) maxk = k;
      if (k > 127) k = 127;
      if (k < 0)   k = 0;
      unsigned int color = palR[k] << 11 | palG[k] << 5 | palB[k];
      tft.drawPixel(WF_CENTER_X + j, rowY, color);
      tft.drawPixel(WF_CENTER_X - j, rowY, color);
    }
    double ta = maxk / 127.0;
    if (ta > wfAtten) wfAtten = ta;
  }
  wfEpoch++;
  if (WF_TOP + (int)wfEpoch >= bottom) wfEpoch = 0;

  WindowStat st;
  st.peakDbm   = peak;
  st.minDbm    = lo;
  st.duty      = (float)busy / JD_SAMPLES;
  st.elapsedMs = millis() - t0;
  return st;
}

/*──────────────────── Detection state machine ────────────────────*/
static void evaluate(const WindowStat& st) {
  // adaptive noise floor: only relax it toward quiet windows, so a sustained
  // jammer can never drag the floor up to hide itself
  if (st.duty < 0.2f) noiseFloor = 0.95f * noiseFloor + 0.05f * st.minDbm;

  dutyRing[dutyRingPos] = st.duty;
  dutyRingPos = (dutyRingPos + 1) % JD_RING;
  float avgDuty = 0;
  for (uint8_t i = 0; i < JD_RING; i++) avgDuty += dutyRing[i];
  avgDuty /= JD_RING;

  if (st.duty >= JD_BUSY_WIN_DUTY) busyStreakMs += st.elapsedMs;
  else                             busyStreakMs = 0;

  bool jam = (busyStreakMs >= JD_JAM_STREAK_MS) || (avgDuty >= JD_JAM_AVG_DUTY);

  if (jam && !jamActive) {
    jamActive  = true;
    jamStartMs = millis();
    jamPeakDbm = st.peakDbm;
    eventCount++;
  } else if (jam && jamActive) {
    if (st.peakDbm > jamPeakDbm) jamPeakDbm = st.peakDbm;
  } else if (!jam && jamActive) {
    jamActive      = false;
    lastEventDurMs = millis() - jamStartMs;
    lastEventPeak  = jamPeakDbm;
    logEvent(jamStartMs, lastEventDurMs, jamPeakDbm, (int)(avgDuty * 100));
  }
}

/*──────────────────── Input ────────────────────*/
static bool edge(int pin, bool& prev) {
  bool now = isPhysicalButtonPressed(pin);
  bool e = now && !prev;
  prev = now;
  return e;
}

static void handleInput() {
  bool navFreqDown = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_LEFT);
  bool navFreqUp   = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_RIGHT);
  bool navReset    = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_UP);
  bool navLog      = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_DOWN);

  if (edge(BTN_LEFT, prevLeft)   || navFreqDown) { tuneTo(freqIdx + kFreqCount - 1); wfAtten = 10; }
  if (edge(BTN_RIGHT, prevRight) || navFreqUp)   { tuneTo(freqIdx + 1);             wfAtten = 10; }
  if (edge(BTN_UP, prevUp) || navReset) {
    eventCount = 0; busyStreakMs = 0; jamActive = false;
    noiseFloor = JD_FLOOR_INIT_DBM;
    for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
    tft.fillRect(0, WF_TOP, 240, wfBottom() - WF_TOP, TFT_BLACK);
    wfEpoch = 0;
  }
  if (edge(BTN_DOWN, prevDown) || navLog) logEnabled = !logEnabled;
}

/*──────────────────── Public entry points ────────────────────*/
void Setup() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Freq-", "Log", "Exit", "Reset", "Freq+");

  cc1101BeginRx();
  tuneTo(freqIdx);

  samplingPeriod = round(1000000.0 * (1.0 / JD_SAMPLE_HZ));
  buildPalette();

  noiseFloor = JD_FLOOR_INIT_DBM;
  busyStreakMs = 0;
  jamActive = false;
  wfEpoch = 0;
  wfAtten = 10.0;
  for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
  prevLeft = prevRight = prevUp = prevDown = false;

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT,  INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP,    INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN,  INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  tft.setRotation(TFT_ROTATION);
  drawStatusBar(readBatteryVoltage(), true);
  maintainTouchNavBar();
  drawStaticChrome();
  drawStatusBox(false, false);
}

void Loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  drawStatusBar(readBatteryVoltage(), false);
  handleInput();

  WindowStat st = sampleWindow();
  evaluate(st);

  bool activity = !jamActive && (st.duty >= JD_ACTIVITY_DUTY);
  drawStatusBox(jamActive, activity);
  drawInfo(st.peakDbm);
}

} // namespace jammingdetector

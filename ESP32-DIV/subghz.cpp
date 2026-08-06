#include <algorithm>
#include <vector>
#include "KeyboardUI.h"
#include "Touchscreen.h"
#include "config.h"
#include "icon.h"
#include "shared.h"


namespace {
  static constexpr const char* SUBGHZ_DIR = "/subghz";
  static constexpr const char* SUBGHZ_EXPORT_PREFIX = "/subghz/profiles_";
  static constexpr const char* SUBGHZ_CURRENT_PATH = "/subghz/profiles_current.bin";
  static constexpr uint32_t SUBGHZ_EXPORT_MAGIC = 0x315A4753;

  struct __attribute__((packed)) SubGhzProfile {
    uint32_t frequency;
    uint32_t value;
    uint16_t bitLength;
    uint16_t protocol;
    char     name[16];
  };

  static constexpr uint16_t MAX_NAME_LENGTH = 16;
  static constexpr uint16_t PROFILE_SIZE = sizeof(SubGhzProfile);

  static constexpr uint16_t ADDR_VALUE = 1280;
  static constexpr uint16_t ADDR_BITLEN = 1284;
  static constexpr uint16_t ADDR_PROTO = 1286;
  static constexpr uint16_t ADDR_FREQ = 1288;
  static constexpr uint16_t ADDR_PROFILE_COUNT = 1296;
  static constexpr uint16_t ADDR_PROFILE_START = 1300;
  static constexpr uint16_t MAX_PROFILES = 5;

  struct __attribute__((packed)) SubGhzExportHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t profileSize;
    uint16_t reserved;
  };

  static bool subghz_sd_mounted = false;
  static bool subghzMountSD() {
    if (subghz_sd_mounted) {
      if (SD.exists("/")) return true;
      subghz_sd_mounted = false;
    }

#if defined(CC1101_CS)
    pinMode(CC1101_CS, OUTPUT);
    digitalWrite(CC1101_CS, HIGH);
#endif

    restoreSdAfterSharedSpi();
    if (isSDCardAvailable()) {
      subghz_sd_mounted = true;
      return true;
    }
    return false;
  }

  static bool subghzEnsureDir(const char* dirPath) {
    if (!subghzMountSD()) return false;
    if (SD.exists(dirPath)) return true;
    if (SD.mkdir(dirPath)) return true;
    if (dirPath && dirPath[0] == '/') return SD.mkdir(dirPath + 1);
    return false;
  }

  static void clearProfilesInEeprom() {

    uint16_t zero = 0;
    EEPROM.put(ADDR_PROFILE_COUNT, zero);
    SubGhzProfile empty{};
    for (uint16_t i = 0; i < MAX_PROFILES; i++) {
      EEPROM.put(ADDR_PROFILE_START + (i * PROFILE_SIZE), empty);
    }
    EEPROM.commit();
  }

  static bool makeNextExportPath(String& outPath) {

    for (uint16_t i = 0; i < 10000; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s%04u.bin", SUBGHZ_EXPORT_PREFIX, (unsigned)i);
      if (!SD.exists(buf)) { outPath = String(buf); return true; }
    }
    return false;
  }

  static bool findLatestExportPath(String& outPath) {

    for (int i = 9999; i >= 0; i--) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s%04u.bin", SUBGHZ_EXPORT_PREFIX, (unsigned)i);
      if (SD.exists(buf)) { outPath = String(buf); return true; }
    }
    return false;
  }

  static bool exportProfilesToSD(String& outPath, String* errOut = nullptr) {
    if (!subghzEnsureDir(SUBGHZ_DIR)) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }

    uint16_t count = 0;
    EEPROM.get(ADDR_PROFILE_COUNT, count);
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    if (!makeNextExportPath(outPath)) {
      if (errOut) *errOut = "No free filename";
      return false;
    }

    File f = SD.open(outPath.c_str(), FILE_WRITE);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    h.magic = SUBGHZ_EXPORT_MAGIC;
    h.version = 1;
    h.count = count;
    h.profileSize = PROFILE_SIZE;
    h.reserved = 0;

    bool ok = (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
    for (uint16_t i = 0; ok && i < count; i++) {
      SubGhzProfile p{};
      int addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
      EEPROM.get(addr, p);
      ok = (f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p));
    }
    f.close();

    if (!ok && errOut) *errOut = "Write failed";
    return ok;
  }

  static bool syncCurrentProfilesToSD(String* errOut = nullptr) {

    if (!subghzEnsureDir(SUBGHZ_DIR)) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }

    uint16_t count = 0;
    EEPROM.get(ADDR_PROFILE_COUNT, count);
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    if (SD.exists(SUBGHZ_CURRENT_PATH)) SD.remove(SUBGHZ_CURRENT_PATH);
    File f = SD.open(SUBGHZ_CURRENT_PATH, FILE_WRITE);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    h.magic = SUBGHZ_EXPORT_MAGIC;
    h.version = 1;
    h.count = count;
    h.profileSize = PROFILE_SIZE;
    h.reserved = 0;

    bool ok = (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
    for (uint16_t i = 0; ok && i < count; i++) {
      SubGhzProfile p{};
      int addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
      EEPROM.get(addr, p);
      ok = (f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p));
    }
    f.close();
    if (!ok && errOut) *errOut = "Write failed";
    return ok;
  }

  static bool importProfilesFromSD(const String& path, String* errOut = nullptr) {
    if (!subghzMountSD()) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }
    if (path.isEmpty() || !SD.exists(path.c_str())) {
      if (errOut) *errOut = "File not found";
      return false;
    }

    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) { f.close(); if (errOut) *errOut="Bad header"; return false; }
    if (h.magic != SUBGHZ_EXPORT_MAGIC || h.version != 1) { f.close(); if (errOut) *errOut="Wrong file"; return false; }
    if (h.profileSize != PROFILE_SIZE) { f.close(); if (errOut) *errOut="Size mismatch"; return false; }

    uint16_t count = h.count;
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    clearProfilesInEeprom();
    for (uint16_t i = 0; i < count; i++) {
      SubGhzProfile p{};
      if (f.read((uint8_t*)&p, sizeof(p)) != sizeof(p)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
      p.name[MAX_NAME_LENGTH - 1] = '\0';
      EEPROM.put(ADDR_PROFILE_START + (i * PROFILE_SIZE), p);
    }
    EEPROM.put(ADDR_PROFILE_COUNT, count);
    EEPROM.commit();
    f.close();
    return true;
  }

  struct SubGhzFileEntry {
    String path;
    uint16_t count = 0;
    bool isCurrent = false;
  };

  static bool readExportHeader(File& f, SubGhzExportHeader& out, String* errOut = nullptr) {
    if (f.read((uint8_t*)&out, sizeof(out)) != sizeof(out)) { if (errOut) *errOut="Bad header"; return false; }
    if (out.magic != SUBGHZ_EXPORT_MAGIC || out.version != 1) { if (errOut) *errOut="Wrong file"; return false; }
    if (out.profileSize != PROFILE_SIZE) { if (errOut) *errOut="Size mismatch"; return false; }
    return true;
  }

  static bool readProfileAt(const String& path, uint16_t localIndex, SubGhzProfile& out, String* errOut = nullptr) {
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) { if (errOut) *errOut="Open failed"; return false; }
    SubGhzExportHeader h{};
    if (!readExportHeader(f, h, errOut)) { f.close(); return false; }
    uint16_t count = h.count; if (count > MAX_PROFILES) count = MAX_PROFILES;
    if (localIndex >= count) { f.close(); if (errOut) *errOut="Index OOR"; return false; }
    uint32_t off = (uint32_t)sizeof(SubGhzExportHeader) + (uint32_t)localIndex * (uint32_t)PROFILE_SIZE;
    if (!f.seek(off)) { f.close(); if (errOut) *errOut="Seek failed"; return false; }
    if (f.read((uint8_t*)&out, sizeof(out)) != sizeof(out)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
    out.name[MAX_NAME_LENGTH - 1] = '\0';
    f.close();
    return true;
  }

  static bool listAllProfileFiles(std::vector<SubGhzFileEntry>& out, String* errOut = nullptr) {
    out.clear();
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    if (!SD.exists(SUBGHZ_DIR)) { if (errOut) *errOut="No /subghz"; return false; }
    File d = SD.open(SUBGHZ_DIR);
    if (!d) { if (errOut) *errOut="Open dir failed"; return false; }

    for (;;) {
      File f = d.openNextFile();
      if (!f) break;
      if (f.isDirectory()) { f.close(); continue; }
      String name = String(f.name());

      String fullPath = String(SUBGHZ_DIR) + "/" + name;

      bool isCurrent = (name == "profiles_current.bin");
      bool isArchive = name.startsWith("profiles_") && name.endsWith(".bin") && !isCurrent;
      if (!isCurrent && !isArchive) { f.close(); continue; }

      SubGhzExportHeader h{};
      String herr;
      bool ok = readExportHeader(f, h, &herr);
      f.close();
      if (!ok) continue;

      uint16_t cnt = h.count;
      if (cnt > MAX_PROFILES) cnt = MAX_PROFILES;
      if (cnt == 0) continue;

      SubGhzFileEntry e;
      e.path = fullPath;
      e.count = cnt;
      e.isCurrent = isCurrent;
      out.push_back(e);
    }
    d.close();

    std::sort(out.begin(), out.end(), [](const SubGhzFileEntry& a, const SubGhzFileEntry& b) {
      if (a.isCurrent != b.isCurrent) return a.isCurrent > b.isCurrent;
      return a.path > b.path;
    });
    return true;
  }

  static uint16_t totalProfilesInIndex(const std::vector<SubGhzFileEntry>& files) {
    uint32_t total = 0;
    for (auto& f : files) total += f.count;
    if (total > 65535) total = 65535;
    return (uint16_t)total;
  }

  static bool locateGlobalIndex(const std::vector<SubGhzFileEntry>& files, uint16_t globalIndex,
                                String& outPath, uint16_t& outLocalIdx) {
    uint32_t idx = globalIndex;
    for (auto& fe : files) {
      if (idx < fe.count) {
        outPath = fe.path;
        outLocalIdx = (uint16_t)idx;
        return true;
      }
      idx -= fe.count;
    }
    return false;
  }

  static bool deleteProfileFromFile(const String& path, uint16_t localIndex, String* errOut = nullptr) {
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) { if (errOut) *errOut="Open failed"; return false; }
    SubGhzExportHeader h{};
    if (!readExportHeader(f, h, errOut)) { f.close(); return false; }
    uint16_t count = h.count; if (count > MAX_PROFILES) count = MAX_PROFILES;
    if (localIndex >= count) { f.close(); if (errOut) *errOut="Index OOR"; return false; }

    SubGhzProfile buf[MAX_PROFILES]{};
    for (uint16_t i = 0; i < count; i++) {
      if (f.read((uint8_t*)&buf[i], sizeof(SubGhzProfile)) != sizeof(SubGhzProfile)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
      buf[i].name[MAX_NAME_LENGTH - 1] = '\0';
    }
    f.close();

    for (uint16_t i = localIndex; i + 1 < count; i++) buf[i] = buf[i + 1];
    count--;

    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File w = SD.open(path.c_str(), FILE_WRITE);
    if (!w) { if (errOut) *errOut="Open write failed"; return false; }

    SubGhzExportHeader nh{};
    nh.magic = SUBGHZ_EXPORT_MAGIC;
    nh.version = 1;
    nh.count = count;
    nh.profileSize = PROFILE_SIZE;
    nh.reserved = 0;
    bool ok = (w.write((const uint8_t*)&nh, sizeof(nh)) == sizeof(nh));
    for (uint16_t i = 0; ok && i < count; i++) {
      ok = (w.write((const uint8_t*)&buf[i], sizeof(SubGhzProfile)) == sizeof(SubGhzProfile));
    }
    w.close();
    if (!ok && errOut) *errOut="Write failed";

    if (ok && path.endsWith("profiles_current.bin")) {
      importProfilesFromSD(path, nullptr);
    }
    return ok;
  }
}

#ifdef TFT_BLACK
#undef TFT_BLACK
#endif
#define TFT_BLACK FEATURE_BG

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif
#ifndef FEATURE_WHITE
#define FEATURE_WHITE 0xFFFF
#endif

#ifdef TFT_WHITE
#undef TFT_WHITE
#endif
#define TFT_WHITE FEATURE_TEXT

#ifdef WHITE
#undef WHITE
#endif
#define WHITE FEATURE_WHITE

#ifdef DARK_GRAY
#undef DARK_GRAY
#endif
#define DARK_GRAY UI_FG

static constexpr int kSubghzScreenH = 320;

static int subghzContentBottom() {
  return featureHasTouchNavBar() ? touchNavContentBottomY() : kSubghzScreenH;
}

static void subghzClearBody(uint16_t color = TFT_BLACK) {
  if (featureHasTouchNavBar()) {
    featureClearContent(color);
  } else {
    tft.fillScreen(color);
  }
}

static constexpr unsigned long kSubghzNavDebounceMs = 200;

static void subghzWaitNavRelease(int pin) {
  while (isTouchNavButtonPressed(pin)) {
    delay(10);
  }
  delay(kSubghzNavDebounceMs);
}

static void subghzRedrawNavChrome() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  invalidateTouchButtonCue();
  redrawTouchButtonBar();
  maintainTouchNavBar();
}

static bool subghzWaitWithNav(uint32_t ms) {
  const uint32_t until = millis() + ms;
  while ((int32_t)(millis() - until) < 0) {
    if (feature_exit_requested || featureExitButtonPressed()) {
      return false;
    }
    if (featureHasTouchNavBar()) {
      maintainTouchNavBar();
    }
    delay(50);
  }
  return true;
}

static void subghzSetReplayNavLabels() {
  setTouchNavLabels("Freq-", "Save", "Exit", "Send", "Freq+");
}

static void subghzSetJammerNavLabels() {
  setTouchNavLabels("Freq-", "Auto", "Exit", "Toggle", "Freq+");
}

static void subghzSetProfileNavLabels() {
  setTouchNavLabels("Delete", "Next", "Exit", "Prev", "TX");
}

static void subghzSetBruteNavLabels() {
  setTouchNavLabels("Prev", "Sel", "Exit", "Go", "Next");
}

namespace replayat { void replayHandleNavButtons(); }
namespace subjammer { void subjammerHandleNavButtons(); }
namespace SavedProfile { void profileHandleNavButtons(); }
namespace SubBrute { void bruteHandleNavButtons(); }

namespace replayat {

#define EEPROM_SIZE 1440

#define ADDR_VALUE         1280
#define ADDR_BITLEN        1284
#define ADDR_PROTO         1286
#define ADDR_FREQ          1288
#define ADDR_PROFILE_COUNT 1296
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES       5

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define SCREEN_HEIGHT 320

static bool uiDrawn = false;

void runUI();
void sendSignal();
void saveProfile();
void updateDisplay();

#define MAX_NAME_LENGTH 16

const char* randomNames[] = {
  "Signal", "Remote", "KeyFob", "GateOpener", "DoorLock",
  "RFTest", "Profile", "Control", "Switch", "Beacon"
};
const uint8_t numRandomNames = 10;

struct __attribute__((packed)) Profile {
    uint32_t frequency;
    uint32_t value;
    uint16_t bitLength;
    uint16_t protocol;
    char name[MAX_NAME_LENGTH];
};

#define PROFILE_SIZE sizeof(Profile)

uint16_t profileCount = 0;

RCSwitch mySwitch = RCSwitch();
arduinoFFT FFTSUB = arduinoFFT();

const uint16_t samplesSUB = ESP32DIV_FFT_SAMPLES;
const double FrequencySUB = 5000;

double attenuation_num = 10;

unsigned int sampling_period;
unsigned long micro_s;

double vRealSUB[samplesSUB];
double vImagSUB[samplesSUB];

byte red[ESP32DIV_FFT_PALETTE_SIZE], green[ESP32DIV_FFT_PALETTE_SIZE],
     blue[ESP32DIV_FFT_PALETTE_SIZE];

unsigned int epochSUB = 0;
unsigned int colorcursor = 2016;

int rssi;

static constexpr uint8_t REPLAY_RX_PIN = SUBGHZ_RX_PIN;
static constexpr uint8_t REPLAY_TX_PIN = SUBGHZ_TX_PIN;

/** RCSwitch::disableReceive() errors if no ISR was ever attached — track arm state. */
static bool s_replayRxArmed = false;

static void replayArmReceive() {
  pinMode(REPLAY_RX_PIN, INPUT);
  pinMode(REPLAY_TX_PIN, INPUT);
  mySwitch.enableReceive(REPLAY_RX_PIN);
  mySwitch.resetAvailable();
  s_replayRxArmed = true;
}

static void replayDisarmReceive() {
  if (!s_replayRxArmed) {
    return;
  }
  mySwitch.disableReceive();
  s_replayRxArmed = false;
}

uint32_t receivedValue = 0;
uint16_t receivedBitLength = 0;
uint16_t receivedProtocol = 0;
const int rssi_threshold = -75;

static const uint32_t subghz_frequency_list[] = {
    300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
    318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
    434420000, 434775000, 438900000, 868350000, 915000000, 925000000
};

uint16_t currentFrequencyIndex = 0;
int yshift = 20;

static bool autoScanEnabled = false;
static uint16_t scanIndex = 0;
static uint32_t lastHopMs = 0;
static uint32_t lockUntilMs = 0;
static constexpr uint32_t SCAN_DWELL_MS = 220;
static constexpr uint32_t SCAN_DWELL_LOW_MS = 360;
static constexpr uint32_t SCAN_SETTLE_MS = 70;
static constexpr uint32_t SCAN_SETTLE_LOW_MS = 95;
static constexpr uint32_t SCAN_SETTLE_BAND_MS = 140;
static constexpr uint32_t LOCK_HOLD_MS  = 2500;
static constexpr uint32_t RSSI_LOCK_MS  = 1200;
static constexpr int      RSSI_DETECT_THRESHOLD = -58;
static constexpr int      RSSI_DETECT_THRESHOLD_LOW = -70;
static constexpr int      RSSI_CLEAR_THRESHOLD  = -66;
static constexpr int      RSSI_CLEAR_THRESHOLD_LOW = -76;
static constexpr int      RSSI_DECODE_THRESHOLD = -55;
static constexpr int      RSSI_DECODE_THRESHOLD_LOW = -66;
static constexpr uint32_t RSSI_SAMPLE_MS = 45;
static constexpr uint8_t  RSSI_DETECT_HITS = 3;
static constexpr uint8_t  RSSI_DETECT_HITS_LOW = 2;
static constexpr uint32_t DECODE_MIN_DWELL_MS = 130;
static constexpr uint32_t DECODE_MIN_DWELL_LOW_MS = 180;
static constexpr uint32_t UI_SCAN_UPDATE_MS = 250;
static uint32_t lastUiScanUpdateMs = 0;
static uint32_t scanSettledAtMs = 0;
static uint32_t lastRssiSampleMs = 0;
static uint8_t  rssiDetectStreak = 0;
static bool     rssiHot = false;

static uint32_t lastDetectAlertMs = 0;
static uint16_t lastDetectAlertFreq = 0xFFFF;
static uint32_t notifHideAtMs = 0;
static bool notifActive = false;

static constexpr uint8_t BUZZER_LEDC_CH = 7;
static bool buzzerArmed = false;
static uint32_t buzzerOffAtMs = 0;
static void replayBeep(uint16_t hz = 2200, uint16_t ms = 60) {
  #ifdef BUZZER_PIN
  ledcSetup(BUZZER_LEDC_CH, 4000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CH);
  ledcWriteTone(BUZZER_LEDC_CH, hz);
  buzzerArmed = true;
  buzzerOffAtMs = millis() + ms;
  #endif
}

static void replayBeepPoll() {
  #ifdef BUZZER_PIN
  if (!buzzerArmed) return;
  if ((int32_t)(millis() - buzzerOffAtMs) < 0) return;
  ledcWriteTone(BUZZER_LEDC_CH, 0);

  ledcDetachPin(BUZZER_PIN);
  buzzerArmed = false;
  #endif
}

static void replayShowDetectNotice(const String& reason, int rssi = 0) {
  uint32_t now = millis();

  if (now - lastDetectAlertMs < 1200 && lastDetectAlertFreq == currentFrequencyIndex) return;
  lastDetectAlertMs = now;
  lastDetectAlertFreq = currentFrequencyIndex;

  char msg[96];
  float mhz = subghz_frequency_list[currentFrequencyIndex] / 1000000.0f;

  snprintf(msg, sizeof(msg), "%s @ %.2f MHz | RSSI %d", reason.c_str(), mhz, rssi);
  showNotificationActions("SubGHz Detected", msg, true);
  replayBeep(reason == "DECODE" ? 2600 : 2000, 70);
  notifActive = true;
  notifHideAtMs = 0;
}

static inline uint16_t freqCount() {
  return (uint16_t)(sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
}

static bool replayFreqIsLowBand(uint16_t idx) {
  return subghz_frequency_list[idx % freqCount()] < 350000000UL;
}

static uint8_t replayFreqBandId(uint16_t idx) {
  const uint32_t hz = subghz_frequency_list[idx % freqCount()];
  if (hz < 350000000UL) {
    return 0;
  }
  if (hz < 500000000UL) {
    return 1;
  }
  return 2;
}

static bool replayFreqBandChanged(uint16_t prevIdx, uint16_t newIdx) {
  return replayFreqBandId(prevIdx) != replayFreqBandId(newIdx);
}

static int replayRssiDetectThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DETECT_THRESHOLD_LOW
                                                    : RSSI_DETECT_THRESHOLD;
}

static int replayRssiClearThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_CLEAR_THRESHOLD_LOW
                                                    : RSSI_CLEAR_THRESHOLD;
}

static int replayRssiDecodeThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DECODE_THRESHOLD_LOW
                                                    : RSSI_DECODE_THRESHOLD;
}

static uint32_t replayScanDwellMs() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? SCAN_DWELL_LOW_MS : SCAN_DWELL_MS;
}

static uint32_t replayDecodeMinDwellMs() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? DECODE_MIN_DWELL_LOW_MS
                                                    : DECODE_MIN_DWELL_MS;
}

static void tuneToIndex(uint16_t idx, bool persist = true) {
  currentFrequencyIndex = idx % freqCount();
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
  if (persist) {
    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
    EEPROM.commit();
  }
}

static void replayClearScanLock() {
  lockUntilMs = 0;
  rssiHot = false;
  rssiDetectStreak = 0;
}

static bool replayLooksLikeRealDecode(uint32_t value, uint16_t bits, uint16_t proto) {
  if (value == 0) {
    return false;
  }
  if (bits < 8 || bits > 64) {
    return false;
  }
  if (proto < 1 || proto > 12) {
    return false;
  }
  return true;
}

static void replayScanHopTo(uint16_t idx, uint16_t fromIdx) {
  tuneToIndex(idx, false);
  mySwitch.resetAvailable();
  mySwitch.setReceiveTolerance(replayFreqIsLowBand(idx) ? 50 : 40);

  uint32_t settleMs = SCAN_SETTLE_MS;
  if (replayFreqBandChanged(fromIdx, idx)) {
    settleMs = SCAN_SETTLE_BAND_MS;
  } else if (replayFreqIsLowBand(idx)) {
    settleMs = SCAN_SETTLE_LOW_MS;
  }
  scanSettledAtMs = millis() + settleMs;
  rssiDetectStreak = 0;
  lastRssiSampleMs = 0;
}

static void replayBeginAutoScan() {
  scanIndex = currentFrequencyIndex;
  lastHopMs = 0;
  scanSettledAtMs = 0;
  lastRssiSampleMs = 0;
  replayClearScanLock();
  lastUiScanUpdateMs = 0;
  mySwitch.resetAvailable();
}

static bool replayAutoScanReadyForDecode(uint32_t now) {
  if (lastHopMs == 0 || (now - lastHopMs) < replayDecodeMinDwellMs()) {
    return false;
  }
  if (now < scanSettledAtMs) {
    return false;
  }
  return ELECHOUSE_cc1101.getRssi() > replayRssiDecodeThreshold();
}

static void replaySampleRssiForScan(uint32_t now) {
  if (now < scanSettledAtMs) {
    return;
  }
  if (lastRssiSampleMs != 0 && (now - lastRssiSampleMs) < RSSI_SAMPLE_MS) {
    return;
  }

  const int rssi = ELECHOUSE_cc1101.getRssi();
  const int detectThreshold = replayRssiDetectThreshold();
  const uint8_t detectHits = replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DETECT_HITS_LOW
                                                                        : RSSI_DETECT_HITS;
  if (rssi > detectThreshold) {
    if (rssiDetectStreak < 255) {
      rssiDetectStreak++;
    }
    if (!rssiHot && rssiDetectStreak >= detectHits) {
      rssiHot = true;
      lockUntilMs = now + RSSI_LOCK_MS;
      EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
      EEPROM.commit();
      replayShowDetectNotice("RSSI", rssi);
    }
  } else {
    rssiDetectStreak = 0;
    if (rssiHot && rssi < replayRssiClearThreshold()) {
      rssiHot = false;
    }
  }
  lastRssiSampleMs = now;
}

static void replayFreqNext() {
  autoScanEnabled = false;
  replayClearScanLock();
  tuneToIndex((uint16_t)((currentFrequencyIndex + 1) % freqCount()), true);
  updateDisplay();
}

static void replayFreqPrev() {
  autoScanEnabled = false;
  replayClearScanLock();
  tuneToIndex((uint16_t)((currentFrequencyIndex + freqCount() - 1) % freqCount()), true);
  updateDisplay();
}

static void replayToggleAuto() {
  autoScanEnabled = !autoScanEnabled;
  if (autoScanEnabled) {
    replayBeginAutoScan();
  } else {
    replayClearScanLock();
  }
  updateDisplay();
}

static void replayTrySave() {
  if (receivedValue == 0) {
    return;
  }
  autoScanEnabled = false;
  replayClearScanLock();
  saveProfile();
}

static bool s_replayStaticDrawn = false;

struct ReplayDisplayCache {
  uint16_t freqIndex = 0xFFFF;
  uint8_t modeState = 0xFF;
  uint16_t bitLength = 0xFFFF;
  int16_t rssi = -9999;
  uint16_t protocol = 0xFFFF;
  uint32_t value = 0xFFFFFFFF;
  bool valid = false;
};

static ReplayDisplayCache s_replayDisp;

static void replayInvalidateDisplay() {
  s_replayStaticDrawn = false;
  s_replayDisp = ReplayDisplayCache{};
}

static void replayRestoreStatusPanel() {
  replayInvalidateDisplay();
  updateDisplay();
}

static uint8_t replayModeState() {
  const bool locked = (autoScanEnabled && lockUntilMs != 0 &&
                       (int32_t)(millis() - lockUntilMs) < 0);
  if (locked) {
    return 2;
  }
  return autoScanEnabled ? 1 : 0;
}

static constexpr int kReplayStatusLineY = 80;
static constexpr int kReplayValueLineH = 11;

static void replayDrawStatusSeparator() {
  tft.drawFastHLine(0, kReplayStatusLineY, 240, UI_LINE);
}

static void replayDrawValueCell(int x, int y, int w, int h, const String& text, uint16_t color) {
  const int maxH = kReplayStatusLineY - y;
  if (maxH <= 0) {
    return;
  }
  const int clipH = min(h, maxH);
  tft.fillRect(x, y, w, clipH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void replayDrawStaticChrome() {
  if (s_replayStaticDrawn) {
    return;
  }

  const int bodyBottom = subghzContentBottom();
  const int infoH = min(kReplayStatusLineY - 40, bodyBottom - 40);
  if (infoH > 0) {
    tft.fillRect(0, 40, 240, infoH, TFT_BLACK);
  }
  replayDrawStatusSeparator();

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(5, 20 + yshift);
  tft.print("Freq:");
  tft.setCursor(5, 35 + yshift);
  tft.print("Bit:");
  tft.setCursor(130, 35 + yshift);
  tft.print("RSSI:");
  tft.setCursor(130, 20 + yshift);
  tft.print("Ptc:");
  tft.setCursor(5, 50 + yshift);
  tft.print("Val:");

  s_replayStaticDrawn = true;
}

void replayHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    replayFreqPrev();
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    replayFreqNext();
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    if (receivedValue != 0) {
      autoScanEnabled = false;
      replayClearScanLock();
      sendSignal();
    }
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    replayTrySave();
    subghzWaitNavRelease(BTN_DOWN);
  }
}

void updateDisplay() {
    replayDrawStaticChrome();

    const uint8_t modeState = replayModeState();
    const int16_t rssi = ELECHOUSE_cc1101.getRssi();
    char freqBuf[16];
    char modeBuf[8];
    char bitBuf[8];
    char rssiBuf[8];
    char ptcBuf[8];
    char valBuf[16];

    snprintf(freqBuf, sizeof(freqBuf), "%.2f MHz",
             subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
    if (modeState == 2) {
      snprintf(modeBuf, sizeof(modeBuf), "LOCK");
    } else {
      snprintf(modeBuf, sizeof(modeBuf), "%s", modeState == 1 ? "AUTO" : "MAN ");
    }
    snprintf(bitBuf, sizeof(bitBuf), "%d", receivedBitLength);
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", rssi);
    snprintf(ptcBuf, sizeof(ptcBuf), "%d", receivedProtocol);
    snprintf(valBuf, sizeof(valBuf), "%lu", (unsigned long)receivedValue);

    const bool fullRedraw = !s_replayDisp.valid;
    if (fullRedraw || s_replayDisp.freqIndex != currentFrequencyIndex) {
      replayDrawValueCell(50, 20 + yshift, 72, kReplayValueLineH, freqBuf, UI_WARN);
      s_replayDisp.freqIndex = currentFrequencyIndex;
    }
    if (fullRedraw || s_replayDisp.modeState != modeState) {
      replayDrawValueCell(175, 20 + yshift, 40, kReplayValueLineH, modeBuf, UI_WARN);
      s_replayDisp.modeState = modeState;
    }
    if (fullRedraw || s_replayDisp.bitLength != receivedBitLength) {
      replayDrawValueCell(50, 35 + yshift, 40, kReplayValueLineH, bitBuf, UI_WARN);
      s_replayDisp.bitLength = receivedBitLength;
    }
    if (fullRedraw || s_replayDisp.rssi != rssi) {
      replayDrawValueCell(170, 35 + yshift, 48, kReplayValueLineH, rssiBuf, UI_WARN);
      s_replayDisp.rssi = rssi;
    }
    if (fullRedraw || s_replayDisp.protocol != receivedProtocol) {
      replayDrawValueCell(170, 20 + yshift, 40, kReplayValueLineH, ptcBuf, UI_WARN);
      s_replayDisp.protocol = receivedProtocol;
    }
    if (fullRedraw || s_replayDisp.value != receivedValue) {
      replayDrawValueCell(50, 50 + yshift, 180, kReplayValueLineH, valBuf, UI_WARN);
      s_replayDisp.value = receivedValue;
    }

    replayDrawStatusSeparator();

    s_replayDisp.valid = true;
    /* Do NOT idle/retune here — that drops RCSwitch pulse timing mid-receive. */
}

String getUserInputName() {
  OnScreenKeyboardConfig cfg;
  cfg.titleLine1     = "[!] Set a name for the saved profile.";
  cfg.titleLine2     = "(max 15 chars, ^ caps, # sym)";
  osKeyboardUseStandardLayout(cfg);
  cfg.maxLen         = MAX_NAME_LENGTH - 1;
  cfg.shuffleNames   = randomNames;
  cfg.shuffleCount   = numRandomNames;
  cfg.buttonsY       = 195;
  cfg.backLabel      = "Back";
  cfg.middleLabel    = "Shuffle";
  cfg.okLabel        = "OK";
  cfg.enableShuffle  = true;
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg  = "Name cannot be empty!";

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");

  if (!r.accepted) {

    subghzClearBody(TFT_BLACK);
    uiDrawn = false;
    replayRestoreStatusPanel();
    runUI();
    subghzRedrawNavChrome();
  }
  return r.text;
}

void sendSignal() {

    replayDisarmReceive();
    delay(100);
    pinMode(REPLAY_TX_PIN, OUTPUT);
    mySwitch.enableTransmit(REPLAY_TX_PIN);
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, 240, kReplayStatusLineY - 40, TFT_BLACK);

    tft.setCursor(10, 30 + yshift);
    tft.print("Sending...");
    tft.setCursor(10, 40 + yshift);
    tft.print(receivedValue);

    mySwitch.setProtocol(receivedProtocol);
    mySwitch.send(receivedValue, receivedBitLength);

    delay(500);
    tft.fillRect(0, 40, 240, kReplayStatusLineY - 40, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    mySwitch.disableTransmit();
    pinMode(REPLAY_TX_PIN, INPUT);
    pinMode(REPLAY_RX_PIN, INPUT);
    ELECHOUSE_cc1101.SetRx();
    delay(50);
    replayArmReceive();

    delay(500);
    replayRestoreStatusPanel();
}

void do_sampling() {
  constexpr unsigned int kGraphYOffset = 81;
  const int plotY = (int)epochSUB + (int)kGraphYOffset;
  if (plotY >= subghzContentBottom()) {
    return;
  }

  micro_s = micros();

  #define ALPHA 0.2
  float ewmaRSSI = -50;

for (int i = 0; i < samplesSUB; i++) {
    int rssi = ELECHOUSE_cc1101.getRssi();
    rssi += 100;

    ewmaRSSI = (ALPHA * rssi) + ((1 - ALPHA) * ewmaRSSI);

    vRealSUB[i] = ewmaRSSI * 2;
    vImagSUB[i] = 1;

    while (micros() < micro_s + sampling_period);
    micro_s += sampling_period;
}

  double mean = 0;

  for (uint16_t i = 0; i < samplesSUB; i++)
        mean += vRealSUB[i];
        mean /= samplesSUB;
  for (uint16_t i = 0; i < samplesSUB; i++)
        vRealSUB[i] -= mean;

  micro_s = micros();

  FFTSUB.Windowing(vRealSUB, samplesSUB, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFTSUB.Compute(vRealSUB, vImagSUB, samplesSUB, FFT_FORWARD);
  FFTSUB.ComplexToMagnitude(vRealSUB, vImagSUB, samplesSUB);

unsigned int left_x = 120;
unsigned int graph_y_offset = kGraphYOffset;
int max_k = 0;

for (int j = 0; j < samplesSUB >> 1; j++) {
    int k = vRealSUB[j] / attenuation_num;
    if (k > max_k)
        max_k = k;
    if (k > 127) k = 127;

    unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
    unsigned int vertical_x = left_x + j;

    tft.drawPixel(vertical_x, epochSUB + graph_y_offset, color);
}

for (int j = 0; j < samplesSUB >> 1; j++) {
    int k = vRealSUB[j] / attenuation_num;
    if (k > max_k)
        max_k = k;
    if (k > 127) k = 127;

    unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
    unsigned int mirrored_x = left_x - j;
    tft.drawPixel(mirrored_x, epochSUB + graph_y_offset, color);
}

  double tattenuation = max_k / 127.0;

  if (tattenuation > attenuation_num)
    attenuation_num = tattenuation;

    delay(10);
}

void readProfileCount() {
    EEPROM.get(ADDR_PROFILE_COUNT, profileCount);
    if (profileCount > MAX_PROFILES) profileCount = 0;
}

void saveProfile() {
    readProfileCount();

    if (profileCount >= MAX_PROFILES) {

        String err, outPath;
        if (exportProfilesToSD(outPath, &err)) {
            clearProfilesInEeprom();
            profileCount = 0;

            syncCurrentProfilesToSD(nullptr);
        } else {
            subghzClearBody(TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(10, 30 + yshift);
            tft.setTextColor(UI_WARN, TFT_BLACK);
            tft.print("Storage full!");
            tft.setCursor(10, 45 + yshift);
            tft.setTextColor(UI_TEXT, TFT_BLACK);
            tft.print("Insert SD / export fail");
            tft.setCursor(10, 60 + yshift);
            tft.print(err);
            uiDrawn = false;
            runUI();
            subghzRedrawNavChrome();
            if (!subghzWaitWithNav(2000)) {
              return;
            }
            replayRestoreStatusPanel();
            float currentBatteryVoltage = readBatteryVoltage();
            drawStatusBar(currentBatteryVoltage, true);
            uiDrawn = false;
            runUI();
            subghzRedrawNavChrome();
            return;
        }
    }

    if (profileCount < MAX_PROFILES) {

        String customName = getUserInputName();

        tft.setTextSize(1);

        Profile newProfile;
        newProfile.frequency = subghz_frequency_list[currentFrequencyIndex];
        newProfile.value = (uint32_t)receivedValue;
        newProfile.bitLength = (uint16_t)receivedBitLength;
        newProfile.protocol = (uint16_t)receivedProtocol;
        strncpy(newProfile.name, customName.c_str(), MAX_NAME_LENGTH - 1);
        newProfile.name[MAX_NAME_LENGTH - 1] = '\0';

        int addr = ADDR_PROFILE_START + (profileCount * PROFILE_SIZE);
        EEPROM.put(addr, newProfile);
        EEPROM.commit();

        profileCount++;

        EEPROM.put(ADDR_PROFILE_COUNT, profileCount);
        EEPROM.commit();

        syncCurrentProfilesToSD(nullptr);

        subghzClearBody(TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Profile saved!");
        tft.setCursor(10, 40 + yshift);
        tft.print("Name: ");
        tft.print(newProfile.name);
        tft.setCursor(10, 50 + yshift);
        tft.print("Profiles saved: ");
        tft.println(profileCount);

    } else {
        subghzClearBody(TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, 30 + yshift);
        tft.setTextColor(UI_TEXT, TFT_BLACK);
        tft.print("Profile storage full!");
    }

    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
    if (!subghzWaitWithNav(2000)) {
      return;
    }
    replayRestoreStatusPanel();
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
}

void loadProfileCount() {

    readProfileCount();
}

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 6

    static int iconX[ICON_NUM] = {90, 130, 170, 210, 50, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_sort_down_minus,
        bitmap_icon_antenna,
        bitmap_icon_floppy,
        bitmap_icon_random,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, 240, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, 240, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                    autoScanEnabled = false;
                    currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    tuneToIndex(currentFrequencyIndex, true);
                    updateDisplay();
                    break;
                case 1:
                    autoScanEnabled = false;
                    currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    tuneToIndex(currentFrequencyIndex, true);
                    updateDisplay();
                    break;
                case 2:
                    sendSignal();
                    break;
                case 3:
                    saveProfile();
                    break;
                case 4:
                    autoScanEnabled = !autoScanEnabled;
                    if (autoScanEnabled) {
                      replayBeginAutoScan();
                    } else {
                      replayClearScanLock();
                    }
                    updateDisplay();
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
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 5) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
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

void ReplayAttackSetup() {
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  subghzSetReplayNavLabels();

  replayDisarmReceive();
  mySwitch.resetAvailable();

  reclaimSharedSpiBus();
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

  EEPROM.begin(EEPROM_SIZE);
  readProfileCount();

  EEPROM.get(ADDR_VALUE, receivedValue);
  EEPROM.get(ADDR_BITLEN, receivedBitLength);
  EEPROM.get(ADDR_PROTO, receivedProtocol);
  EEPROM.get(ADDR_FREQ, currentFrequencyIndex);

  const uint16_t freqCount = (uint16_t)(sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
  if (currentFrequencyIndex >= freqCount) currentFrequencyIndex = 0;

  autoScanEnabled = false;
  replayClearScanLock();

  subghzClearBody(TFT_BLACK);
  tft.setRotation(TFT_ROTATION);

  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();
  setupTouchscreen();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  sampling_period = round(1000000*(1.0/FrequencySUB));

  for (int i = 0; i < 32; i++) {
    red[i] = i / 2;
    green[i] = 0;
    blue[i] = i;
  }
  for (int i = 32; i < 64; i++) {
    red[i] = i / 2;
    green[i] = 0;
    blue[i] = 63 - i;
  }
#if ESP32DIV_FFT_PALETTE_SIZE > 64
  for (int i = 64; i < 96; i++) {
    red[i] = 31;
    green[i] = (i - 64) * 2;
    blue[i] = 0;
  }
  for (int i = 96; i < 128; i++) {
    red[i] = 31;
    green[i] = 63;
    blue[i] = i - 96;
  }
#endif

  replayInvalidateDisplay();
  updateDisplay();
  uiDrawn = false;
  subghzRedrawNavChrome();

  /* Bring radio up after UI/SPI activity so first entry RX matches re-entry. */
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(500.0);

  pinMode(REPLAY_RX_PIN, INPUT);
  pinMode(REPLAY_TX_PIN, INPUT);

  tuneToIndex(currentFrequencyIndex, false);
  mySwitch.setReceiveTolerance(replayFreqIsLowBand(currentFrequencyIndex) ? 50 : 40);
  mySwitch.setRepeatTransmit(8);

  delay(50);
  replayArmReceive();
}

void ReplayAttackLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        replayDisarmReceive();
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    if (uiDrawn) {
      tft.drawFastHLine(0, 19, 240, UI_LINE);
      tft.drawFastHLine(0, 36, 240, UI_LINE);
      if (s_replayDisp.valid) {
        replayDrawStatusSeparator();
      }
    }
    replayHandleNavButtons();

    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;
    const bool leftPressed  = isPhysicalButtonPressed(BTN_LEFT);
    const bool rightPressed = isPhysicalButtonPressed(BTN_RIGHT);
    const bool upPressed    = isPhysicalButtonPressed(BTN_UP);
    const bool downPressed  = isPhysicalButtonPressed(BTN_DOWN);

    replayBeepPoll();

    if (notifActive && isNotificationVisible()) {
      int x, y;
      if (readTouchXY(x, y)) {
        NotificationAction act = notificationHandleTouch(x, y);
        if (act == NotificationAction::Save) {
          notifActive = false;

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          float v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();

          autoScanEnabled = false;
          saveProfile();

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();
        } else if (act == NotificationAction::Ok || act == NotificationAction::Close) {
          notifActive = false;

          lastDetectAlertMs = millis();
          lastDetectAlertFreq = currentFrequencyIndex;
          lockUntilMs = millis() + 1500;
          rssiHot = true;

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          float v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();
        }
      }

      return;
    } else if (notifActive && !isNotificationVisible()) {

      notifActive = false;
      subghzClearBody(TFT_BLACK);
      uiDrawn = false;
      replayInvalidateDisplay();
      float v = readBatteryVoltage();
      drawStatusBar(v, true);
      runUI();
      updateDisplay();
      subghzRedrawNavChrome();
    }

    if (rightPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
        replayFreqNext();
        lastDebounceTime = millis();
    }
    if (leftPressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
        replayFreqPrev();
        lastDebounceTime = millis();
    }
    if (upPressed && !prevUp && receivedValue != 0 && millis() - lastDebounceTime > debounceDelay) {
        autoScanEnabled = false;
        replayClearScanLock();
        sendSignal();
        lastDebounceTime = millis();
    }
    if (downPressed && !prevDown && millis() - lastDebounceTime > debounceDelay) {
        replayTrySave();
        lastDebounceTime = millis();
    }

    prevLeft = leftPressed;
    prevRight = rightPressed;
    prevUp = upPressed;
    prevDown = downPressed;

    if (autoScanEnabled) {
      const uint32_t now = millis();
      const bool scanLocked = (lockUntilMs != 0 && (int32_t)(now - lockUntilMs) < 0);

      if (!scanLocked &&
          (lastHopMs == 0 || (now - lastHopMs) >= replayScanDwellMs())) {
        const uint16_t fromIdx = currentFrequencyIndex;
        scanIndex = (uint16_t)((scanIndex + 1) % freqCount());
        replayScanHopTo(scanIndex, fromIdx);
        lastHopMs = now;
        rssiHot = false;
      }

      replaySampleRssiForScan(now);

      if (lastUiScanUpdateMs == 0 || (now - lastUiScanUpdateMs) >= UI_SCAN_UPDATE_MS) {
        updateDisplay();
        lastUiScanUpdateMs = now;
      }
    }

    if (!autoScanEnabled) {
      do_sampling();
    }
    delay(10);
    epochSUB++;

    if (epochSUB >= tft.width())
      epochSUB = 0;

    if (mySwitch.available()) {
        const uint32_t val = mySwitch.getReceivedValue();
        const uint16_t bits = mySwitch.getReceivedBitlength();
        const uint16_t proto = mySwitch.getReceivedProtocol();
        mySwitch.resetAvailable();

        const uint32_t now = millis();
        const bool validDecode = replayLooksLikeRealDecode(val, bits, proto) &&
            (!autoScanEnabled || replayAutoScanReadyForDecode(now));

        if (validDecode) {
          receivedValue = val;
          receivedBitLength = bits;
          receivedProtocol = proto;

          EEPROM.put(ADDR_VALUE, receivedValue);
          EEPROM.put(ADDR_BITLEN, receivedBitLength);
          EEPROM.put(ADDR_PROTO, receivedProtocol);
          EEPROM.commit();

          updateDisplay();

          if (autoScanEnabled) {
            lockUntilMs = now + LOCK_HOLD_MS;
            scanIndex = currentFrequencyIndex;
            rssiHot = false;
            rssiDetectStreak = 0;

            EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
            EEPROM.commit();
            replayShowDetectNotice("DECODE", ELECHOUSE_cc1101.getRssi());
          }
        }
    }

  }
}

namespace SavedProfile {

void updateDisplay();
void runUI();
void transmitProfile(int index);
void deleteProfile(int index);

static bool uiDrawn = false;

#define EEPROM_SIZE 1440

#define ADDR_PROFILE_COUNT 1296
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES       5
#define MAX_NAME_LENGTH    16

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

RCSwitch mySwitch = RCSwitch();
struct __attribute__((packed)) Profile {
    uint32_t frequency;
    uint32_t value;
    uint16_t bitLength;
    uint16_t protocol;
    char name[MAX_NAME_LENGTH];
};

#define PROFILE_SIZE sizeof(Profile)

uint16_t profileCount = 0;
uint16_t currentProfileIndex = 0;

int yshift = 16;

static std::vector<SubGhzFileEntry> sdFiles;
static uint16_t sdTotalProfiles = 0;
static String sdLastErr = "";
static String selectedPath = "";
static uint16_t selectedLocalIdx = 0;
static Profile selectedProfile{};
static bool selectedValid = false;

static constexpr uint8_t ITEMS_PER_PAGE = 7;
static constexpr int PROFILE_PAD_X = 10;
static constexpr int LIST_X = PROFILE_PAD_X;
static constexpr int LIST_W = 220;

static constexpr int PROFILE_HEADER_Y = 50;
static constexpr int PROFILE_HEADER_H = 14;
static constexpr int LIST_Y = PROFILE_HEADER_Y + PROFILE_HEADER_H + 2;
static constexpr int ROW_H  = 18;
static constexpr int PROFILE_LINE_H = 14;
static constexpr int PROFILE_LINE_GAP = 3;
static constexpr int PROFILE_LINE_STEP = PROFILE_LINE_H + PROFILE_LINE_GAP;
static constexpr int PROFILE_INFO_LINES = 4;
static constexpr int PROFILE_INFO_CONTENT_H =
    PROFILE_LINE_STEP * (PROFILE_INFO_LINES - 1) + PROFILE_LINE_H;
static constexpr int PROFILE_LABEL_X = PROFILE_PAD_X;
static constexpr int PROFILE_VALUE_X = 50;
static constexpr int PROFILE_COL2_LABEL_X = 130;
static constexpr int PROFILE_COL2_VALUE_X = 165;

static constexpr int UI_GAP_Y = 6;

static int profileBottomY() {
  return subghzContentBottom();
}

static int profileListBottom() {
  return LIST_Y + (ITEMS_PER_PAGE * ROW_H);
}

static int profileDetailsY() {
  const int areaTop = profileListBottom();
  const int areaBottom = profileBottomY();
  const int areaH = areaBottom - areaTop;
  if (areaH <= PROFILE_INFO_CONTENT_H) {
    return areaTop + UI_GAP_Y;
  }
  return areaTop + (areaH - PROFILE_INFO_CONTENT_H) / 2;
}

static void profileClearContentArea(uint16_t color = TFT_BLACK) {
  const int top = 40;
  const int h = subghzContentBottom() - top;
  if (h > 0) {
    tft.fillRect(0, top, 240, h, color);
  }
}

static void profileRestoreChrome() {
  drawStatusBar(readBatteryVoltage(), true);
  uiDrawn = false;
  updateDisplay();
  runUI();
  subghzRedrawNavChrome();
}

static void updateSelectionUI(uint16_t oldIndex, bool forceListRedraw = false);

static uint16_t cachedPageStart = 0xFFFF;
static SubGhzProfile cachedPage[ITEMS_PER_PAGE]{};
static bool cachedOk[ITEMS_PER_PAGE]{};
static bool cacheDirty = true;

static bool deleteArmed = false;
static uint32_t deleteArmUntilMs = 0;

static void refreshSdIndex(bool keepSelection = true) {
    uint16_t oldIdx = currentProfileIndex;
    String err;
    if (!listAllProfileFiles(sdFiles, &err)) {
        sdFiles.clear();
        sdTotalProfiles = 0;
        currentProfileIndex = 0;
        selectedValid = false;
        sdLastErr = err;
        cacheDirty = true;
        return;
    }
    sdLastErr = "";
    sdTotalProfiles = totalProfilesInIndex(sdFiles);
    if (sdTotalProfiles == 0) {
        currentProfileIndex = 0;
        selectedValid = false;
        sdLastErr = "No profiles found";
        cacheDirty = true;
        return;
    }
    if (keepSelection) currentProfileIndex = oldIdx;
    if (currentProfileIndex >= sdTotalProfiles) currentProfileIndex = (uint16_t)(sdTotalProfiles - 1);
    selectedValid = false;
    cacheDirty = true;
}

static bool loadSelectedFromSd(String* errOut = nullptr) {
    if (sdTotalProfiles == 0) { selectedValid = false; return false; }
    if (!locateGlobalIndex(sdFiles, currentProfileIndex, selectedPath, selectedLocalIdx)) {
        selectedValid = false; if (errOut) *errOut="Locate failed"; return false;
    }
    SubGhzProfile p{};
    if (!readProfileAt(selectedPath, selectedLocalIdx, p, errOut)) {
        selectedValid = false; return false;
    }

    selectedProfile.frequency = p.frequency;
    selectedProfile.value = p.value;
    selectedProfile.bitLength = p.bitLength;
    selectedProfile.protocol = p.protocol;
    memcpy(selectedProfile.name, p.name, MAX_NAME_LENGTH);
    selectedProfile.name[MAX_NAME_LENGTH - 1] = '\0';
    selectedValid = true;
    return true;
}

static uint16_t pageStartForIndex(uint16_t idx) {
  return (uint16_t)((idx / ITEMS_PER_PAGE) * ITEMS_PER_PAGE);
}

static void ensurePageCache() {
  if (sdTotalProfiles == 0) return;
  uint16_t start = pageStartForIndex(currentProfileIndex);
  if (!cacheDirty && cachedPageStart == start) return;
  cachedPageStart = start;
  for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
    cachedOk[i] = false;
    uint16_t globalIdx = (uint16_t)(start + i);
    if (globalIdx >= sdTotalProfiles) continue;
    String pth; uint16_t li = 0;
    if (!locateGlobalIndex(sdFiles, globalIdx, pth, li)) continue;
    String err;
    cachedOk[i] = readProfileAt(pth, li, cachedPage[i], &err);
    if (!cachedOk[i]) memset(&cachedPage[i], 0, sizeof(SubGhzProfile));
  }
  cacheDirty = false;
}

static void drawHeaderLine() {
  const int hy = PROFILE_HEADER_Y;
  tft.fillRect(LIST_X, hy, LIST_W, PROFILE_HEADER_H, TFT_BLACK);
  tft.setTextColor(UI_WARN, TFT_BLACK);
  tft.setCursor(LIST_X, hy);
  tft.printf("Profile %d/%d", (int)currentProfileIndex + 1, (int)sdTotalProfiles);
}

static void profileSelectNext() {
  if (sdTotalProfiles == 0) {
    return;
  }
  uint16_t oldIdx = currentProfileIndex;
  currentProfileIndex = (uint16_t)((currentProfileIndex + 1) % sdTotalProfiles);
  selectedValid = false;
  updateSelectionUI(oldIdx, false);
}

static void profileSelectPrev() {
  if (sdTotalProfiles == 0) {
    return;
  }
  uint16_t oldIdx = currentProfileIndex;
  currentProfileIndex = (uint16_t)((currentProfileIndex + sdTotalProfiles - 1) % sdTotalProfiles);
  selectedValid = false;
  updateSelectionUI(oldIdx, false);
}

static void profileRefreshSd() {
  refreshSdIndex(true);
  selectedValid = false;
  cacheDirty = true;
  deleteArmed = false;
  updateDisplay();
}

void profileHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    profileSelectPrev();
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    profileSelectNext();
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    if (sdTotalProfiles > 0) {
      transmitProfile(currentProfileIndex);
    }
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    if (sdTotalProfiles > 0) {
      deleteProfile(currentProfileIndex);
    }
    subghzWaitNavRelease(BTN_LEFT);
  }
}

static void drawRow(uint16_t pageStart, uint8_t row) {
  uint16_t globalIdx = (uint16_t)(pageStart + row);
  if (globalIdx >= sdTotalProfiles) return;

  bool isSel = (globalIdx == currentProfileIndex);
  int y = LIST_Y + (row * ROW_H);

  uint16_t bg = isSel ? DARK_GRAY : TFT_BLACK;
  uint16_t fg = isSel ? UI_WARN : UI_DIM_TEXT;
  tft.fillRect(LIST_X, y, LIST_W, ROW_H - 1, bg);
  tft.setTextColor(fg, bg);
  tft.setCursor(LIST_X + 2, y + 4);
  tft.printf("%2d.", (int)globalIdx + 1);
  tft.setCursor(LIST_X + 34, y + 4);

  if (cachedOk[row]) {

    char nameBuf[17];
    memcpy(nameBuf, cachedPage[row].name, 16);
    nameBuf[16] = '\0';
    String nm = String(nameBuf);
    if (nm.length() > 10) nm = nm.substring(0, 10);
    tft.print(nm);

    char fbuf[16];
    snprintf(fbuf, sizeof(fbuf), "%.2f", cachedPage[row].frequency / 1000000.0);
    int tw = tft.textWidth(fbuf, 1);
    tft.setCursor(LIST_X + LIST_W - 4 - tw, y + 4);
    tft.print(fbuf);
  } else {
    tft.print("<?>");
  }
}

static void drawListPage(uint16_t pageStart) {
  ensurePageCache();

  tft.fillRect(LIST_X, LIST_Y, LIST_W, (ITEMS_PER_PAGE * ROW_H), TFT_BLACK);
  for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
    if ((uint16_t)(pageStart + row) >= sdTotalProfiles) break;
    drawRow(pageStart, row);
  }
}

static void drawDetails() {
  const int detailsY = profileDetailsY();
  const int gapTop = profileListBottom();
  const int gapH = profileBottomY() - gapTop;
  if (gapH > 0) {
    tft.fillRect(LIST_X, gapTop, LIST_W, gapH, TFT_BLACK);
  }
  tft.drawFastHLine(LIST_X, profileListBottom(), LIST_W, UI_LINE);

  String err;
  if (!selectedValid) {
    loadSelectedFromSd(&err);
  }

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  if (!selectedValid) {
    tft.setCursor(PROFILE_LABEL_X, detailsY);
    tft.print("Read failed:");
    tft.setCursor(PROFILE_VALUE_X, detailsY);
    tft.print(err);
    return;
  }

  tft.setCursor(PROFILE_LABEL_X, detailsY);
  tft.print("Name:");
  tft.setCursor(PROFILE_VALUE_X, detailsY);
  tft.print(selectedProfile.name);

  tft.setCursor(PROFILE_LABEL_X, detailsY + PROFILE_LINE_STEP);
  tft.print("Freq:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + PROFILE_LINE_STEP);
  tft.printf("%.2f MHz", selectedProfile.frequency / 1000000.0);
  tft.setCursor(PROFILE_COL2_LABEL_X, detailsY + PROFILE_LINE_STEP);
  tft.print("Ptc:");
  tft.setCursor(PROFILE_COL2_VALUE_X, detailsY + PROFILE_LINE_STEP);
  tft.print(selectedProfile.protocol);

  tft.setCursor(PROFILE_LABEL_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print("Val:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print((unsigned long)selectedProfile.value);
  tft.setCursor(PROFILE_COL2_LABEL_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print("Bit:");
  tft.setCursor(PROFILE_COL2_VALUE_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print(selectedProfile.bitLength);

  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(PROFILE_LABEL_X, detailsY + (PROFILE_LINE_STEP * 3));
  tft.print("SRC:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + (PROFILE_LINE_STEP * 3));
  if (selectedPath.endsWith("profiles_current.bin")) {
    tft.print("current");
  } else {
    const int slash = selectedPath.lastIndexOf('/');
    tft.print(slash >= 0 ? selectedPath.substring(slash + 1) : selectedPath);
  }

  if (deleteArmed && (int32_t)(millis() - deleteArmUntilMs) < 0) {
    int hintY = detailsY + (PROFILE_LINE_STEP * 4);
    if (hintY >= profileBottomY() - 12) {
      hintY = profileBottomY() - 12;
    }
    tft.setCursor(PROFILE_LABEL_X, hintY);
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.print("Press Delete again to confirm");
  }
}

static void updateSelectionUI(uint16_t oldIndex, bool forceListRedraw) {
  if (sdTotalProfiles == 0) return;
  uint16_t oldPage = pageStartForIndex(oldIndex);
  uint16_t newPage = pageStartForIndex(currentProfileIndex);

  tft.startWrite();
  drawHeaderLine();

  if (forceListRedraw || oldPage != newPage) {
    drawListPage(newPage);
  } else {

    uint8_t oldRow = (uint8_t)(oldIndex - oldPage);
    uint8_t newRow = (uint8_t)(currentProfileIndex - newPage);
    ensurePageCache();

    drawRow(newPage, oldRow);
    drawRow(newPage, newRow);
  }

  drawDetails();
  tft.endWrite();
}

void updateDisplay() {

    tft.startWrite();
    const int bodyH = subghzContentBottom() - 40;
    if (bodyH > 0) {
      tft.fillRect(0, 40, 240, bodyH, TFT_BLACK);
    }

    if (sdTotalProfiles == 0) {
        tft.setTextSize(1);
        tft.setCursor(PROFILE_LABEL_X, PROFILE_HEADER_Y + PROFILE_LINE_H);
        tft.setTextColor(UI_TEXT, TFT_BLACK);
        if (sdLastErr.indexOf("SD not mounted") >= 0) {
          tft.print("SD card not inserted.");
        } else {
          tft.print("No profiles on SD.");
        }
        if (sdLastErr.length()) {
          tft.setCursor(PROFILE_LABEL_X, PROFILE_HEADER_Y + (PROFILE_LINE_H * 2));
          tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
          tft.print(sdLastErr);
        }
        tft.endWrite();
        return;
    }

    drawHeaderLine();
    drawListPage(pageStartForIndex(currentProfileIndex));
    drawDetails();
    tft.endWrite();
}

void transmitProfile(int index) {
    (void)index;
    String err;
    loadSelectedFromSd(&err);
    if (!selectedValid) return;
    Profile profileToSend = selectedProfile;

    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(profileToSend.frequency / 1000000.0);

    mySwitch.disableReceive();
    delay(100);
    pinMode(SUBGHZ_TX_PIN, OUTPUT);
    mySwitch.enableTransmit(SUBGHZ_TX_PIN);
    ELECHOUSE_cc1101.SetTx();

    profileClearContentArea(TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.print("Sending ");
    tft.print(profileToSend.name);
    tft.print("...");
    tft.setCursor(10, 50 + yshift);
    tft.print("Value: ");
    tft.print(profileToSend.value);

    mySwitch.setProtocol(profileToSend.protocol);
    mySwitch.send(profileToSend.value, profileToSend.bitLength);

    delay(500);
    profileClearContentArea(TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    mySwitch.disableTransmit();
    pinMode(SUBGHZ_TX_PIN, INPUT);
    pinMode(SUBGHZ_RX_PIN, INPUT);
    ELECHOUSE_cc1101.SetRx();
    delay(50);
    mySwitch.enableReceive(SUBGHZ_RX_PIN);

    delay(500);
    profileRestoreChrome();
}

void loadProfileCount() {

    refreshSdIndex(true);
}

void printProfiles() {
    refreshSdIndex(false);
}

void deleteProfile(int index) {
    (void)index;
    if (sdTotalProfiles == 0) return;
    String err;
    loadSelectedFromSd(&err);
    if (!selectedValid) return;

    String path = selectedPath;
    uint16_t local = selectedLocalIdx;

    uint32_t now = millis();
    if (!deleteArmed || (int32_t)(now - deleteArmUntilMs) >= 0) {
      deleteArmed = true;
      deleteArmUntilMs = now + 3000;
      updateDisplay();
      return;
    }
    deleteArmed = false;

    if (!deleteProfileFromFile(path, local, &err)) {
      profileClearContentArea(TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.setTextColor(UI_WARN);
      tft.print("Delete FAILED");
      tft.setCursor(10, 45 + yshift);
      tft.setTextColor(TFT_WHITE);
      tft.print(err);
      delay(1200);
      profileRestoreChrome();
      return;
    }

    refreshSdIndex(false);
    if (sdTotalProfiles == 0) currentProfileIndex = 0;
    else if (currentProfileIndex >= sdTotalProfiles) currentProfileIndex = (uint16_t)(sdTotalProfiles - 1);
    selectedValid = false;
    cacheDirty = true;
    updateDisplay();
}

void runUI() {
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 4

    static int iconX[ICON_NUM] = {130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_antenna,
        bitmap_icon_recycle,
        bitmap_icon_undo,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, 240, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, 240, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                    if (sdTotalProfiles > 0) {
                        transmitProfile(currentProfileIndex);
                    }
                    break;
                case 1:
                    if (sdTotalProfiles > 0) {
                        deleteProfile(currentProfileIndex);
                    }
                    break;
                case 2: {
                    refreshSdIndex(true);
                    selectedValid = false;
                    cacheDirty = true;
                    deleteArmed = false;
                    updateDisplay();
                    break;
                }
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
              uint16_t oldIdx = currentProfileIndex;
              uint16_t start = pageStartForIndex(currentProfileIndex);
              uint16_t idx = (uint16_t)(start + row);
              if (idx < sdTotalProfiles) {
                currentProfileIndex = idx;
                selectedValid = false;
                cacheDirty = true;
                deleteArmed = false;
                updateSelectionUI(oldIdx, false);
              }
            }
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 3) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
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

void saveSetup() {
    Serial.begin(115200);
    setTouchButtonInputEnabled(true);
    subghzSetProfileNavLabels();

    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    EEPROM.begin(EEPROM_SIZE);
    loadProfileCount();
    printProfiles();

#if HAS_PCF8574_BUTTONS
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

    subghzClearBody(TFT_BLACK);
    tft.setTextColor(UI_TEXT);

    setupTouchscreen();

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    subghzRedrawNavChrome();
    uiDrawn = false;

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setCCMode(0);
    ELECHOUSE_cc1101.setModulation(2);
    pinMode(SUBGHZ_RX_PIN, INPUT);
    pinMode(SUBGHZ_TX_PIN, INPUT);
    ELECHOUSE_cc1101.SetRx();

    mySwitch.enableReceive(SUBGHZ_RX_PIN);
    mySwitch.setRepeatTransmit(8);

    refreshSdIndex(false);
    cacheDirty = true;
    deleteArmed = false;
    updateDisplay();
    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
}

void saveLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    profileHandleNavButtons();

    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    static bool prevUp = false;
    static bool prevDown = false;
    static bool prevRight = false;
    static bool prevLeft = false;
    const bool prevPressed    = isPhysicalButtonPressed(BTN_UP);
    const bool nextPressed    = isPhysicalButtonPressed(BTN_DOWN);
    const bool txPressed      = isPhysicalButtonPressed(BTN_RIGHT);
    const bool deletePressed = isPhysicalButtonPressed(BTN_LEFT);

    if (sdTotalProfiles > 0) {

        if (nextPressed && !prevDown && millis() - lastDebounceTime > debounceDelay) {
            profileSelectNext();
            lastDebounceTime = millis();
        }

        if (prevPressed && !prevUp && millis() - lastDebounceTime > debounceDelay) {
            profileSelectPrev();
            lastDebounceTime = millis();
        }

        if (txPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
            transmitProfile(currentProfileIndex);
            lastDebounceTime = millis();
        }

        if (deletePressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
            deleteProfile(currentProfileIndex);
            lastDebounceTime = millis();
        }
    }

    prevUp = prevPressed;
    prevDown = nextPressed;
    prevRight = txPressed;
    prevLeft = deletePressed;
}

}

namespace subjammer {

void updateDisplay();

static bool uiDrawn = false;

static unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 64

static constexpr uint8_t JAM_BTN_LEFT  = 4;
static constexpr uint8_t JAM_BTN_RIGHT = 5;
static constexpr uint8_t JAM_BTN_DOWN  = 3;
static constexpr uint8_t JAM_BTN_UP    = 6;

bool jammingRunning = false;
bool continuousMode = true;
bool autoMode = false;
unsigned long lastSweepTime = 0;
const unsigned long sweepInterval = 1000;

static const uint32_t subghz_frequency_list[] = {
    300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
    318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
    434420000, 434775000, 438900000, 868350000, 915000000, 925000000
};
const int numFrequencies = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);
int currentFrequencyIndex = 5;
float targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;

static constexpr int kJammerStatusLineY = 79;
static constexpr int kJammerYSHIFT = 20;
static constexpr int kJammerValueLineH = 11;
static constexpr int kJammerProgressY = 60 + kJammerYSHIFT;

static bool s_jammerStaticDrawn = false;

struct JammerDisplayCache {
  bool valid = false;
  int freqMHz100 = -1;
  bool autoMode = false;
  bool continuousMode = false;
  bool jammingRunning = false;
  int progress = -1;
  bool blinkOn = false;
};

static JammerDisplayCache s_jammerDisp;

static void jammerInvalidateDisplay() {
  s_jammerStaticDrawn = false;
  s_jammerDisp = JammerDisplayCache{};
}

static void subjammerToggleJam() {
  jammingRunning = !jammingRunning;
  if (jammingRunning) {
    Serial.println("Jamming started");
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    ELECHOUSE_cc1101.SetTx();
  } else {
    Serial.println("Jamming stopped");
    ELECHOUSE_cc1101.setSidle();
    digitalWrite(TX_PIN, LOW);
  }
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerFreqNext() {
  if (autoMode) {
    return;
  }
  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerFreqPrev() {
  if (autoMode) {
    return;
  }
  currentFrequencyIndex = (currentFrequencyIndex - 1 + numFrequencies) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerApplyFrequency() {
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  if (jammingRunning) {
    ELECHOUSE_cc1101.SetTx();
  } else {
    ELECHOUSE_cc1101.setSidle();
    digitalWrite(TX_PIN, LOW);
  }
}

static void subjammerAutoSweepIfDue() {
  if (!autoMode || millis() - lastSweepTime < sweepInterval) {
    return;
  }

  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  subjammerApplyFrequency();
  updateDisplay();
  lastSweepTime = millis();
}

static void subjammerToggleAuto() {
  autoMode = !autoMode;
  Serial.print("Frequency mode: ");
  Serial.println(autoMode ? "Automatic" : "Manual");
  if (autoMode) {
    currentFrequencyIndex = 0;
    targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
    lastSweepTime = millis();
    subjammerApplyFrequency();
    s_jammerDisp.freqMHz100 = -1;
  }
  updateDisplay();
  lastDebounceTime = millis();
}

void subjammerHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    subjammerToggleJam();
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    subjammerFreqPrev();
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    subjammerFreqNext();
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    subjammerToggleAuto();
    subghzWaitNavRelease(BTN_DOWN);
  }
}

static void jammerDrawStatusSeparator() {
  tft.drawFastHLine(0, kJammerStatusLineY, 240, UI_LINE);
}

static void jammerDrawValueCell(int x, int y, int w, int h, const String& text, uint16_t color) {
  const int maxH = kJammerStatusLineY - y;
  if (maxH <= 0) {
    return;
  }
  const int clipH = min(h, maxH);
  tft.fillRect(x, y, w, clipH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void jammerDrawStaticChrome() {
  if (s_jammerStaticDrawn) {
    return;
  }

  const int bodyBottom = subghzContentBottom();
  const int bodyH = min(kJammerStatusLineY - 40, bodyBottom - 40);
  if (bodyH > 0) {
    tft.fillRect(0, 40, 240, bodyH, TFT_BLACK);
  }
  jammerDrawStatusSeparator();

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(5, 22 + kJammerYSHIFT);
  tft.print("Freq:");
  tft.setCursor(130, 22 + kJammerYSHIFT);
  tft.print("Mode:");
  tft.setCursor(5, 42 + kJammerYSHIFT);
  tft.print("Status:");

  s_jammerStaticDrawn = true;
}

static void jammerDrawProgressBar(int progress) {
  tft.fillRect(0, kJammerProgressY, 240, 4, TFT_BLACK);
  if (progress > 0) {
    tft.fillRect(0, kJammerProgressY, progress, 4, UI_WARN);
  }
}

static void jammerDrawBlinkDot(bool on) {
  const int cx = 220;
  const int cy = 22 + kJammerYSHIFT;
  const int r = 2;
  if (on) {
    tft.fillCircle(cx, cy, r, UI_WARN);
  } else {
    tft.fillRect(cx - r, cy - r, r * 2 + 1, r * 2 + 1, TFT_BLACK);
  }
}

static void jammerPollBlinkIndicator() {
  const bool wantBlink = autoMode && jammingRunning;
  const bool blinkOn = wantBlink && ((millis() % 1000) < 500);
  if (!s_jammerDisp.valid) {
    return;
  }
  if (blinkOn != s_jammerDisp.blinkOn) {
    jammerDrawBlinkDot(blinkOn);
    s_jammerDisp.blinkOn = blinkOn;
  }
}

void updateDisplay() {
    jammerDrawStaticChrome();

    char freqBuf[20];
    char modeBuf[8];
    char statusBuf[8];

    if (autoMode) {
      snprintf(freqBuf, sizeof(freqBuf), "Auto:%.1f", targetFrequency);
    } else {
      snprintf(freqBuf, sizeof(freqBuf), "%.2f MHz", targetFrequency);
    }
    snprintf(modeBuf, sizeof(modeBuf), "%s", continuousMode ? "Cont" : "Noise");
    snprintf(statusBuf, sizeof(statusBuf), "%s", jammingRunning ? "Jamming" : "Idle   ");

    const int freqKey = (int)(targetFrequency * 100.0f + 0.5f);
    const bool fullRedraw = !s_jammerDisp.valid;
    if (fullRedraw || s_jammerDisp.freqMHz100 != freqKey ||
        s_jammerDisp.autoMode != autoMode) {
      jammerDrawValueCell(40, 22 + kJammerYSHIFT, 96, kJammerValueLineH, freqBuf,
                          autoMode ? UI_WARN : UI_TEXT);
      s_jammerDisp.freqMHz100 = freqKey;
      s_jammerDisp.autoMode = autoMode;
    }

    if (fullRedraw || s_jammerDisp.continuousMode != continuousMode) {
      jammerDrawValueCell(165, 22 + kJammerYSHIFT, 40, kJammerValueLineH, modeBuf,
                          continuousMode ? UI_WARN : UI_TEXT);
      s_jammerDisp.continuousMode = continuousMode;
    }

    if (fullRedraw || s_jammerDisp.jammingRunning != jammingRunning) {
      jammerDrawValueCell(50, 42 + kJammerYSHIFT, 72, kJammerValueLineH, statusBuf,
                          jammingRunning ? UI_WARN : UI_TEXT);
      s_jammerDisp.jammingRunning = jammingRunning;
    }

    if (autoMode) {
      const int progress = ::map(currentFrequencyIndex, 0, numFrequencies - 1, 0, 240);
      if (fullRedraw || s_jammerDisp.progress != progress) {
        jammerDrawProgressBar(progress);
        s_jammerDisp.progress = progress;
      }
    } else if (s_jammerDisp.progress != -1) {
      jammerDrawProgressBar(0);
      s_jammerDisp.progress = -1;
      if (s_jammerDisp.blinkOn) {
        jammerDrawBlinkDot(false);
        s_jammerDisp.blinkOn = false;
      }
    }

    jammerDrawStatusSeparator();
    s_jammerDisp.valid = true;
}

void runUI() {
    #define SCREEN_WIDTH  240
    #define SCREENHEIGHT 320
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 6

    static int iconX[ICON_NUM] = {50, 90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_power,
        bitmap_icon_antenna,
        bitmap_icon_random,
        bitmap_icon_sort_down_minus,
        bitmap_icon_sort_up_plus,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, 240, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, 240, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                  jammingRunning = !jammingRunning;
                    if (jammingRunning) {
                        Serial.println("Jamming started");
                        ELECHOUSE_cc1101.setMHZ(targetFrequency);
                        ELECHOUSE_cc1101.SetTx();
                    } else {
                        Serial.println("Jamming stopped");
                        ELECHOUSE_cc1101.setSidle();
                        digitalWrite(TX_PIN, LOW);
                    }
                    updateDisplay();
                    lastDebounceTime = millis();
                    break;
                case 1:
                 continuousMode = !continuousMode;
                  Serial.print("Jamming mode: ");
                  Serial.println(continuousMode ? "Continuous Carrier" : "Noise");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 2:
                  autoMode = !autoMode;
                  Serial.print("Frequency mode: ");
                  Serial.println(autoMode ? "Automatic" : "Manual");
                  if (autoMode) {
                      currentFrequencyIndex = 0;
                      targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                      lastSweepTime = millis();
                      subjammerApplyFrequency();
                      s_jammerDisp.freqMHz100 = -1;
                  }
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 3:
                  currentFrequencyIndex = (currentFrequencyIndex - 1 + numFrequencies) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                 case 4:
                  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
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
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 5) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
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
#undef SCREEN_WIDTH
#undef SCREENHEIGHT
#undef STATUS_BAR_Y_OFFSET
#undef STATUS_BAR_HEIGHT
#undef ICON_SIZE
#undef ICON_NUM
}

void subjammerSetup() {
    Serial.begin(115200);
    setTouchButtonInputEnabled(true);
    subghzSetJammerNavLabels();
    subghzClearBody(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);
    subghzRedrawNavChrome();

    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setModulation(0);
    ELECHOUSE_cc1101.setRxBW(500.0);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    ELECHOUSE_cc1101.SetTx();

    randomSeed(analogRead(0));

#if HAS_PCF8574_BUTTONS
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
#endif
    delay(100);

    subghzClearBody(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);

    setupTouchscreen();

   jammerInvalidateDisplay();
   updateDisplay();
   uiDrawn = false;
   subghzRedrawNavChrome();
}

void subjammerLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    if (uiDrawn) {
      tft.drawFastHLine(0, 19, 240, UI_LINE);
      tft.drawFastHLine(0, 36, 240, UI_LINE);
      if (s_jammerDisp.valid) {
        jammerDrawStatusSeparator();
      }
    }
    jammerPollBlinkIndicator();
    subjammerHandleNavButtons();

#if HAS_PCF8574_BUTTONS
    int btnLeftState = pcf.digitalRead(JAM_BTN_LEFT);
    int btnRightState = pcf.digitalRead(JAM_BTN_RIGHT);
    int btnUpState = pcf.digitalRead(JAM_BTN_UP);
    int btnDownState = pcf.digitalRead(JAM_BTN_DOWN);
#else
    int btnLeftState = isPhysicalButtonPressed(BTN_LEFT) ? LOW : HIGH;
    int btnRightState = isPhysicalButtonPressed(BTN_RIGHT) ? LOW : HIGH;
    int btnUpState = isPhysicalButtonPressed(BTN_UP) ? LOW : HIGH;
    int btnDownState = isPhysicalButtonPressed(BTN_DOWN) ? LOW : HIGH;
#endif

    if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
        subjammerToggleJam();
    }

    if (btnRightState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        subjammerFreqNext();
    }

    if (btnLeftState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        subjammerFreqPrev();
    }

    if (btnDownState == LOW && millis() - lastDebounceTime > debounceDelay) {
        subjammerToggleAuto();
    }

    subjammerAutoSweepIfDue();

    if (jammingRunning) {
        ELECHOUSE_cc1101.SetTx();

        if (continuousMode) {
            ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, 0xFF);
            ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
            digitalWrite(TX_PIN, HIGH);
        } else {
            for (int i = 0; i < 10; i++) {
                uint32_t noise = random(16777216);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise >> 16);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, (noise >> 8) & 0xFF);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise & 0xFF);
                ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
                delayMicroseconds(50);
              }
          }
      }
  }
}

namespace SubBrute {

static constexpr uint8_t BRUTE_TX_PIN = SUBGHZ_TX_PIN;

static const uint32_t kBruteFreqList[] = {
    300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
    318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
    434420000, 434775000, 438900000, 868350000, 915000000, 925000000
};
static constexpr int kBruteFreqCount =
    (int)(sizeof(kBruteFreqList) / sizeof(kBruteFreqList[0]));

static const uint8_t kBitsChoices[] = {8, 10, 12, 16, 18, 20, 24};
static constexpr int kBitsChoiceCount =
    (int)(sizeof(kBitsChoices) / sizeof(kBitsChoices[0]));

static const uint16_t kPulseChoicesUs[] = {250, 350, 400, 500};
static constexpr int kPulseChoiceCount =
    (int)(sizeof(kPulseChoicesUs) / sizeof(kPulseChoicesUs[0]));

static constexpr int kBarBottom = 36;
static constexpr int kPanelTop = 44;
static constexpr int kBoxHeaderH = 14;
static constexpr int kLineH = 14;
static constexpr int kPanelPadX = 4;
static constexpr int kPanelW = 240 - (kPanelPadX * 2);
static constexpr int kLabelX = 12;
static constexpr int kValueX = 58;
static constexpr int kValueW = 170;
static constexpr int kSettingsRows = 4;
static constexpr int kSettingsInnerH = kBoxHeaderH + (kSettingsRows * kLineH) + 4;
static constexpr int kProgressInnerH = kBoxHeaderH + (kLineH * 2) + 16;
static constexpr int kRadius = 3;

enum FocusRow : uint8_t { FOCUS_FREQ = 0, FOCUS_BITS, FOCUS_MODE, FOCUS_OPT, FOCUS_COUNT };
enum RunMode : uint8_t { MODE_DEBRUIJN = 0, MODE_BRUTE = 1 };
enum RunState : uint8_t { ST_IDLE = 0, ST_RUNNING, ST_DONE, ST_STOPPED };

static RCSwitch s_switch;
static bool s_uiDrawn = false;
static unsigned long s_lastDebounce = 0;
static constexpr unsigned long kDebounceMs = 200;

static int s_freqIndex = 11;  // 433.92 MHz
static int s_bitsIndex = 2;   // 12-bit
static int s_pulseIndex = 1;  // 350 us
static int s_protocol = 1;
static FocusRow s_focus = FOCUS_FREQ;
static RunMode s_mode = MODE_DEBRUIJN;
static RunState s_runState = ST_IDLE;
static bool s_stopRequested = false;
static bool s_running = false;

static uint32_t s_progressDone = 0;
static uint32_t s_progressTotal = 0;
static uint32_t s_lastUiProgress = 0xFFFFFFFFu;
static uint8_t s_lastUiPct = 255;
static RunState s_lastUiState = ST_IDLE;
static FocusRow s_lastUiFocus = FOCUS_COUNT;
static RunMode s_lastUiMode = MODE_BRUTE;
static int s_lastUiFreq = -1;
static int s_lastUiBits = -1;
static int s_lastUiPulse = -1;
static int s_lastUiProto = -1;
static bool s_chromeDrawn = false;

static float bruteFreqMHz() {
  return kBruteFreqList[s_freqIndex % kBruteFreqCount] / 1000000.0f;
}

static uint8_t bruteBits() {
  return kBitsChoices[s_bitsIndex % kBitsChoiceCount];
}

static uint16_t brutePulseUs() {
  return kPulseChoicesUs[s_pulseIndex % kPulseChoiceCount];
}

static int settingsPanelY() { return kPanelTop; }
static int settingsPanelH() { return kSettingsInnerH; }
static int progressPanelY() { return kPanelTop + kSettingsInnerH + 6; }
static int progressPanelH() { return kProgressInnerH; }
static int hintY0() { return progressPanelY() + kProgressInnerH + 6; }

static int rowTextY(int row) {
  return settingsPanelY() + kBoxHeaderH + 2 + row * kLineH;
}

// Fibonacci LFSR feedback: taps are 1-indexed from LSB.
static uint32_t bruteLfsrFeedback(uint8_t n, uint32_t state) {
  uint32_t fb = 0;
  switch (n) {
    case 8:
      fb ^= (state >> (8 - 1)) & 1u;
      fb ^= (state >> (6 - 1)) & 1u;
      fb ^= (state >> (5 - 1)) & 1u;
      fb ^= (state >> (4 - 1)) & 1u;
      break;
    case 10:
      fb ^= (state >> (10 - 1)) & 1u;
      fb ^= (state >> (7 - 1)) & 1u;
      break;
    case 12:
      fb ^= (state >> (12 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      fb ^= (state >> (8 - 1)) & 1u;
      fb ^= (state >> (6 - 1)) & 1u;
      break;
    case 16:
      fb ^= (state >> (16 - 1)) & 1u;
      fb ^= (state >> (14 - 1)) & 1u;
      fb ^= (state >> (13 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      break;
    case 18:
      fb ^= (state >> (18 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      break;
    case 20:
      fb ^= (state >> (20 - 1)) & 1u;
      fb ^= (state >> (17 - 1)) & 1u;
      break;
    case 24:
      fb ^= (state >> (24 - 1)) & 1u;
      fb ^= (state >> (23 - 1)) & 1u;
      fb ^= (state >> (22 - 1)) & 1u;
      fb ^= (state >> (17 - 1)) & 1u;
      break;
    default:
      fb = (state >> (n - 1)) & 1u;
      break;
  }
  (void)n;
  return fb & 1u;
}

static void bruteRadioIdle() {
  ELECHOUSE_cc1101.setSidle();
  digitalWrite(BRUTE_TX_PIN, LOW);
}

static void brutePrepareTx() {
  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPA(12);
  pinMode(BRUTE_TX_PIN, OUTPUT);
  digitalWrite(BRUTE_TX_PIN, LOW);
  ELECHOUSE_cc1101.SetTx();
}

static void bruteFinishTx() {
  s_switch.disableTransmit();
  s_switch.disableReceive();
  bruteRadioIdle();
  pinMode(BRUTE_TX_PIN, OUTPUT);
  digitalWrite(BRUTE_TX_PIN, LOW);
}

static bool bruteShouldAbort() {
  if (feature_exit_requested || featureExitButtonPressed()) {
    feature_exit_requested = true;
    s_stopRequested = true;
    return true;
  }
  if (s_stopRequested) {
    return true;
  }
  maintainTouchNavBar();
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    s_stopRequested = true;
    return true;
  }
  static bool prevPhysUp = false;
  const bool physUp = isPhysicalButtonPressed(BTN_UP);
  if (physUp && !prevPhysUp) {
    prevPhysUp = physUp;
    s_stopRequested = true;
    return true;
  }
  prevPhysUp = physUp;

  int x, y;
  if (readTouchXY(x, y)) {
    if (y > 20 && y < kBarBottom && x >= 10 && x < 26) {
      feature_exit_requested = true;
      s_stopRequested = true;
      return true;
    }
  }
  return false;
}

static void bruteWaitGoRelease() {
  const uint32_t t0 = millis();
  while ((isTouchNavButtonPressed(BTN_UP) || isPhysicalButtonPressed(BTN_UP)) &&
         (millis() - t0) < 800) {
    delay(5);
  }
  delay(30);
  (void)isTouchNavButtonPressedEdge(BTN_UP);
}

static void drawPanelFrame(int y, int h, const char* title) {
  tft.drawRoundRect(kPanelPadX, y, kPanelW, h, kRadius, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y + 3);
  tft.print(title);
}

static void clearValueCell(int y) {
  tft.fillRect(kValueX, y, kValueW, kLineH - 1, TFT_BLACK);
}

static void drawSettingRow(int row, bool focused) {
  const int y = rowTextY(row);
  tft.fillRect(kLabelX, y, kValueX - kLabelX - 2, kLineH - 1, TFT_BLACK);
  clearValueCell(y);

  const char* label = "?";
  char value[28];
  value[0] = '\0';

  switch (row) {
    case FOCUS_FREQ:
      label = "Freq";
      snprintf(value, sizeof(value), "%.2f MHz", bruteFreqMHz());
      break;
    case FOCUS_BITS:
      label = "Bits";
      snprintf(value, sizeof(value), "%u", (unsigned)bruteBits());
      break;
    case FOCUS_MODE:
      label = "Mode";
      snprintf(value, sizeof(value), "%s",
               s_mode == MODE_DEBRUIJN ? "De Bruijn" : "Brute");
      break;
    case FOCUS_OPT:
      if (s_mode == MODE_DEBRUIJN) {
        label = "Pulse";
        snprintf(value, sizeof(value), "%u us", (unsigned)brutePulseUs());
      } else {
        label = "Proto";
        snprintf(value, sizeof(value), "%d", s_protocol);
      }
      break;
    default:
      break;
  }

  tft.setTextSize(1);
  tft.setTextColor(focused ? ORANGE : UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y);
  tft.print(focused ? ">" : " ");
  tft.print(label);

  tft.setTextColor(focused ? ORANGE : UI_TEXT, TFT_BLACK);
  tft.setCursor(kValueX, y);
  tft.print(value);
}

static void drawProgressBody(bool force) {
  const int py = progressPanelY();
  const int statusY = py + kBoxHeaderH + 2;
  const int countY = statusY + kLineH;
  const int barX = kLabelX;
  const int barW = kPanelW - 16;
  const int barH = 6;

  uint8_t pct = 0;
  if (s_progressTotal > 0) {
    pct = (uint8_t)((s_progressDone * 100UL) / s_progressTotal);
    if (pct > 100) pct = 100;
  }

  const bool stateChanged = force || s_lastUiState != s_runState;
  const bool progChanged =
      force || s_lastUiProgress != s_progressDone || s_lastUiPct != pct;

  if (stateChanged) {
    tft.fillRect(kLabelX, statusY, kPanelW - 16, kLineH - 1, TFT_BLACK);
    tft.setTextSize(1);
    const char* st = "Idle";
    uint16_t col = UI_DIM_TEXT;
    if (s_runState == ST_RUNNING) {
      st = "Running";
      col = ORANGE;
    } else if (s_runState == ST_DONE) {
      st = "Done";
      col = UI_TEXT;
    } else if (s_runState == ST_STOPPED) {
      st = "Stopped";
      col = UI_WARN;
    }
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(kLabelX, statusY);
    tft.print("Status");
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(kValueX, statusY);
    tft.print(st);
    s_lastUiState = s_runState;
  }

  if (progChanged) {
    tft.fillRect(kLabelX, countY, kPanelW - 16, kLineH - 1, TFT_BLACK);
    char buf[40];
    if (s_progressTotal == 0) {
      snprintf(buf, sizeof(buf), "0 / 0");
    } else if (s_progressTotal >= 1000000UL) {
      snprintf(buf, sizeof(buf), "%lu/%lu %u%%",
               (unsigned long)s_progressDone,
               (unsigned long)s_progressTotal,
               (unsigned)pct);
    } else {
      snprintf(buf, sizeof(buf), "%lu / %lu  %u%%",
               (unsigned long)s_progressDone,
               (unsigned long)s_progressTotal,
               (unsigned)pct);
    }
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(kLabelX, countY);
    tft.print("Count");
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.setCursor(kValueX, countY);
    tft.print(buf);

    const int barY2 = countY + kLineH + 2;
    tft.fillRect(barX, barY2, barW, barH, DARK_GRAY);
    const int fill = (s_progressTotal > 0)
                         ? (int)((s_progressDone * (uint32_t)barW) / s_progressTotal)
                         : 0;
    if (fill > 0) {
      tft.fillRect(barX, barY2, min(fill, barW), barH, ORANGE);
    }
    s_lastUiProgress = s_progressDone;
    s_lastUiPct = pct;
  }
}

static void drawHints() {
  const int y0 = hintY0();
  const int bottom = subghzContentBottom();
  if (y0 + 20 >= bottom) {
    return;
  }
  tft.fillRect(0, y0, 240, min(28, bottom - y0), TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y0);
  tft.print("Sel focus   Prev/Next adjust");
  if (y0 + 12 < bottom) {
    tft.setCursor(kLabelX, y0 + 12);
    tft.print("Go start/stop");
  }
}

static void updateDisplay(bool force = false) {
  if (force || !s_chromeDrawn) {
    const int bodyBottom = subghzContentBottom();
    if (bodyBottom > kBarBottom) {
      tft.fillRect(0, kBarBottom + 1, 240, bodyBottom - kBarBottom - 1, TFT_BLACK);
    }
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.drawFastHLine(0, kBarBottom, 240, UI_LINE);

    drawPanelFrame(settingsPanelY(), settingsPanelH(), "Settings");
    drawPanelFrame(progressPanelY(), progressPanelH(), "Progress");
    drawHints();

    for (int r = 0; r < kSettingsRows; r++) {
      drawSettingRow(r, (FocusRow)r == s_focus);
    }
    s_lastUiFocus = s_focus;
    s_lastUiMode = s_mode;
    s_lastUiFreq = s_freqIndex;
    s_lastUiBits = s_bitsIndex;
    s_lastUiPulse = s_pulseIndex;
    s_lastUiProto = s_protocol;
    s_lastUiState = (RunState)255;
    s_lastUiProgress = 0xFFFFFFFFu;
    s_lastUiPct = 255;
    s_chromeDrawn = true;
    drawProgressBody(true);
    return;
  }

  const bool settingsDirty =
      s_lastUiFocus != s_focus || s_lastUiMode != s_mode ||
      s_lastUiFreq != s_freqIndex || s_lastUiBits != s_bitsIndex ||
      s_lastUiPulse != s_pulseIndex || s_lastUiProto != s_protocol;

  if (settingsDirty) {
    for (int r = 0; r < kSettingsRows; r++) {
      drawSettingRow(r, (FocusRow)r == s_focus);
    }
    s_lastUiFocus = s_focus;
    s_lastUiMode = s_mode;
    s_lastUiFreq = s_freqIndex;
    s_lastUiBits = s_bitsIndex;
    s_lastUiPulse = s_pulseIndex;
    s_lastUiProto = s_protocol;
  }

  drawProgressBody(false);
}

static void invalidateChrome() {
  s_chromeDrawn = false;
}

static void adjustFocused(int dir) {
  if (s_running) {
    return;
  }
  switch (s_focus) {
    case FOCUS_FREQ:
      s_freqIndex = (s_freqIndex + dir + kBruteFreqCount) % kBruteFreqCount;
      ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
      break;
    case FOCUS_BITS:
      s_bitsIndex = (s_bitsIndex + dir + kBitsChoiceCount) % kBitsChoiceCount;
      break;
    case FOCUS_MODE:
      s_mode = (s_mode == MODE_DEBRUIJN) ? MODE_BRUTE : MODE_DEBRUIJN;
      break;
    case FOCUS_OPT:
      if (s_mode == MODE_DEBRUIJN) {
        s_pulseIndex = (s_pulseIndex + dir + kPulseChoiceCount) % kPulseChoiceCount;
      } else {
        s_protocol += dir;
        if (s_protocol < 1) s_protocol = 12;
        if (s_protocol > 12) s_protocol = 1;
      }
      break;
    default:
      break;
  }
  updateDisplay();
  s_lastDebounce = millis();
}

static void cycleFocus() {
  if (s_running) {
    return;
  }
  s_focus = (FocusRow)((s_focus + 1) % FOCUS_COUNT);
  updateDisplay();
  s_lastDebounce = millis();
}

static void focusRowAtY(int y) {
  if (s_running) {
    return;
  }
  for (int r = 0; r < kSettingsRows; r++) {
    const int ry = rowTextY(r);
    if (y >= ry && y < ry + kLineH) {
      s_focus = (FocusRow)r;
      updateDisplay();
      s_lastDebounce = millis();
      return;
    }
  }
}

static void runDeBruijnStream() {
  const uint8_t n = bruteBits();
  const uint16_t pulse = brutePulseUs();
  const uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1UL << n) - 1UL);
  const uint32_t total = (n >= 32) ? 0xFFFFFFFFu : ((1UL << n) - 1UL);

  s_progressTotal = total;
  s_progressDone = 0;
  s_runState = ST_RUNNING;
  updateDisplay(true);

  brutePrepareTx();

  uint32_t state = 1u;
  for (uint32_t i = 0; i < total; i++) {
    if ((i & 0xFFu) == 0) {
      if (bruteShouldAbort()) {
        break;
      }
      yield();
      delay(0);
      s_progressDone = i;
      drawProgressBody(false);
    }

    const uint8_t bit = (uint8_t)(state & 1u);
    digitalWrite(BRUTE_TX_PIN, bit ? HIGH : LOW);
    delayMicroseconds(pulse);

    const uint32_t fb = bruteLfsrFeedback(n, state);
    state = ((state << 1) | fb) & mask;
  }

  digitalWrite(BRUTE_TX_PIN, LOW);
  bruteFinishTx();

  if (s_stopRequested || feature_exit_requested) {
    s_runState = ST_STOPPED;
  } else {
    s_progressDone = total;
    s_runState = ST_DONE;
  }
  s_running = false;
  s_stopRequested = false;
  updateDisplay(true);
}

static void runBruteForce() {
  const uint8_t bits = bruteBits();
  // Practical cap: framed RCSwitch brute beyond 12 bits is extremely slow.
  // Still allow larger sizes, but keep the loop responsive.
  const uint32_t total = (bits >= 31) ? 0x7FFFFFFFu : (1UL << bits);

  s_progressTotal = total;
  s_progressDone = 0;
  s_runState = ST_RUNNING;
  updateDisplay(true);

  brutePrepareTx();
  s_switch.disableReceive();
  s_switch.enableTransmit(BRUTE_TX_PIN);
  s_switch.setProtocol(s_protocol);
  s_switch.setPulseLength(brutePulseUs());
  // Default RCSwitch repeats (~10) make each code ~0.5–1s → looks frozen.
  s_switch.setRepeatTransmit(1);

  uint32_t lastUiMs = millis();
  for (uint32_t code = 0; code < total; code++) {
    // Poll stop often — send() itself blocks briefly per code.
    if ((code & 0x03u) == 0) {
      if (bruteShouldAbort()) {
        s_progressDone = code;
        break;
      }
      yield();
    }

    s_switch.send(code, bits);
    s_progressDone = code + 1;

    // Keep progress alive so the UI doesn't look hung.
    const uint32_t now = millis();
    if (now - lastUiMs >= 150) {
#if defined(CC1101_CS)
      digitalWrite(CC1101_CS, HIGH);
#endif
      drawProgressBody(false);
      maintainTouchNavBar();
      // TFT SPI can disturb CC1101 — re-enter TX for the next burst.
      ELECHOUSE_cc1101.SetTx();
      lastUiMs = now;
      yield();
    }
  }

  bruteFinishTx();

  if (s_stopRequested || feature_exit_requested) {
    s_runState = ST_STOPPED;
  } else {
    s_progressDone = total;
    s_runState = ST_DONE;
  }
  s_running = false;
  s_stopRequested = false;
  updateDisplay(true);
}

static void startOrStop() {
  if (s_running) {
    s_stopRequested = true;
    s_lastDebounce = millis();
    return;
  }

  bruteWaitGoRelease();
  s_stopRequested = false;
  s_running = true;
  s_runState = ST_RUNNING;
  s_progressDone = 0;
  s_progressTotal = 0;
  updateDisplay(true);

  if (s_mode == MODE_DEBRUIJN) {
    runDeBruijnStream();
  } else {
    runBruteForce();
  }
  s_lastDebounce = millis();
}

void bruteHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    adjustFocused(-1);
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    adjustFocused(+1);
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    cycleFocus();
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    startOrStop();
    subghzWaitNavRelease(BTN_UP);
  }
}

void runUI() {
  // Avoid jammer/replay macros (SCREEN_WIDTH, ICON_NUM, …) leaking into this scope.
  static constexpr int kBarY = 20;
  static constexpr int kBarH = 16;
  static constexpr int kIconSz = 16;
  static constexpr int kIconN = 6;
  static constexpr int kScreenW = 240;

  static int iconX[kIconN] = {50, 90, 130, 170, 210, 10};
  static int iconY = kBarY;

  static const unsigned char* icons[kIconN] = {
      bitmap_icon_power,
      bitmap_icon_antenna,
      bitmap_icon_random,
      bitmap_icon_sort_down_minus,
      bitmap_icon_sort_up_plus,
      bitmap_icon_go_back
  };

  if (!s_uiDrawn) {
    tft.fillRect(0, kBarY, kScreenW, kBarH, DARK_GRAY);
    for (int i = 0; i < kIconN; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, UI_ICON);
      }
    }
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.drawFastHLine(0, kBarY + kBarH, 240, UI_LINE);
    s_uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], kIconSz, kIconSz, UI_ICON);
      animationState = 2;
      switch (activeIcon) {
        case 0:
          startOrStop();
          break;
        case 1:
          if (!s_running) {
            s_mode = (s_mode == MODE_DEBRUIJN) ? MODE_BRUTE : MODE_DEBRUIJN;
            updateDisplay();
          }
          break;
        case 2:
          cycleFocus();
          break;
        case 3:
          adjustFocused(-1);
          break;
        case 4:
          adjustFocused(+1);
          break;
        case 5:
          feature_exit_requested = true;
          s_stopRequested = true;
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > kBarY && y < kBarY + kBarH) {
        for (int i = 0; i < kIconN; i++) {
          if (x > iconX[i] && x < iconX[i] + kIconSz) {
            if (icons[i] != NULL && animationState == 0) {
              if (i == 5) {
                feature_exit_requested = true;
                s_stopRequested = true;
              } else {
                tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
              }
            }
            break;
          }
        }
      } else if (!s_running && y >= settingsPanelY() &&
                 y < settingsPanelY() + settingsPanelH()) {
        focusRowAtY(y);
      }
    }
    lastTouchCheck = millis();
  }
}

void subBruteSetup() {
  Serial.begin(115200);
  setTouchButtonInputEnabled(true);
  subghzSetBruteNavLabels();
  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();

  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();

#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(500.0);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
  ELECHOUSE_cc1101.setSidle();
  pinMode(BRUTE_TX_PIN, INPUT);

  s_freqIndex = 11;
  s_bitsIndex = 2;
  s_pulseIndex = 1;
  s_protocol = 1;
  s_focus = FOCUS_FREQ;
  s_mode = MODE_DEBRUIJN;
  s_runState = ST_IDLE;
  s_running = false;
  s_stopRequested = false;
  s_progressDone = 0;
  s_progressTotal = 0;

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
#endif
  delay(100);

  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setupTouchscreen();

  invalidateChrome();
  s_uiDrawn = false;
  updateDisplay(true);
  subghzRedrawNavChrome();
}

void subBruteLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    s_stopRequested = true;
    return;
  }

  maintainTouchNavBar();
  runUI();
  if (s_uiDrawn) {
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.drawFastHLine(0, kBarBottom, 240, UI_LINE);
  }
  bruteHandleNavButtons();

#if HAS_PCF8574_BUTTONS
  const int btnLeftState = pcf.digitalRead(BTN_LEFT);
  const int btnRightState = pcf.digitalRead(BTN_RIGHT);
  const int btnUpState = pcf.digitalRead(BTN_UP);
  const int btnDownState = pcf.digitalRead(BTN_DOWN);
#else
  const int btnLeftState = isPhysicalButtonPressed(BTN_LEFT) ? LOW : HIGH;
  const int btnRightState = isPhysicalButtonPressed(BTN_RIGHT) ? LOW : HIGH;
  const int btnUpState = isPhysicalButtonPressed(BTN_UP) ? LOW : HIGH;
  const int btnDownState = isPhysicalButtonPressed(BTN_DOWN) ? LOW : HIGH;
#endif

  if (btnLeftState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    adjustFocused(-1);
  }
  if (btnRightState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    adjustFocused(+1);
  }
  if (btnDownState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    cycleFocus();
  }
  if (btnUpState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    startOrStop();
  }
}

}  // namespace SubBrute

namespace jammingdetector {

static constexpr uint16_t JD_SAMPLES = ESP32DIV_JD_RSSI_SAMPLES;
static constexpr double JD_SAMPLE_HZ = 5000.0;
static constexpr double JD_RXBW = 650.0;
static constexpr int JD_MARGIN_DB = 18;
static constexpr int JD_ABS_THRESH_DBM = -75;
static constexpr float JD_BUSY_WIN_DUTY = 0.50f;
static constexpr uint32_t JD_JAM_STREAK_MS = 400;
static constexpr float JD_JAM_AVG_DUTY = 0.80f;
static constexpr float JD_ACTIVITY_DUTY = 0.10f;
static constexpr uint8_t JD_RING = 20;
static constexpr int JD_FLOOR_INIT_DBM = -95;

static const uint32_t kFreqHz[] = {433920000UL, 434420000UL, 315000000UL, 868350000UL};
static const char* kFreqLabel[] = {"433.92", "434.42", "315.00", "868.35"};
static constexpr uint8_t kFreqCount = sizeof(kFreqHz) / sizeof(kFreqHz[0]);
static uint8_t freqIdx = 1;

static unsigned int samplingPeriod = 0;

static float noiseFloor = JD_FLOOR_INIT_DBM;
static uint32_t busyStreakMs = 0;
static float dutyRing[JD_RING];
static uint8_t dutyRingPos = 0;
static bool jamActive = false;
static uint32_t jamStartMs = 0;
static int jamPeakDbm = -127;
static uint32_t eventCount = 0;

static bool logEnabled = false;
static bool logMounted = false;
static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;

static constexpr int kJdBarBottom = 36;
static constexpr int kJdSectionGap = 6;
static constexpr int kJdPad = 4;
static constexpr int kJdPanelW = 240 - (kJdPad * 2);
static constexpr int kJdRadius = 3;
static constexpr int kJdLabelX = 10;
static constexpr int kJdCol2LabelX = 128;
static constexpr int kJdValueX = 44;
static constexpr int kJdCol2ValueX = 162;
static constexpr int kJdCol1ValueW = 78;
static constexpr int kJdCol2ValueW = 70;
static constexpr int kJdLineH = 12;
static constexpr int kJdPanelHeader = 14;
static constexpr int kJdInfoY = kJdBarBottom + 4;
static constexpr int kJdInfoH = 46;
static constexpr int kJdInfoRow1Y = kJdInfoY + kJdPanelHeader + 2;
static constexpr int kJdInfoRow2Y = kJdInfoRow1Y + kJdLineH + 2;
static constexpr int kJdStatusY = kJdInfoY + kJdInfoH + kJdSectionGap;
static constexpr int kJdStatusH = 30;
static constexpr int kJdWaveY = kJdStatusY + kJdStatusH + kJdSectionGap;
static constexpr int kJdWaveHeader = 14;

static constexpr int kWaveW = ESP32DIV_JD_WAVE_WIDTH;
static constexpr int kWaveRssiMin = -100;
static constexpr int kWaveRssiMax = -35;
static int8_t waveBuf[kWaveW];
static uint16_t waveWrite = 0;
static float waveSmooth = -95.0f;

static bool s_chromeDrawn = false;
static bool s_jdUiDrawn = false;
static bool s_waveHasPrev = false;
static int16_t s_wavePrevY[kWaveW];
static uint8_t s_statusState = 255;

struct JdDisp {
  bool valid = false;
  uint8_t freqIdx = 255;
  int rssi = -999;
  int floor = -999;
  int dutyPct = -1;
  uint32_t events = 0xFFFFFFFFu;
  bool logOn = false;
};
static JdDisp s_disp;

static void cc1101BeginRx() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
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
  s_disp.freqIdx = 255;
}

static bool jdMountSD() {
  if (logMounted && SD.exists("/")) return true;

#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  restoreSdAfterSharedSpi();
  logMounted = isSDCardAvailable();
  return logMounted;
}

static void logEvent(uint32_t whenMs, uint32_t durMs, int peakDbm, int dutyPct) {
  if (!logEnabled) return;

  restoreSdAfterSharedSpi();
  if (jdMountSD()) {
    if (!SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
    File f = SD.open(LOG_DIR "/jamdet.csv", FILE_APPEND);
    if (f) {
      f.printf("%lu,%s,JAM,%d,%lu,%d\n",
               (unsigned long)whenMs, kFreqLabel[freqIdx], peakDbm,
               (unsigned long)durMs, dutyPct);
      f.close();
    }
  }
  cc1101BeginRx();
}

static int jdWaveBottom() {
  return subghzContentBottom() - 2;
}

static int jdPlotTop() {
  return kJdWaveY + kJdWaveHeader + 4;
}

static int jdPlotHeight() {
  const int h = jdWaveBottom() - jdPlotTop() - 2;
  return h > 8 ? h : 8;
}

static int jdRssiToY(int dbm) {
  dbm = constrain(dbm, kWaveRssiMin, kWaveRssiMax);
  const int plotH = jdPlotHeight();
  return jdPlotTop() + plotH - 2 -
         ((dbm - kWaveRssiMin) * (plotH - 4) / (kWaveRssiMax - kWaveRssiMin));
}

static void jdDrawValueCell(int x, int y, int w, const char* text, uint16_t color) {
  tft.fillRect(x, y, w, kJdLineH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void jdInvalidateContent() {
  s_chromeDrawn = false;
  s_statusState = 255;
  s_disp.valid = false;
  s_waveHasPrev = false;
  waveWrite = 0;
  waveSmooth = -95.0f;
  memset(waveBuf, kWaveRssiMin, sizeof(waveBuf));
}

static void jdInvalidateAll() {
  jdInvalidateContent();
  s_jdUiDrawn = false;
}

static void jdResetStats() {
  eventCount = 0;
  busyStreakMs = 0;
  jamActive = false;
  noiseFloor = JD_FLOOR_INIT_DBM;
  for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
  jdInvalidateContent();
}

static void jdRunUI() {
  static constexpr int kBarY = 20;
  static constexpr int kBarH = 16;
  static constexpr int kIconSz = 16;
  static constexpr int kIconN = 5;
  static constexpr int kBackIdx = 4;

  static int iconX[kIconN] = {90, 130, 170, 210, 10};
  static int iconY = kBarY;

  static const unsigned char* icons[kIconN] = {
      bitmap_icon_sort_down_minus,
      bitmap_icon_floppy,
      bitmap_icon_undo,
      bitmap_icon_sort_up_plus,
      bitmap_icon_go_back,
  };

  if (!s_jdUiDrawn) {
    tft.fillRect(0, kBarY, 240, kBarH, DARK_GRAY);
    for (int i = 0; i < kIconN; i++) {
      tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, UI_ICON);
    }
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.drawFastHLine(0, kBarY + kBarH, 240, UI_LINE);
    s_jdUiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      if (activeIcon >= 0 && activeIcon != kBackIdx) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], kIconSz, kIconSz, UI_ICON);
      }
      animationState = 2;
      switch (activeIcon) {
        case 0:
          tuneTo(freqIdx + kFreqCount - 1);
          s_disp.freqIdx = 255;
          break;
        case 1:
          logEnabled = !logEnabled;
          s_disp.logOn = !logEnabled;
          break;
        case 2:
          jdResetStats();
          break;
        case 3:
          tuneTo(freqIdx + 1);
          s_disp.freqIdx = 255;
          break;
        case kBackIdx:
          feature_exit_requested = true;
          break;
        default:
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > kBarY && y < kBarY + kBarH) {
        for (int i = 0; i < kIconN; i++) {
          if (x > iconX[i] && x < iconX[i] + kIconSz) {
            if (animationState == 0) {
              if (i == kBackIdx) {
                feature_exit_requested = true;
              } else {
                tft.fillRect(iconX[i], iconY, kIconSz, kIconSz, DARK_GRAY);
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

static void jdDrawPanelFrame(int y, int h, const char* title) {
  tft.drawRoundRect(kJdPad, y, kJdPanelW, h, kJdRadius, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kJdLabelX, y + 3);
  tft.print(title);
}

static void jdDrawPlotGridLines() {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  const int plotTop = jdPlotTop();
  const int plotH = jdPlotHeight();
  if (plotH < 8) {
    return;
  }

  for (int g = 1; g < 4; g++) {
    const int gy = plotTop + (plotH * g) / 4;
    tft.drawFastHLine(plotX + 1, gy, plotW - 2, DARK_GRAY);
  }
  for (int g = 1; g < 4; g++) {
    const int gx = plotX + (plotW * g) / 4;
    tft.drawFastVLine(gx, plotTop + 1, plotH - 2, DARK_GRAY);
  }
}

static void jdDrawPlotBackground() {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  const int plotTop = jdPlotTop();
  const int plotH = jdPlotHeight();
  if (plotH < 8) {
    return;
  }

  tft.fillRect(plotX, plotTop, plotW, plotH, TFT_BLACK);
  jdDrawPlotGridLines();
}

static void jdDrawStaticChrome() {
  if (s_chromeDrawn) {
    return;
  }

  const int bodyBottom = jdWaveBottom();
  tft.fillRect(0, kJdBarBottom + 1, 240, bodyBottom - kJdBarBottom - 1, TFT_BLACK);

  jdDrawPanelFrame(kJdInfoY, kJdInfoH, "Monitor");
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kJdLabelX, kJdInfoRow1Y);
  tft.print("Freq");
  tft.setCursor(kJdLabelX, kJdInfoRow2Y);
  tft.print("RSSI");
  tft.setCursor(kJdCol2LabelX, kJdInfoRow1Y);
  tft.print("Duty");
  tft.setCursor(kJdCol2LabelX, kJdInfoRow2Y);
  tft.print("Log");

  const int waveH = bodyBottom - kJdWaveY;
  if (waveH > kJdWaveHeader + 12) {
    jdDrawPanelFrame(kJdWaveY, waveH, "Signal");
    jdDrawPlotBackground();
  }

  s_chromeDrawn = true;
  s_disp.valid = false;
  s_statusState = 255;
}

static void jdDrawStatusBox(bool jam, bool activity) {
  const uint8_t st = jam ? 2 : (activity ? 1 : 0);
  if (st == s_statusState && s_disp.valid) {
    return;
  }
  s_statusState = st;

  uint16_t bg = jam ? ORANGE : (activity ? UI_WARN : UI_OK);
  uint16_t fg = (jam || activity) ? TFT_BLACK : UI_FG;
  const char* s = jam ? "JAMMING DETECTED" : (activity ? "ACTIVITY" : "CLEAR");

  tft.fillRoundRect(kJdPad, kJdStatusY, kJdPanelW, kJdStatusH, kJdRadius, bg);
  tft.drawRoundRect(kJdPad, kJdStatusY, kJdPanelW, kJdStatusH, kJdRadius, UI_LINE);
  tft.setTextColor(fg, bg);
  uint8_t sz = 2;
  tft.setTextSize(sz);
  if (tft.textWidth(s) > kJdPanelW - 8) { sz = 1; tft.setTextSize(sz); }
  const int16_t tw = tft.textWidth(s);
  const int16_t th = 8 * sz;
  tft.setCursor(kJdPad + (kJdPanelW - tw) / 2, kJdStatusY + (kJdStatusH - th) / 2);
  tft.print(s);
  tft.setTextSize(1);
}

static void jdUpdateInfo(int rssiNow, int dutyPct) {
  jdDrawStaticChrome();

  const bool full = !s_disp.valid;
  char buf[28];

  if (full || s_disp.freqIdx != freqIdx) {
    snprintf(buf, sizeof(buf), "%s MHz", kFreqLabel[freqIdx]);
    jdDrawValueCell(kJdValueX, kJdInfoRow1Y, kJdCol1ValueW, buf, UI_TEXT);
    s_disp.freqIdx = freqIdx;
  }

  if (full || abs(s_disp.rssi - rssiNow) >= 2 ||
      abs(s_disp.floor - (int)noiseFloor) >= 2) {
    snprintf(buf, sizeof(buf), "%d/%d dBm", rssiNow, (int)noiseFloor);
    jdDrawValueCell(kJdValueX, kJdInfoRow2Y, kJdCol1ValueW, buf, UI_TEXT);
    s_disp.rssi = rssiNow;
    s_disp.floor = (int)noiseFloor;
  }

  if (full || abs(s_disp.dutyPct - dutyPct) >= 5 || s_disp.events != eventCount) {
    snprintf(buf, sizeof(buf), "%d%% E:%lu", dutyPct, (unsigned long)eventCount);
    jdDrawValueCell(kJdCol2ValueX, kJdInfoRow1Y, kJdCol2ValueW, buf, UI_TEXT);
    s_disp.dutyPct = dutyPct;
    s_disp.events = eventCount;
  }

  if (full || s_disp.logOn != logEnabled) {
    jdDrawValueCell(kJdCol2ValueX, kJdInfoRow2Y, kJdCol2ValueW,
                    logEnabled ? "on" : "off", logEnabled ? UI_OK : UI_DIM_TEXT);
    s_disp.logOn = logEnabled;
  }

  s_disp.valid = true;
}

static void jdDrawWaveform(bool jam, bool activity) {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  if (jdPlotHeight() < 8 || plotW < 2 || kWaveW < 2) {
    return;
  }

  const uint16_t waveColor = jam ? ORANGE : (activity ? UI_WARN : UI_OK);

  if (s_waveHasPrev) {
    for (int x = 0; x < plotW - 1; x++) {
      const int px0 = (x * (kWaveW - 1)) / (plotW - 1);
      const int px1 = ((x + 1) * (kWaveW - 1)) / (plotW - 1);
      tft.drawLine(plotX + x, s_wavePrevY[px0], plotX + x + 1, s_wavePrevY[px1], TFT_BLACK);
    }
  }

  for (int i = 0; i < kWaveW; i++) {
    s_wavePrevY[i] = jdRssiToY(waveBuf[(waveWrite + i) % kWaveW]);
  }

  for (int x = 0; x < plotW - 1; x++) {
    const int i0 = (x * (kWaveW - 1)) / (plotW - 1);
    const int i1 = ((x + 1) * (kWaveW - 1)) / (plotW - 1);
    tft.drawLine(plotX + x, s_wavePrevY[i0], plotX + x + 1, s_wavePrevY[i1], waveColor);
  }

  s_waveHasPrev = true;
  jdDrawPlotGridLines();
}

struct WindowStat { int peakDbm; int minDbm; float duty; uint32_t elapsedMs; };

static WindowStat sampleWindow() {
  const int busyThresh = max(JD_ABS_THRESH_DBM, (int)(noiseFloor + JD_MARGIN_DB));
  int peak = -127, lo = 0;
  uint16_t busy = 0;
  const float kEwmaAlpha = 0.35f;

  const uint32_t t0 = millis();
  uint32_t micro_s = micros();
  for (int i = 0; i < JD_SAMPLES; i++) {
    const int dbm = ELECHOUSE_cc1101.getRssi();
    if (dbm > peak) peak = dbm;
    if (dbm < lo) lo = dbm;
    if (dbm > busyThresh) busy++;

    waveSmooth = (kEwmaAlpha * dbm) + ((1.0f - kEwmaAlpha) * waveSmooth);
    if ((i & 1) == 0) {
      waveBuf[waveWrite] = (int8_t)constrain((int)lroundf(waveSmooth), kWaveRssiMin, kWaveRssiMax);
      waveWrite = (waveWrite + 1) % kWaveW;
    }

    while (micros() < micro_s + samplingPeriod) {}
    micro_s += samplingPeriod;
  }

  WindowStat st;
  st.peakDbm = peak;
  st.minDbm = lo;
  st.duty = (float)busy / JD_SAMPLES;
  st.elapsedMs = millis() - t0;
  return st;
}

static void evaluate(const WindowStat& st) {
  if (st.duty < 0.2f) noiseFloor = 0.95f * noiseFloor + 0.05f * st.minDbm;

  dutyRing[dutyRingPos] = st.duty;
  dutyRingPos = (dutyRingPos + 1) % JD_RING;
  float avgDuty = 0;
  for (uint8_t i = 0; i < JD_RING; i++) avgDuty += dutyRing[i];
  avgDuty /= JD_RING;

  if (st.duty >= JD_BUSY_WIN_DUTY) busyStreakMs += st.elapsedMs;
  else busyStreakMs = 0;

  const bool jam = (busyStreakMs >= JD_JAM_STREAK_MS) || (avgDuty >= JD_JAM_AVG_DUTY);

  if (jam && !jamActive) {
    jamActive = true;
    jamStartMs = millis();
    jamPeakDbm = st.peakDbm;
    eventCount++;
    s_disp.events = 0xFFFFFFFFu;
  } else if (jam && jamActive) {
    if (st.peakDbm > jamPeakDbm) jamPeakDbm = st.peakDbm;
  } else if (!jam && jamActive) {
    jamActive = false;
    logEvent(jamStartMs, millis() - jamStartMs, jamPeakDbm, (int)(avgDuty * 100));
  }
}

static bool edge(int pin, bool& prev) {
  const bool now = isPhysicalButtonPressed(pin);
  const bool e = now && !prev;
  prev = now;
  return e;
}

static void handleInput() {
  const bool navFreqDown = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_LEFT);
  const bool navFreqUp = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_RIGHT);
  const bool navReset = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_UP);
  const bool navLog = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_DOWN);

  if (edge(BTN_LEFT, prevLeft) || navFreqDown) tuneTo(freqIdx + kFreqCount - 1);
  if (edge(BTN_RIGHT, prevRight) || navFreqUp) tuneTo(freqIdx + 1);
  if (edge(BTN_UP, prevUp) || navReset) {
    jdResetStats();
  }
  if (edge(BTN_DOWN, prevDown) || navLog) {
    logEnabled = !logEnabled;
    s_disp.logOn = !logEnabled;
  }
}

static void exitCleanup() {
  ELECHOUSE_cc1101.setSidle();
  restoreSdAfterSharedSpi();
}

void Setup() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Freq-", "Log", "Exit", "Reset", "Freq+");

  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();

#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  cc1101BeginRx();
  tuneTo(freqIdx);

  samplingPeriod = round(1000000.0 * (1.0 / JD_SAMPLE_HZ));

  noiseFloor = JD_FLOOR_INIT_DBM;
  busyStreakMs = 0;
  jamActive = false;
  logMounted = false;
  for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
  prevLeft = prevRight = prevUp = prevDown = false;
  jdInvalidateAll();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  tft.setRotation(TFT_ROTATION);
  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();
  setupTouchscreen();
  jdRunUI();
  jdDrawStaticChrome();
  jdDrawStatusBox(false, false);
}

void Loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    exitCleanup();
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  jdRunUI();
  handleInput();

  const WindowStat st = sampleWindow();
  evaluate(st);

  const bool activity = !jamActive && (st.duty >= JD_ACTIVITY_DUTY);
  const int dutyPct = (int)(dutyRing[(dutyRingPos + JD_RING - 1) % JD_RING] * 100.0f);

  jdUpdateInfo(st.peakDbm, dutyPct);
  jdDrawStatusBox(jamActive, activity);
  jdDrawWaveform(jamActive, activity);
}

}  // namespace jammingdetector

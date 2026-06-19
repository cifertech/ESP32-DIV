#include <algorithm>
#include <vector>
#include "BuzzerService.h"
#include "KeyboardUI.h"
#include "StatusLedService.h"
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

    #ifdef SD_CD
    pinMode(SD_CD, INPUT_PULLUP);
    if (digitalRead(SD_CD)) return false;
    #endif

    #ifdef SD_SCLK
    #ifdef SD_MISO
    #ifdef SD_MOSI
    #ifdef SD_CS
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    #endif
    #endif
    #endif
    #endif

    #ifdef SD_CS
    if (SD.begin(SD_CS)) { subghz_sd_mounted = true; return true; }
    #endif

    #ifdef SD_CS_PIN
    #ifdef CC1101_CS
    if (SD_CS_PIN != CC1101_CS) {
      if (SD.begin(SD_CS_PIN)) { subghz_sd_mounted = true; return true; }
    }
    #else
    if (SD.begin(SD_CS_PIN)) { subghz_sd_mounted = true; return true; }
    #endif
    #endif

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

namespace replayat { void replayHandleNavButtons(); }
namespace subjammer { void subjammerHandleNavButtons(); }
namespace SavedProfile { void profileHandleNavButtons(); }

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

const uint16_t samplesSUB = 256;
const double FrequencySUB = 5000;

double attenuation_num = 10;

unsigned int sampling_period;
unsigned long micro_s;

double vRealSUB[samplesSUB];
double vImagSUB[samplesSUB];

byte red[128], green[128], blue[128];

unsigned int epochSUB = 0;
unsigned int colorcursor = 2016;

int rssi;

static constexpr uint8_t REPLAY_RX_PIN = SUBGHZ_RX_PIN;
static constexpr uint8_t REPLAY_TX_PIN = SUBGHZ_TX_PIN;

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

static void replayShowDetectNotice(const String& reason, int rssi = 0) {
  uint32_t now = millis();

  if (now - lastDetectAlertMs < 1200 && lastDetectAlertFreq == currentFrequencyIndex) return;
  lastDetectAlertMs = now;
  lastDetectAlertFreq = currentFrequencyIndex;

  char msg[96];
  float mhz = subghz_frequency_list[currentFrequencyIndex] / 1000000.0f;

  snprintf(msg, sizeof(msg), "%s @ %.2f MHz | RSSI %d", reason.c_str(), mhz, rssi);
  showNotificationActions("SubGHz Detected", msg, true);
  BuzzerService::beepCapture();
  StatusLedService::event(StatusLedService::Event::CaptureSuccess);
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

    if (!autoScanEnabled) {
      ELECHOUSE_cc1101.setSidle();
      ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
      ELECHOUSE_cc1101.SetRx();
    }
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

    mySwitch.disableReceive();
    delay(100);
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

    ELECHOUSE_cc1101.SetRx();
    mySwitch.disableTransmit();
    delay(100);
    mySwitch.enableReceive(REPLAY_RX_PIN);

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
  Serial.begin(115200);
  setTouchButtonInputEnabled(true);
  subghzSetReplayNavLabels();

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(500.0);

  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

  ELECHOUSE_cc1101.SetRx();

  mySwitch.enableReceive(REPLAY_RX_PIN);
  mySwitch.enableTransmit(REPLAY_TX_PIN);
  mySwitch.setRepeatTransmit(8);

  EEPROM.begin(EEPROM_SIZE);
  readProfileCount();

  EEPROM.get(ADDR_VALUE, receivedValue);
  EEPROM.get(ADDR_BITLEN, receivedBitLength);
  EEPROM.get(ADDR_PROTO, receivedProtocol);
  EEPROM.get(ADDR_FREQ, currentFrequencyIndex);

  const uint16_t freqCount = (uint16_t)(sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
  if (currentFrequencyIndex >= freqCount) currentFrequencyIndex = 0;

  tuneToIndex(currentFrequencyIndex, false);
  mySwitch.setReceiveTolerance(replayFreqIsLowBand(currentFrequencyIndex) ? 50 : 40);

  subghzClearBody(TFT_BLACK);
  tft.setRotation(TFT_ROTATION);

  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();

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

   replayInvalidateDisplay();
   updateDisplay();
   uiDrawn = false;
   subghzRedrawNavChrome();

}

void ReplayAttackLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
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

    BuzzerService::loop();

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

    ELECHOUSE_cc1101.SetRx();
    mySwitch.disableTransmit();
    delay(100);
    mySwitch.enableReceive(SUBGHZ_RX_PIN);

    delay(500);
    profileRestoreChrome();
}

void loadProfileCount() {

    refreshSdIndex(true);
}

void printProfiles() {
    Serial.println("Saved Profiles (SD index):");
    String err;
    refreshSdIndex(false);
    Serial.printf("Total profiles: %d\n", (int)sdTotalProfiles);
    if (!sdTotalProfiles) return;

    uint16_t n = sdTotalProfiles > 10 ? 10 : sdTotalProfiles;
    for (uint16_t i = 0; i < n; i++) {
      String pth; uint16_t li = 0;
      if (!locateGlobalIndex(sdFiles, i, pth, li)) continue;
      SubGhzProfile p{};
      if (!readProfileAt(pth, li, p, &err)) continue;
      Serial.printf("  [%d] %s @ %.2f MHz (val=%lu)\n", (int)i, p.name, p.frequency/1000000.0, (unsigned long)p.value);
    }
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
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    ELECHOUSE_cc1101.SetRx();

    mySwitch.enableReceive(SUBGHZ_RX_PIN);
    mySwitch.enableTransmit(SUBGHZ_TX_PIN);
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

#include <Preferences.h>
#include <ArduinoJson.h>
#include <SD.h>
#include "SettingsStore.h"
#include "utils.h"


static AppSettings g_settings;
AppSettings& settings() { return g_settings; }
static Preferences preferences;

static const AccentOption kAccentPresets[] = {
  {"Orange", 0xFBE4},
  {"Green",  0x07E0},
  {"Red",    0xF800},
  {"Cyan",   0x07FF},
  {"Purple", 0xF81F},
  {"Yellow", 0xFFE0},
  {"White",  0xFFFF},
};

uint8_t accentPresetClamp(uint8_t preset) {
  if (preset >= ACCENT_PRESET_COUNT) return 0;
  return preset;
}

uint16_t accentColor565(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].color565;
}

const char* accentPresetName(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].name;
}

const char* settingsBoardProfileId() {
  return TOUCH_PROFILE_ID;
}

void settingsApplyBoardTouchDefaults() {
  auto& s = g_settings;
  s.touchXMin = TOUCH_X_MIN;
  s.touchXMax = TOUCH_X_MAX;
  s.touchYMin = TOUCH_Y_MIN;
  s.touchYMax = TOUCH_Y_MAX;
}

static bool settingsTouchSavedForBoard(const StaticJsonDocument<512>& doc) {
  JsonObjectConst touch = doc["touch"];
  if (touch.isNull()) {
    return false;
  }
  if (!touch["xMin"].is<uint16_t>() || !touch["xMax"].is<uint16_t>() ||
      !touch["yMin"].is<uint16_t>() || !touch["yMax"].is<uint16_t>()) {
    return false;
  }
  const char* savedBoard = doc["board"] | "";
  if (savedBoard[0] == '\0') {
    return true;
  }
  return strcmp(savedBoard, TOUCH_PROFILE_ID) == 0;
}

bool mountSD() {

  if (sd_mounted) {
    if (SD.exists("/")) return true;
    sd_mounted = false;
  }

  sdSpiInit();

  #ifdef SD_CS
  if (sdMountChipSelect(SD_CS)) { sd_mounted = true; return true; }
  #endif
  #ifdef SD_CS_PIN

  #ifdef CC1101_CS
  if (SD_CS_PIN != CC1101_CS) {
    if (sdMountChipSelect(SD_CS_PIN)) { sd_mounted = true; return true; }
  }
  #else
  if (sdMountChipSelect(SD_CS_PIN)) { sd_mounted = true; return true; }
  #endif
  #endif
  return false;
}

static bool ensureDir(const char* dirPath) {
  if (!mountSD()) return false;
  if (!SD.exists(dirPath)) {
    if (SD.mkdir(dirPath)) return true;

    if (dirPath && dirPath[0] == '/') {
      return SD.mkdir(dirPath + 1);
    }
    return false;
  }
  return true;
}

static void loadFromPreferences() {
  preferences.begin("esp32div", true);
  auto& s = g_settings;
  s.brightness      = preferences.getUChar("bright", s.brightness);
  s.theme           = (Theme)preferences.getUChar("theme", (uint8_t)s.theme);
  s.accentColor     = accentPresetClamp(preferences.getUChar("accent", s.accentColor));
  s.neopixelEnabled = preferences.getBool("neopixel", s.neopixelEnabled);
  s.autoWifiScan    = preferences.getBool("autoWifiScan", s.autoWifiScan);
  s.autoBleScan     = preferences.getBool("autoBleScan", s.autoBleScan);

  if (s.autoWifiScan != s.autoBleScan) {
    bool en = (s.autoWifiScan || s.autoBleScan);
    s.autoWifiScan = en;
    s.autoBleScan  = en;
  }

  // Touch calibration: only restore if the stored board id matches the current
  // profile; otherwise keep the board defaults applied by the caller.
  String savedBoard = preferences.getString("board", "");
  if (savedBoard.length() == 0 || savedBoard == String(TOUCH_PROFILE_ID)) {
    s.touchXMin = preferences.getUShort("txMin", s.touchXMin);
    s.touchXMax = preferences.getUShort("txMax", s.touchXMax);
    s.touchYMin = preferences.getUShort("tyMin", s.touchYMin);
    s.touchYMax = preferences.getUShort("tyMax", s.touchYMax);
  }
  preferences.end();
}

static bool saveToPreferences() {
  preferences.begin("esp32div", false);
  auto& s = g_settings;
  size_t n = 0;
  n += preferences.putUChar("bright", s.brightness);
  n += preferences.putUChar("theme", (uint8_t)s.theme);
  n += preferences.putUChar("accent", s.accentColor);
  n += preferences.putBool("neopixel", s.neopixelEnabled);
  n += preferences.putBool("autoWifiScan", s.autoWifiScan);
  n += preferences.putBool("autoBleScan", s.autoBleScan);
  n += preferences.putString("board", TOUCH_PROFILE_ID);
  n += preferences.putUShort("txMin", s.touchXMin);
  n += preferences.putUShort("txMax", s.touchXMax);
  n += preferences.putUShort("tyMin", s.touchYMin);
  n += preferences.putUShort("tyMax", s.touchYMax);
  preferences.end();
  return n > 0;
}

bool settingsLoad() {
  settingsApplyBoardTouchDefaults();
  bool loaded = false;

  if (mountSD() && SD.exists(SETTINGS_PATH)) {
    File f = SD.open(SETTINGS_PATH, FILE_READ);
    if (f) {
      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        auto& s = g_settings;
        s.brightness      = doc["brightness"]      | s.brightness;
        s.theme           = (Theme)(uint8_t)(doc["theme"] | (uint8_t)s.theme);
        s.accentColor     = accentPresetClamp(doc["accentColor"] | s.accentColor);
        s.neopixelEnabled = doc["neopixelEnabled"] | s.neopixelEnabled;

        s.autoWifiScan    = doc["autoWifiScan"]    | s.autoWifiScan;
        s.autoBleScan     = doc["autoBleScan"]     | s.autoBleScan;

        if (s.autoWifiScan != s.autoBleScan) {
          bool en = (s.autoWifiScan || s.autoBleScan);
          s.autoWifiScan = en;
          s.autoBleScan  = en;
        }

        if (settingsTouchSavedForBoard(doc)) {
          JsonObjectConst touch = doc["touch"];
          s.touchXMin = touch["xMin"] | s.touchXMin;
          s.touchXMax = touch["xMax"] | s.touchXMax;
          s.touchYMin = touch["yMin"] | s.touchYMin;
          s.touchYMax = touch["yMax"] | s.touchYMax;
        } else {
          settingsApplyBoardTouchDefaults();
        }
        loaded = true;
      }
    }
  }

  // Fall back to NVS when SD card is missing or the JSON file is missing/invalid.
  // This is the primary reason settings had to work without a TF card.
  if (!loaded) {
    loadFromPreferences();
  }

  return loaded;
}

bool settingsSave() {
  // Always persist to NVS first. If the SD card is not present the user
  // still gets the behaviour they had in 1.5.9.
  bool nvsOk = saveToPreferences();

  if (!ensureDir("/config")) {
    sd_mounted = false;
    if (!ensureDir("/config")) return nvsOk;
  }

  File f = SD.open(SETTINGS_PATH, FILE_WRITE);
  if (!f) {
    sd_mounted = false;
    if (!mountSD()) return nvsOk;
    f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (!f) return nvsOk;
  }

  auto& s = g_settings;
  StaticJsonDocument<512> doc;
  doc["board"]           = TOUCH_PROFILE_ID;
  doc["brightness"]      = s.brightness;
  doc["theme"]           = (uint8_t)s.theme;
  doc["accentColor"]     = s.accentColor;
  doc["neopixelEnabled"] = s.neopixelEnabled;

  doc["autoWifiScan"]    = s.autoWifiScan;
  doc["autoBleScan"]     = s.autoBleScan;

  JsonObject t = doc.createNestedObject("touch");
  t["xMin"] = s.touchXMin;
  t["xMax"] = s.touchXMax;
  t["yMin"] = s.touchYMin;
  t["yMax"] = s.touchYMax;

  bool sdOk = serializeJson(doc, f) > 0;
  f.close();
  return nvsOk || sdOk;
}

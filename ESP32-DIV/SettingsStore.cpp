#include <ArduinoJson.h>
#include <SD.h>
#include "SettingsStore.h"


static AppSettings g_settings;
AppSettings& settings() { return g_settings; }

static bool sd_mounted = false;
static bool mountSD() {

  if (sd_mounted) {
    if (SD.exists("/")) return true;
    sd_mounted = false;
  }
  #ifdef SD_CS
  if (SD.begin(SD_CS)) { sd_mounted = true; return true; }
  #endif
  #ifdef SD_CS_PIN

  #ifdef CC1101_CS
  if (SD_CS_PIN != CC1101_CS) {
    if (SD.begin(SD_CS_PIN)) { sd_mounted = true; return true; }
  }
  #else
  if (SD.begin(SD_CS_PIN)) { sd_mounted = true; return true; }
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

bool settingsLoad() {
  if (!mountSD()) return false;
  if (!SD.exists(SETTINGS_PATH)) return true;

  File f = SD.open(SETTINGS_PATH, FILE_READ);
  if (!f) return false;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  auto& s = g_settings;
  s.brightness      = doc["brightness"]      | s.brightness;
  s.theme           = (Theme)(uint8_t)(doc["theme"] | (uint8_t)s.theme);
  s.neopixelEnabled = doc["neopixelEnabled"] | s.neopixelEnabled;

  s.autoWifiScan    = doc["autoWifiScan"]    | s.autoWifiScan;
  s.autoBleScan     = doc["autoBleScan"]     | s.autoBleScan;

  if (s.autoWifiScan != s.autoBleScan) {
    bool en = (s.autoWifiScan || s.autoBleScan);
    s.autoWifiScan = en;
    s.autoBleScan  = en;
  }

  s.touchXMin       = doc["touch"]["xMin"]   | s.touchXMin;
  s.touchXMax       = doc["touch"]["xMax"]   | s.touchXMax;
  s.touchYMin       = doc["touch"]["yMin"]   | s.touchYMin;
  s.touchYMax       = doc["touch"]["yMax"]   | s.touchYMax;

  return true;
}

bool settingsSave() {

  if (!ensureDir("/config")) {
    sd_mounted = false;
    if (!ensureDir("/config")) return false;
  }

  File f = SD.open(SETTINGS_PATH, FILE_WRITE);
  if (!f) {
    sd_mounted = false;
    if (!mountSD()) return false;
    f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (!f) return false;
  }

  auto& s = g_settings;
  StaticJsonDocument<512> doc;
  doc["brightness"]      = s.brightness;
  doc["theme"]           = (uint8_t)s.theme;
  doc["neopixelEnabled"] = s.neopixelEnabled;

  doc["autoWifiScan"]    = s.autoWifiScan;
  doc["autoBleScan"]     = s.autoBleScan;

  JsonObject t = doc.createNestedObject("touch");
  t["xMin"] = s.touchXMin;
  t["xMax"] = s.touchXMax;
  t["yMin"] = s.touchYMin;
  t["yMax"] = s.touchYMax;

  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

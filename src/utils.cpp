#include <SD.h>
#include <SPI.h>
#include "Touchscreen.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"


bool notificationVisible = false;
static bool notificationHasSave = false;
static int notifX, notifY, notifWidth, notifHeight;
static int closeButtonX, closeButtonY, closeButtonSize = 16;
static int okButtonX, okButtonY, okButtonWidth = 84, okButtonHeight = 24;
static int saveButtonX, saveButtonY, saveButtonWidth = 84, saveButtonHeight = 24;

static size_t decodeObfTo(char* out, size_t outSize, const uint8_t* in, size_t inLen, uint8_t key) {
  if (!out || outSize == 0) return 0;
  size_t n = inLen;
  if (n > outSize - 1) n = outSize - 1;
  for (size_t i = 0; i < n; ++i) out[i] = (char)(in[i] ^ key);
  out[n] = '\0';
  return n;
}

void tftPrintObf(const uint8_t* data, size_t len, uint8_t key) {
  char buf[96];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  tft.print(buf);
}

void tftPrintlnObf(const uint8_t* data, size_t len, uint8_t key) {
  char buf[96];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  tft.println(buf);
}

void serialPrintObf(const uint8_t* data, size_t len, bool newline, uint8_t key) {
  char buf[128];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  if (newline) Serial.println(buf);
  else Serial.print(buf);
}

static inline bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

static void drawNotificationInternal(const char* title, const char* message, bool showSave) {
    notificationHasSave = showSave;

    notifWidth = 200;
    notifHeight = showSave ? 112 : 98;
    notifX = (240 - notifWidth) / 2;
    notifY = (320 - notifHeight) / 2;

    tft.fillRect(notifX, notifY, notifWidth, notifHeight, LIGHT_GRAY);
    tft.drawRect(notifX, notifY, notifWidth, notifHeight, DARK_GRAY);
    tft.fillRect(notifX, notifY, notifWidth, 20, BLUE);

    tft.setTextColor(WHITE, BLUE);
    tft.setTextSize(1);
    tft.setCursor(notifX + 6, notifY + 5);
    tft.print(title);

    closeButtonX = notifX + notifWidth - closeButtonSize - 6;
    closeButtonY = notifY + 3;
    tft.fillRect(closeButtonX, closeButtonY, closeButtonSize, closeButtonSize, RED);
    tft.setTextColor(WHITE, RED);
    tft.setCursor(closeButtonX + 5, closeButtonY + 4);
    tft.print("X");

    int messageBoxX = notifX + 5;
    int messageBoxY = notifY + 25;
    int messageBoxWidth = notifWidth - 10;
    int messageBoxHeight = notifHeight - 25 - 30;
    tft.fillRect(messageBoxX, messageBoxY, messageBoxWidth, messageBoxHeight, WHITE);
    tft.setTextColor(BLACK, WHITE);

    {
      const int lineH = 12;
      const int maxY = messageBoxY + messageBoxHeight - lineH;
      String msg = message ? String(message) : String("");
      msg.trim();
      int cursorX = messageBoxX + 3;
      int cursorY = messageBoxY + 4;
      while (msg.length() > 0 && cursorY <= maxY) {
        int lineEnd = msg.length();
        while (lineEnd > 0 && tft.textWidth(msg.substring(0, lineEnd)) > (messageBoxWidth - 6)) {
          lineEnd--;
        }
        if (lineEnd <= 0) break;
        if (lineEnd < msg.length()) {
          int lastSpace = msg.substring(0, lineEnd).lastIndexOf(' ');
          if (lastSpace > 0) lineEnd = lastSpace;
        }
        tft.setCursor(cursorX, cursorY);
        tft.print(msg.substring(0, lineEnd));
        msg = msg.substring(lineEnd);
        msg.trim();
        cursorY += lineH;
      }

      if (msg.length() > 0 && cursorY > (messageBoxY + 4)) {
        tft.setCursor(cursorX, maxY);
        tft.print("...");
      }
    }

    int btnY = notifY + notifHeight - 25;
    if (showSave) {
      okButtonWidth = 72;
      saveButtonWidth = 72;
      okButtonHeight = saveButtonHeight = 20;
      okButtonX = notifX + 10;
      saveButtonX = notifX + notifWidth - 10 - saveButtonWidth;
      okButtonY = saveButtonY = btnY;

      tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
      tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
      tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
      tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);
      tft.setTextColor(BLACK, GRAY);
      tft.setCursor(okButtonX + 28, okButtonY + 5);
      tft.print("OK");

      tft.fillRect(saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight, GREEN);
      tft.drawRect(saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight, DARK_GRAY);
      tft.drawLine(saveButtonX, saveButtonY, saveButtonX + saveButtonWidth, saveButtonY, WHITE);
      tft.drawLine(saveButtonX, saveButtonY, saveButtonX, saveButtonY + saveButtonHeight, WHITE);
      tft.setTextColor(BLACK, GREEN);
      tft.setCursor(saveButtonX + 18, saveButtonY + 5);
      tft.print("SAVE");
    } else {
      okButtonWidth = 72;
      okButtonHeight = 20;
      okButtonX = notifX + (notifWidth - okButtonWidth) / 2;
      okButtonY = btnY;
      tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
      tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
      tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
      tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);
      tft.setTextColor(BLACK, GRAY);
      tft.setCursor(okButtonX + 28, okButtonY + 5);
      tft.print("OK");
    }

    notificationVisible = true;
}

void showNotificationActions(const char* title, const char* message, bool showSave) {
    drawNotificationInternal(title, message, showSave);
}

void showNotification(const char* title, const char* message) {
    drawNotificationInternal(title, message, false);
}

void hideNotification() {
    tft.fillRect(notifX, notifY, notifWidth, notifHeight, BLACK);
    notificationVisible = false;
}

bool isNotificationVisible() { return notificationVisible; }

NotificationAction notificationHandleTouch(int x, int y) {
  if (!notificationVisible) return NotificationAction::None;

  if (inRect(x, y, closeButtonX, closeButtonY, closeButtonSize, closeButtonSize)) {
    hideNotification();
    return NotificationAction::Close;
  }
  if (inRect(x, y, okButtonX, okButtonY, okButtonWidth, okButtonHeight)) {
    hideNotification();
    return NotificationAction::Ok;
  }
  if (notificationHasSave && inRect(x, y, saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight)) {
    hideNotification();
    return NotificationAction::Save;
  }
  return NotificationAction::None;
}

void printWrappedText(int x, int y, int maxWidth, const char* text) {
    String message = text;
    int cursorX = x, cursorY = y;

    while (message.length() > 0) {
        int lineEnd = message.length();

        while (tft.textWidth(message.substring(0, lineEnd)) > maxWidth) {
            lineEnd--;
        }

        if (lineEnd < message.length()) {
            int lastSpace = message.substring(0, lineEnd).lastIndexOf(' ');
            if (lastSpace > 0) lineEnd = lastSpace;
        }

        tft.setCursor(cursorX, cursorY);
        tft.print(message.substring(0, lineEnd));

        message = message.substring(lineEnd);
        message.trim();

        cursorY += 15;
    }
}

namespace FeatureUI {

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif

static inline uint16_t btnFill(ButtonStyle style, bool pressed, bool disabled) {
  if (disabled) return UI_LINE;
  if (pressed)  return UI_FG;
  switch (style) {
    case ButtonStyle::Primary:   return FEATURE_TEXT;
    case ButtonStyle::Secondary: return UI_FG;
    case ButtonStyle::Danger:    return UI_WARN;
  }
  return UI_FG;
}

static inline uint16_t btnBorder(ButtonStyle style, bool disabled) {
  if (disabled) return UI_LINE;
  switch (style) {
    case ButtonStyle::Primary:   return FEATURE_TEXT;
    case ButtonStyle::Secondary: return FEATURE_TEXT;
    case ButtonStyle::Danger:    return UI_WARN;
  }
  return FEATURE_TEXT;
}

static inline uint16_t btnText(ButtonStyle style, bool disabled) {
  if (disabled) return UI_LABLE;

  switch (style) {
    case ButtonStyle::Secondary: return WHITE;
    case ButtonStyle::Primary:
    case ButtonStyle::Danger:    return FEATURE_BG;
  }
  return WHITE;
}

void drawFooterBg() {

  tft.fillRect(0, tft.height() - FOOTER_H, tft.width(), FOOTER_H, FEATURE_BG);
}

void drawButtonRect(int x, int y, int w, int h,
                    const char* label,
                    ButtonStyle style,
                    bool pressed,
                    bool disabled,
                    uint8_t font) {
  if (!label) label = "";
  int r = h / 2;
  if (r > 6) r = 6;
  if (r < 0) r = 0;
  uint16_t fill = btnFill(style, pressed, disabled);
  uint16_t edge = btnBorder(style, disabled);
  uint16_t txt  = btnText(style, disabled);

  tft.fillRoundRect(x, y, w, h, r, fill);
  tft.drawRoundRect(x, y, w, h, r, edge);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(txt, fill);

  tft.drawString(label, x + w/2, y + h/2, font);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void layoutFooter3(Button (&btns)[3],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   const char* l2, ButtonStyle s2,
                   bool d0, bool d1, bool d2) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - 2*GAP_X) / 3;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  int x2 = x1 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
  btns[2] = {(int16_t)x2,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l2,s2,d2};
}

void layoutFooter2(Button (&btns)[2],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   bool d0, bool d1) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - GAP_X) / 2;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
}

void layoutFooter4(Button (&btns)[4],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   const char* l2, ButtonStyle s2,
                   const char* l3, ButtonStyle s3,
                   bool d0, bool d1, bool d2, bool d3) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - 3*GAP_X) / 4;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  int x2 = x1 + w + GAP_X;
  int x3 = x2 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
  btns[2] = {(int16_t)x2,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l2,s2,d2};
  btns[3] = {(int16_t)x3,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l3,s3,d3};
}

void layoutFooter1(Button& btn, const char* label, ButtonStyle style, bool disabled) {
  int x = PAD_X;
  int w = tft.width() - 2*PAD_X;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  btn = {(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)BTN_H,label,style,disabled};
}

int hit(const Button* btns, int n, int x, int y) {
  for (int i = 0; i < n; ++i) {
    const auto& b = btns[i];
    if (x >= b.x && x <= (b.x + b.w) && y >= b.y && y <= (b.y + b.h)) return i;
  }
  return -1;
}

}

unsigned long lastStatusBarUpdate = 0;
const int STATUS_BAR_UPDATE_INTERVAL = 2000;
float lastBatteryVoltage = 0.0;
bool sdCardPresent = false;
bool lastSdCardState = false;

#define BATTERY_PIN  2

const float R1 = 100000.0;
const float R2 = 100000.0;

float readBatteryVoltage() {
  const int sampleCount = 10;
  long sum = 0;

  for (int i = 0; i < sampleCount; i++) {
    sum += analogRead(2);
    delay(5);
  }

  float averageADC = sum / (float)sampleCount;

  float pinVoltage = (averageADC / 4095.0) * 2.2;

  float outputVoltage = pinVoltage * 2.0;

  return outputVoltage;
}

float readInternalTemperature() {
  float temperature = temperatureRead();
  return temperature;
}

void updateSdCardStatus() {

  bool cardDetected = !digitalRead(SD_CD);

  if (cardDetected != lastSdCardState) {
    sdCardPresent = cardDetected;
    lastSdCardState = cardDetected;

    Serial.println(sdCardPresent ? "SD Card detected" : "SD Card removed");
  }
}

void drawStatusBar(float batteryVoltage, bool forceUpdate) {
  static int lastBatteryPercentage = -1;
  static int lastWifiDevices      = -1;
  static int lastBleDevices       = -1;
  static String lastDisplayedTime = "";

  int batteryPercentage = ::map(batteryVoltage * 100, 300, 420, 0, 100);
  batteryPercentage = constrain(batteryPercentage, 0, 100);

  int wifiDevices = 0;
  int bleDevices  = 0;

  wifiDevices = WifiScan::getLastCount();
  bleDevices  = BleScan::getLastCount();

  float internalTemp = readInternalTemperature();

  if (batteryPercentage != lastBatteryPercentage ||
      wifiDevices      != lastWifiDevices      ||
      bleDevices       != lastBleDevices       ||
      forceUpdate) {
    int barHeight = 20;
    int x = 7;
    int y = 4;

    tft.fillRect(0, 0, tft.width(), barHeight, UI_LABLE);

    tft.drawRoundRect(x, y, 22, 10, 2, TFT_WHITE);
    tft.fillRect(x + 22, y + 3, 2, 4, TFT_WHITE);

    int batteryLevelWidth = ::map(batteryPercentage, 0, 100, 0, 20);
    uint16_t batteryColor = (batteryPercentage > 20) ? TFT_GREEN : TFT_RED;
    tft.fillRoundRect(x + 2, y + 2, batteryLevelWidth, 6, 1, batteryColor);

    tft.setCursor(x + 30, y + 2);
    tft.setTextColor(TFT_GREEN, UI_LABLE);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print(String(batteryPercentage) + "%");

    const int iconW         = 16;
    const int gap           = 3;
    const int wifiBarsWidth = 24;

    const int bleIconX      = 130;
    const int bleTextX      = bleIconX + iconW + gap;
    const int wifiBarsX     = bleTextX + 12 + gap;
    const int tempIconX     = wifiBarsX + wifiBarsWidth + gap;
    const int sdIconX       = tempIconX + iconW + gap;
    int iconY               = y - 2;

    int clearWidth = tft.width() - bleIconX;
    tft.fillRect(bleIconX, 0, clearWidth, barHeight, UI_LABLE);

    uint16_t wifiColor = (wifiDevices > 0) ? TFT_GREEN : TFT_WHITE;
    uint16_t bleColor  = (bleDevices  > 0) ? TFT_CYAN  : TFT_WHITE;

    int wifiStrength = 0;
    if (wifiDevices > 0) {

      wifiStrength = constrain(::map(wifiDevices, 0, 15, 0, 100), 0, 100);
    }

    int wifiX = wifiBarsX + 10;
    int wifiY = y + 11;

    for (int i = 0; i < 4; i++) {
      int barHeight = (i + 1) * 3;
      int barWidth  = 4;
      int barX      = wifiX + i * 6;

      if (wifiStrength > i * 25) {
        tft.fillRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_GREEN);
      } else {
        tft.drawRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_WHITE);
      }
    }

    tft.drawBitmap(bleIconX + 25, iconY, bitmap_icon_ble, 16, 16, bleColor);
    tft.setTextColor(bleColor, UI_LABLE);

    if (internalTemp == 53.33f) {
      tft.drawBitmap(tempIconX + 10, y - 2, bitmap_icon_temp, 16, 16, TFT_YELLOW);
    } else if (internalTemp > 55) {
      tft.drawBitmap(tempIconX + 10, y - 2, bitmap_icon_temp, 16, 16, TFT_RED);
    } else {
      tft.drawBitmap(tempIconX + 10, y - 2, bitmap_icon_temp, 16, 16, TFT_GREEN);
    }

    if (sdCardPresent) {
      tft.drawBitmap(sdIconX + 10, y - 2, bitmap_icon_sdcard, 16, 16, TFT_GREEN);
    } else {
      tft.drawBitmap(sdIconX + 10, y - 2, bitmap_icon_nullsdcard, 16, 16, TFT_RED);
    }

    lastBatteryPercentage = batteryPercentage;
    lastWifiDevices       = wifiDevices;
    lastBleDevices        = bleDevices;
  }
}

static TaskHandle_t statusBarTaskHandle = nullptr;
static volatile bool statusBarDirty = true;

static void statusBarTask(void* ) {
  for (;;) {

    float v = readBatteryVoltage();
    updateSdCardStatus();
    currentBatteryVoltage = v;
    statusBarDirty = true;
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void startStatusBarTask() {
  if (statusBarTaskHandle != nullptr) return;
  xTaskCreatePinnedToCore(
    statusBarTask,
    "statusBar",
    2048,
    nullptr,
    1,
    &statusBarTaskHandle,
    0
  );
}

void updateStatusBar() {

  if (statusBarTaskHandle != nullptr) {
    if (statusBarDirty) {
      drawStatusBar(currentBatteryVoltage, false);
      statusBarDirty = false;
    }
    return;
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastStatusBarUpdate > STATUS_BAR_UPDATE_INTERVAL) {
    float batteryVoltage = readBatteryVoltage();
    updateSdCardStatus();
    if (fabs(batteryVoltage - lastBatteryVoltage) > 0.05 || lastBatteryVoltage == 0) {
      drawStatusBar(batteryVoltage, false);
      lastBatteryVoltage = batteryVoltage;
    }
    lastStatusBarUpdate = currentMillis;
  }
}

void initSDCard() {

  pinMode(SD_CD, INPUT_PULLUP);

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  updateSdCardStatus();
}

bool isSDCardAvailable() {

  #ifdef SD_CD
  updateSdCardStatus();
  if (!sdCardPresent) return false;
  #endif

  static bool sdMounted = false;
  if (sdMounted) {

    if (SD.exists("/")) return true;
    sdMounted = false;
  }

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
  if (SD.begin(SD_CS)) { sdMounted = true; return true; }
  #endif

  #ifdef SD_CS_PIN
  #ifdef CC1101_CS
  if (SD_CS_PIN != CC1101_CS) {
    if (SD.begin(SD_CS_PIN)) { sdMounted = true; return true; }
  }
  #else
  if (SD.begin(SD_CS_PIN)) { sdMounted = true; return true; }
  #endif
  #endif

  return false;
}

void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center) {
  int16_t bitmapWidth = 100;
  int16_t bitmapHeight = 120;
  int16_t logoX = x;
  int16_t logoY = y;

  if (center) {
    int16_t screenWidth = tft.width();
    int16_t screenHeight = tft.height();
    logoX = (screenWidth - bitmapWidth) / 2;
    logoY = (screenHeight - bitmapHeight) / 2;
  }

  const unsigned char* bitmaps[] = {
    bitmap_icon_skull_loading_1,
    bitmap_icon_skull_loading_2,
    bitmap_icon_skull_loading_3,
    bitmap_icon_skull_loading_4,
    bitmap_icon_skull_loading_5,
    bitmap_icon_skull_loading_6,
    bitmap_icon_skull_loading_7,
    bitmap_icon_skull_loading_8,
    bitmap_icon_skull_loading_9,
    bitmap_icon_skull_loading_10
  };
  const int numFrames = 10;

  for (int r = 0; r < repeats; r++) {
    for (int i = 0; i < numFrames; i++) {
      tft.fillRect(logoX, logoY, bitmapWidth, bitmapHeight, TFT_BLACK);
      tft.drawBitmap(logoX, logoY, bitmaps[i], bitmapWidth, bitmapHeight, color);
      delay(frameDelay);
    }
  }
}

void displayLogo(uint16_t color, int displayTime) {
  int16_t bitmapWidth = 150;
  int16_t bitmapHeight = 150;
  int16_t screenWidth = tft.width();
  int16_t screenHeight = tft.height();
  int16_t logoX = (screenWidth - bitmapWidth) / 2;
  int16_t logoY = (screenHeight - bitmapHeight) / 2 - 20;

  tft.fillRect(logoX, logoY, bitmapWidth, bitmapHeight, TFT_BLACK);
  tft.drawBitmap(logoX, logoY, bitmap_icon_cifer, bitmapWidth, bitmapHeight, color);

  tft.setTextColor(color);
  tft.setTextFont(1);

  tft.setTextSize(2);
  int16_t textX = screenWidth / 3.5;
  int16_t textY = logoY + bitmapHeight + 10;
  tft.setCursor(textX, textY);
  tftPrintObf(OBF_PN, sizeof(OBF_PN));

  tft.setTextSize(1);
  textX = screenWidth / 3.5;
  textY += 20;
  tft.setCursor(textX, textY);
  tft.print("by ");
  tftPrintObf(OBF_DN, sizeof(OBF_DN));

  textX = screenWidth / 2.5;
  textY += 50;
  tft.setCursor(textX, textY);
  // Version is intentionally NOT obfuscated.
  tft.print(ESP32DIV_VERSION);

  Serial.println("==================================");
  serialPrintObf(OBF_PN, sizeof(OBF_PN), true);
  Serial.print("Developed by: "); serialPrintObf(OBF_DN, sizeof(OBF_DN), true);
  // Version is intentionally NOT obfuscated.
  Serial.print("Version:      "); Serial.println(ESP32DIV_VERSION);
  Serial.print("Contact:      "); serialPrintObf(OBF_EM, sizeof(OBF_EM), true);
  Serial.print("GitHub:       "); serialPrintObf(OBF_GH, sizeof(OBF_GH), true);
  Serial.print("Website:      "); serialPrintObf(OBF_WB, sizeof(OBF_WB), true);
  Serial.println("==================================");

  delay(displayTime);
}

namespace Terminal {

#define TEXT_HEIGHT 16
#define BOT_FIXED_AREA 0
#define TOP_FIXED_AREA 86
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define SCREEN_WIDTH 240
#define SCREENHEIGHT 320

static bool uiDrawn = false;

uint16_t yStart = TOP_FIXED_AREA;
uint16_t yArea = DISPLAY_HEIGHT - TOP_FIXED_AREA - BOT_FIXED_AREA;
uint16_t yDraw = DISPLAY_HEIGHT - BOT_FIXED_AREA - TEXT_HEIGHT;

uint16_t xPos = 0;

byte data = 0;

boolean change_colour = 1;
boolean selected = 1;
boolean terminalActive = true;

int blank[19];

long baudRates[] = {9600, 19200, 38400, 57600, 115200};
byte baudIndex = 0;

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 3

    static int iconX[ICON_NUM] = {210, 170, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_power,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            }
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
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
                  if (terminalActive) {
                    terminalActive = false;
                  } else if (!terminalActive) {
                    baudIndex = (baudIndex + 1) % 5;
                    Serial.end();
                    delay(100);
                    Serial.begin(baudRates[baudIndex]);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
                    tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
                    delay(10);
                  }
                    break;
                case 1:
                    delay(10);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    tft.drawCentreString(" Serial Terminal Active ", DISPLAY_WIDTH / 2, 37, 2);
                    terminalActive = true;
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
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {
                            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                            animationState = 1;
                            activeIcon = i;
                            lastAnimationTime = millis();
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void scrollAddress(uint16_t vsp) {
  tft.writecommand(ILI9341_VSCRSADD);
  tft.writedata(vsp >> 8);
  tft.writedata(vsp);
}

int scroll_line() {
  int yTemp = yStart;
  tft.fillRect(0, yStart, blank[(yStart - TOP_FIXED_AREA) / TEXT_HEIGHT], TEXT_HEIGHT, TFT_BLACK);

  yStart += TEXT_HEIGHT;
  if (yStart >= DISPLAY_HEIGHT - BOT_FIXED_AREA) yStart = TOP_FIXED_AREA + (yStart - DISPLAY_HEIGHT + BOT_FIXED_AREA);
  scrollAddress(yStart);
  delay(1);
  return yTemp;
}

void setupScrollArea(uint16_t tfa, uint16_t bfa) {
  tft.writecommand(ILI9341_VSCRDEF);
  tft.writedata(tfa >> 8);
  tft.writedata(tfa);
  tft.writedata((DISPLAY_HEIGHT - tfa - bfa) >> 8);
  tft.writedata(DISPLAY_HEIGHT - tfa - bfa);
  tft.writedata(bfa >> 8);
  tft.writedata(bfa);
}

void terminalSetup() {

  setupTouchscreen();
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_WHITE);
  String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
  tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);

  float currentBatteryVoltage = readBatteryVoltage();

  drawStatusBar(currentBatteryVoltage, true);

  uiDrawn = false;

  Serial.begin(baudRates[baudIndex]);

  setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);

  for (byte i = 0; i < 19; i++) blank[i] = 0;

}

void terminalLoop() {

  updateStatusBar();
  runUI();

  if (terminalActive) {
    byte charCount = 0;
    while (Serial.available() && charCount < 10) {
      data = Serial.read();
      if (data == '\r' || xPos > 231) {
        xPos = 0;
        yDraw = scroll_line();
      }
      if (data > 31 && data < 128) {
        xPos += tft.drawChar(data, xPos, yDraw, 2);
        blank[(18 + (yStart - TOP_FIXED_AREA) / TEXT_HEIGHT) % 19] = xPos;
      }
      charCount++;
      }
    }
  }
}

namespace AppSettingsUI {

static const int SCREEN_W = 240;
static const int BAR_H    = 22;
static const int TITLE_Y  = BAR_H + 4;
static const int TITLE_H  = 16;
static const int ROW_H    = 32;
static const int GAP_Y    = 4;
static const int PAD_X    = 12;
static const int LABEL_W  = 92;
static const int RADIUS   = 8;

static inline int rowY(int i) { return TITLE_Y + TITLE_H + 6 + i * (ROW_H + GAP_Y); }

struct Rect { int x,y,w,h; };
static inline Rect makeRect(int x,int y,int w,int h){ return {x,y,w,h}; }
static inline void fillRound(const Rect& r, uint16_t c){ tft.fillRoundRect(r.x,r.y,r.w,r.h,RADIUS,c); }
static inline void drawRound(const Rect& r, uint16_t c){ tft.drawRoundRect(r.x,r.y,r.w,r.h,RADIUS,c); }

static uint16_t cardBG, cardEdge, textDim, textStrong, accentSoft, btnBg, btnFg, btnStroke;

static uint16_t blend565(uint16_t a, uint16_t b, uint8_t k ) {
  uint32_t rb = (((a & 0xF81F) * (255-k)) + ((b & 0xF81F) * k)) >> 8;
  uint32_t g  = (((a & 0x07E0) * (255-k)) + ((b & 0x07E0) * k)) >> 8;
  return (rb & 0xF81F) | (g & 0x07E0);
}
static void buildPalette() {

  cardBG     = UI_FG;
  cardEdge   = UI_LINE;
  textDim    = UI_TEXT;
  textStrong = UI_TEXT;
  accentSoft = UI_ICON;

  btnBg      = UI_BG;
  btnFg      = UI_FG;
  btnStroke  = UI_LINE;
}

static int  sel = 0;
static bool dirtySettings = false;
static bool uiDirty = false;

static const char* items[] = {"Brightness", "Theme", "NeoPixel", "Auto Scan"};
static const int N = sizeof(items)/sizeof(items[0]);

static uint8_t  last_brightness;
static Theme    last_theme;
static bool     last_neopixel;
static bool     last_autoScan;
static int      last_sel;

static bool dragging = false;

static uint32_t lastChangeMs = 0;

static Rect rowRect(int i) { return makeRect(PAD_X, rowY(i), SCREEN_W - PAD_X*2, ROW_H); }

static void setTitleFont() { tft.setTextFont(2); }
static void setLabelFont() { tft.setTextFont(2); }

static void drawTitle() {
  setTitleFont();
  tft.setTextColor(textStrong, UI.bg);
  tft.setCursor(PAD_X, TITLE_Y);
  tft.print("Settings");
}

static void drawCardStatic(int i, bool selected) {
  Rect r = rowRect(i);

  tft.fillRect(0, r.y, SCREEN_W, r.h, UI_BG);

  if (selected) {
    tft.fillRect(0, r.y, 3, r.h, UI.accent);
  }

  setLabelFont();
  tft.setTextColor(textDim, UI_BG);
  int ty = r.y + (r.h/2 - 6);
  tft.setCursor(r.x, ty);
  tft.print(items[i]);

  tft.drawLine(PAD_X, r.y + r.h - 1, SCREEN_W - PAD_X, r.y + r.h - 1, UI_LINE);
}

static void wipeBrightnessWidgetArea() {
  Rect r = rowRect(0);
  tft.fillRect(r.x + LABEL_W, r.y + 2, r.w - LABEL_W - 6, r.h - 4, UI_BG);
}
static void wipeThemeWidgetArea() {
  Rect r = rowRect(1);
  tft.fillRect(r.x + LABEL_W, r.y + 2, r.w - LABEL_W - 6, r.h - 4, UI_BG);
}
static void wipeSwitchWidgetArea(int row) {
  Rect r = rowRect(row);
  tft.fillRect(r.x + LABEL_W, r.y + 2, r.w - LABEL_W - 6, r.h - 4, UI_BG);
}

static Rect rBrightTrack(){
  Rect r = rowRect(0);
  int left  = r.x + LABEL_W + 6;
  int right = r.x + r.w - 44;
  int w     = max(70, right - left);
  return makeRect(left, r.y + (r.h/2 - 8), w, 16);
}
static Rect rBrightKnob(uint8_t v) {
  Rect tr = rBrightTrack();
  int d = 18;
  int x = tr.x + (int)((uint32_t)v * (tr.w - d) / 255u);
  int y = tr.y + tr.h/2 - d/2;
  return makeRect(x, y, d, d);
}
static void drawBrightnessWidget(uint8_t v, bool selected) {
  tft.startWrite();
  wipeBrightnessWidgetArea();

  Rect tr = rBrightTrack();

  tft.fillRoundRect(tr.x, tr.y, tr.w, tr.h, tr.h/2, UI_BG);
  tft.drawRoundRect(tr.x, tr.y, tr.w, tr.h, tr.h/2, cardEdge);

  int fw = (int)((uint32_t)v * tr.w / 255u);
  tft.fillRoundRect(tr.x, tr.y, fw+2, tr.h, tr.h/2, UI_ICON);

  Rect kb = rBrightKnob(v);
  tft.fillCircle(kb.x + kb.w/2, kb.y + kb.h/2, kb.w/2, UI_FG);
  tft.drawCircle(kb.x + kb.w/2, kb.y + kb.h/2, kb.w/2, selected ? UI_ICON : UI_FG);

  int bx = tr.x + tr.w + 6, by = tr.y - 2, bw = 34, bh = tr.h + 4;
  tft.fillRoundRect(bx+1, by+1, bw, bh, 4, blend565(UI_BG, UI_FG, 28));
  tft.fillRoundRect(bx,   by,   bw, bh, 4, cardBG);
  tft.drawRoundRect(bx,   by,   bw, bh, 4, cardEdge);
  setLabelFont();
  tft.setTextColor(textStrong, cardBG);
  char buf[8]; snprintf(buf,sizeof(buf),"%3u", v);
  tft.setCursor(bx+5, by+2);
  tft.print(buf);

  tft.endWrite();
}
static void drawBrightness(uint8_t v, bool selected) {
  drawCardStatic(0, selected);
  drawBrightnessWidget(v, selected);
}

static Rect rThemeDark()  { Rect r=rowRect(1); return makeRect(r.x + LABEL_W + 6,   r.y + 8, 50, r.h-16); }
static Rect rThemeLight() { Rect r=rowRect(1); return makeRect(r.x + LABEL_W + 6+54, r.y + 8, 50, r.h-16); }

static void drawThemeWidget(Theme th, bool ) {

  tft.startWrite();
  wipeThemeWidgetArea();

  Rect r = rowRect(1);
  int baseX = r.x + LABEL_W + 6;
  int ty    = r.y + (r.h/2 - 6);

  setLabelFont();
  tft.setTextColor(textStrong, UI_BG);
  tft.setCursor(baseX, ty);

  if (th == Theme::Dark) {
    tft.print("[Dark]  Light");
  } else {
    tft.print("Dark  [Light]");
  }

  tft.endWrite();
}
static void drawTheme(Theme th, bool selected) {
  drawCardStatic(1, selected);
  drawThemeWidget(th, selected);
}

static Rect rSwitchTrack(int row){
  Rect r = rowRect(row);
  const int w = 34;
  const int h = 14;
  int x = r.x + r.w - w - 10;
  int y = r.y + (r.h - h) / 2;
  return makeRect(x, y, w, h);
}
static Rect rSwitchKnob(bool on, int row) {
  Rect tr = rSwitchTrack(row);
  int d = tr.h - 4;
  int x = on ? (tr.x + tr.w - d - 2)
             : (tr.x + 2);
  int y = tr.y + (tr.h - d) / 2;
  return makeRect(x, y, d, d);
}
static void drawSwitchWidgetRow(bool on, bool , int row) {
  tft.startWrite();
  wipeSwitchWidgetArea(row);

  Rect tr = rSwitchTrack(row);

  tft.fillRoundRect(tr.x, tr.y, tr.w, tr.h, tr.h/2, UI_BG);
  tft.drawRoundRect(tr.x, tr.y, tr.w, tr.h, tr.h/2, cardEdge);

  if (on) {
    tft.fillRoundRect(tr.x+1, tr.y+1, tr.w-2, tr.h-2, tr.h/2, UI_ICON);
  }

  Rect kb = rSwitchKnob(on, row);
  uint16_t knobBody  = UI_FG;
  uint16_t knobEdge  = on ? UI.accent : UI_FG;
  tft.fillCircle(kb.x+kb.w/2, kb.y+kb.h/2, kb.w/2, knobBody);
  tft.drawCircle(kb.x+kb.w/2, kb.y+kb.h/2, kb.w/2, knobEdge);

  setLabelFont();
  tft.setTextColor(textStrong, UI_BG);
  int labelX = tr.x - 26;
  int labelY = tr.y + 1;
  tft.setCursor(labelX, labelY);
  tft.print(on ? "ON" : "OFF");

  tft.endWrite();
}
static void drawSwitchRow(bool on, bool selected, int row) {
  drawCardStatic(row, selected);
  drawSwitchWidgetRow(on, selected, row);
}

static void drawNeoPixel(bool on, bool selected) { drawSwitchRow(on, selected, 2); }
static void drawAutoScan(bool on, bool selected) { drawSwitchRow(on, selected, 3); }

static Rect backRect(){
  int h = tft.height();
  int bwTotal = SCREEN_W - PAD_X*2;
  int bh = 24;
  int bx = PAD_X;
  int by = h - bh - 8;
  const int gap = 8;
  int bw = (bwTotal - gap) / 2;
  return makeRect(bx, by, bw, bh);
}
static Rect saveRect(){
  Rect b = backRect();
  const int gap = 8;
  return makeRect(b.x + b.w + gap, b.y, b.w, b.h);
}
static void drawFooterButton(const Rect& b, const char* label, uint16_t body, uint16_t edge){

  FeatureUI::ButtonStyle style =
    (body == UI.accent) ? FeatureUI::ButtonStyle::Primary : FeatureUI::ButtonStyle::Secondary;
  FeatureUI::drawButtonRect(b.x, b.y, b.w, b.h, label, style);
}
static void footerToast(const char* msg, uint16_t color){
  Rect b = backRect();
  int y = b.y - 18;
  int h = 14;
  tft.fillRect(PAD_X, y, SCREEN_W - PAD_X*2, h, UI_BG);
  tft.setTextColor(color, UI_BG);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setCursor(PAD_X, y + 3);
  tft.print(msg);
}
static void drawFooter(bool backPressed=false, bool savePressed=false){
  Rect b = backRect();
  Rect s = saveRect();

  int clearX = PAD_X - 2;
  int clearW = SCREEN_W - (PAD_X*2) + 4;
  tft.fillRect(clearX, b.y-2, clearW, b.h+4, UI.bg);

  FeatureUI::drawButtonRect(b.x, b.y, b.w, b.h, "Back",
                            backPressed ? FeatureUI::ButtonStyle::Primary : FeatureUI::ButtonStyle::Secondary);

  FeatureUI::drawButtonRect(s.x, s.y, s.w, s.h, "Save",
                            FeatureUI::ButtonStyle::Primary);
}
static bool touchInRect(const Rect& r, int x, int y) {
  return (x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h);
}

static void drawAll() {
  tft.fillScreen(UI_BG);
  buildPalette();

  drawStatusBar(currentBatteryVoltage, true);
  setTitleFont();
  drawTitle();

  auto& s = settings();
  drawBrightness(s.brightness, sel==0);
  drawTheme(s.theme, sel==1);
  drawNeoPixel(s.neopixelEnabled, sel==2);
  bool autoScan = (s.autoWifiScan || s.autoBleScan);
  drawAutoScan(autoScan, sel==3);

  drawFooter(false, false);

  last_sel        = sel;
  last_brightness = s.brightness;
  last_theme      = s.theme;
  last_neopixel   = s.neopixelEnabled;
  last_autoScan     = autoScan;
  uiDirty = false;
}

static void redrawIfChanged() {
  auto& s = settings();

  if (s.theme != last_theme) {
    applyThemeToPalette(s.theme);
    buildPalette();
    drawAll();
    return;
  }

  if (sel != last_sel) {
    drawCardStatic(0, sel==0);  drawBrightnessWidget(s.brightness, sel==0);
    drawCardStatic(1, sel==1);  drawThemeWidget(s.theme, sel==1);
    drawCardStatic(2, sel==2);  drawSwitchWidgetRow(s.neopixelEnabled, sel==2, 2);
    bool autoScan = (s.autoWifiScan || s.autoBleScan);
    drawCardStatic(3, sel==3);  drawSwitchWidgetRow(autoScan, sel==3, 3);
    last_sel = sel;
  } else {
    if (s.brightness != last_brightness) {
      drawBrightnessWidget(s.brightness, sel==0);
      last_brightness = s.brightness;
    }
    if (s.neopixelEnabled != last_neopixel) {
      drawSwitchWidgetRow(s.neopixelEnabled, sel==2, 2);
      last_neopixel = s.neopixelEnabled;
    }
    bool autoScan = (s.autoWifiScan || s.autoBleScan);
    if (autoScan != last_autoScan) {
      drawSwitchWidgetRow(autoScan, sel==3, 3);
      last_autoScan = autoScan;
    }
    if (s.theme != last_theme) {
      drawThemeWidget(s.theme, sel==1);
      last_theme = s.theme;
    }
  }

  uiDirty = false;
}

static bool applyBrightness(uint8_t v){
  if (v > 255) v = 255;
  auto& s = settings();
  if (s.brightness == v) return false;
  s.brightness = v;
  ::setBrightness(v);
  dirtySettings = true;
  uiDirty = true;
  lastChangeMs = millis();
  return true;
}
static bool applyTheme(Theme t){
  auto& s = settings();
  if (s.theme == t) return false;
  s.theme = t;
  applyThemeToPalette(t);
  buildPalette();
  dirtySettings = true;
  uiDirty = true;
  lastChangeMs = millis();
  return true;
}
static bool applyNeoPixel(bool en){
  auto& s = settings();
  if (s.neopixelEnabled == en) return false;
  s.neopixelEnabled = en;
  dirtySettings = true;
  uiDirty = true;
  lastChangeMs = millis();
  return true;
}

static bool applyAutoScan(bool en){
  auto& s = settings();
  if (s.autoWifiScan == en && s.autoBleScan == en) return false;
  s.autoWifiScan = en;
  s.autoBleScan  = en;
  dirtySettings = true;
  uiDirty = true;
  lastChangeMs = millis();
  return true;
}

static void handleTouch() {
  int tx, ty;
  static uint32_t lastToggleMs = 0;
  if (!readTouchXY(tx, ty)) { dragging = false; return; }

  Rect br = backRect();
  Rect sr = saveRect();
  if (touchInRect(br, tx, ty)) {
    drawFooter(true, false);
    delay(25);
    drawFooter(false, false);

    feature_exit_requested = true;
    return;
  }
  if (touchInRect(sr, tx, ty)) {
    drawFooter(false, true);
    delay(25);
    drawFooter(false, false);
    if (dirtySettings) {
      bool ok = settingsSave();
      if (ok) {
        dirtySettings = false;
        footerToast("Saved", UI.ok);
        delay(250);
        footerToast("      ", UI_BG);
      } else {
        footerToast("Save FAILED", UI.warn);
      }
    } else {
      footerToast("No changes", UI_TEXT);
      delay(250);
      footerToast("         ", UI_BG);
    }
    return;
  }

  for (int i=0;i<N;++i){
    Rect rr = rowRect(i);
    if (ty >= rr.y && ty <= rr.y+rr.h) { sel = i; break; }
  }

  auto& s = settings();

  if (sel == 0) {
    Rect tr = rBrightTrack();
    Rect kb = rBrightKnob(s.brightness);

    bool inTrack = (tx >= tr.x && tx <= tr.x+tr.w && ty >= tr.y-10 && ty <= tr.y+tr.h+10);
    bool inKnob  = (tx >= kb.x && tx <= kb.x+kb.w && ty >= kb.y && ty <= kb.y+kb.h);

    if (inTrack || inKnob) {
      dragging = true;
      long rel = tx - tr.x;
      if (rel < 0) rel = 0;
      if (rel > tr.w-1) rel = tr.w-1;
      uint8_t v = (uint8_t)((rel * 255L) / (tr.w - 1));
      applyBrightness(v);
    }
  } else if (sel == 1) {
    Rect d = rThemeDark();
    Rect l = rThemeLight();
    if (tx >= d.x && tx <= d.x+d.w && ty >= d.y && ty <= d.y+d.h) {
      applyTheme(Theme::Dark);
    } else if (tx >= l.x && tx <= l.x+l.w && ty >= l.y && ty <= l.y+l.h) {
      applyTheme(Theme::Light);
    }
  } else if (sel == 2) {
    Rect tr = rSwitchTrack(2);
    if (tx >= tr.x && tx <= tr.x+tr.w && ty >= tr.y-10 && ty <= tr.y+tr.h+10) {
      uint32_t now = millis();
      if (now - lastToggleMs > 120) {
        applyNeoPixel(!s.neopixelEnabled);
        lastToggleMs = now;
      }
    }
  } else if (sel == 3) {
    Rect tr = rSwitchTrack(3);
    if (tx >= tr.x && tx <= tr.x+tr.w && ty >= tr.y-10 && ty <= tr.y+tr.h+10) {
      uint32_t now = millis();
      if (now - lastToggleMs > 120) {
        bool autoScan = (s.autoWifiScan || s.autoBleScan);
        applyAutoScan(!autoScan);
        lastToggleMs = now;
      }
    }
  }
}

void setup(){

  applyThemeToPalette(settings().theme);
  buildPalette();
  ::setBrightness(settings().brightness);
  sel = 0; dirtySettings = false; uiDirty = false; dragging = false;
  drawAll();
}

void loop(){
  bool changedByButtons=false;

  static bool upWasDown     = false;
  static bool downWasDown   = false;
  static bool leftWasDown   = false;
  static bool rightWasDown  = false;
  static bool selectWasDown = false;
  static uint32_t lastNavMs = 0;
  static uint32_t lastActionMs = 0;
  const uint32_t NAV_DEBOUNCE_MS    = 140;
  const uint32_t ACTION_DEBOUNCE_MS = 140;

  uint32_t now = millis();

  bool upNow     = isButtonPressed(BTN_UP);
  bool downNow   = isButtonPressed(BTN_DOWN);
  bool leftNow   = isButtonPressed(BTN_LEFT);
  bool rightNow  = isButtonPressed(BTN_RIGHT);
  bool selectNow = isButtonPressed(BTN_SELECT);

  if (selectNow && !selectWasDown && (now - lastActionMs > ACTION_DEBOUNCE_MS)) {
    feature_exit_requested = true;
    lastActionMs = now;
    return;
  }

  if (upNow && !upWasDown && (now - lastNavMs > NAV_DEBOUNCE_MS)) {
    sel=(sel+N-1)%N; changedByButtons=true;
    lastNavMs = now;
  }
  if (downNow && !downWasDown && (now - lastNavMs > NAV_DEBOUNCE_MS)) {
    sel=(sel+1)%N;   changedByButtons=true;
    lastNavMs = now;
  }

  if (leftNow && !leftWasDown && (now - lastActionMs > ACTION_DEBOUNCE_MS)){
    auto& s=settings();
    if (sel==0 && s.brightness>0)      { applyBrightness(s.brightness>8? s.brightness-8:0); }
    else if (sel==1)                   { applyTheme(Theme::Dark); }
    else if (sel==2)                   { applyNeoPixel(false); }
    else if (sel==3)                   { applyAutoScan(false); }
    changedByButtons=true;
    lastActionMs = now;
  }
  if ((rightNow && !rightWasDown) && (now - lastActionMs > ACTION_DEBOUNCE_MS)){
    auto& s=settings();
    if (sel==0 && s.brightness<255)    { applyBrightness(s.brightness+8); }
    else if (sel==1)                   { applyTheme(Theme::Light); }
    else if (sel==2)                   { applyNeoPixel(true); }
    else if (sel==3)                   { applyAutoScan(true); }
    changedByButtons=true;
    lastActionMs = now;
  }

  upWasDown     = upNow;
  downWasDown   = downNow;
  leftWasDown   = leftNow;
  rightWasDown  = rightNow;
  selectWasDown = selectNow;

  handleTouch();

  if (changedByButtons || uiDirty) redrawIfChanged();

  delay(2);
}

}

namespace TouchCalib {
static int stepIdx = 0;
static uint16_t xs[4], ys[4];
static const int pts[4][2] = { {20,20}, {TFT_WIDTH-20,20}, {TFT_WIDTH-20,TFT_HEIGHT-20}, {20,TFT_HEIGHT-20} };

static void drawTarget(int x,int y){
  tft.fillScreen(UI_BG);
  tft.drawCircle(x,y,10,UI_ICON);
  tft.drawLine(x-14,y, x+14,y, UI_ICON);
  tft.drawLine(x,y-14, x,y+14, UI_ICON);
  tft.setCursor(70, 8);
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.print("Touch the target");
}

void setup(){
  stepIdx=0;
  drawTarget(pts[0][0], pts[0][1]);
}

void loop(){
  if (stepIdx>=4){
    uint16_t xMin = min(xs[0], xs[3]);
    uint16_t xMax = max(xs[1], xs[2]);
    uint16_t yMin = min(ys[0], ys[1]);
    uint16_t yMax = max(ys[2], ys[3]);
    auto& s = settings();
    s.touchXMin = xMin; s.touchXMax = xMax;
    s.touchYMin = yMax; s.touchYMax = yMin;

    bool ok = settingsSave();

    tft.fillScreen(UI_BG);
    tft.setTextColor(ok ? UI.ok : UI.warn, UI_BG);
    tft.setCursor(70, 8);
    tft.print(ok ? "Calibration Saved" : "Save FAILED");

    tft.setTextColor(UI_TEXT, UI_BG);
    tft.setCursor(70,28);
    tft.printf("X:[%u..%u] Y:[%u..%u]", xMin,xMax,yMin,yMax);

    delay(1200);
    feature_exit_requested = true;
    return;
  }

  if (ts.touched()){
    TS_Point p = ts.getPoint();
    xs[stepIdx]=p.x; ys[stepIdx]=p.y;
    stepIdx++;
    if (stepIdx<4) drawTarget(pts[stepIdx][0], pts[stepIdx][1]);
  }
  delay(100);
}
}

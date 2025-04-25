#include "utils.h"
#include "shared.h"
#include "icon.h"

// TFT and Touchscreen objects
extern TFT_eSPI tft;
extern XPT2046_Touchscreen ts;


/*
 * 
 * Notification
 * 
 */

extern bool notificationVisible;
extern int notifX, notifY, notifWidth, notifHeight;
extern int closeButtonX, closeButtonY, closeButtonSize;
extern int okButtonX, okButtonY, okButtonWidth, okButtonHeight;


void showNotification(const char* title, const char* message) {
    notifWidth = 200;
    notifHeight = 80;
    notifX = (240 - notifWidth) / 2;
    notifY = (320 - notifHeight) / 2;

    tft.fillRect(notifX, notifY, notifWidth, notifHeight, LIGHT_GRAY);
    tft.fillRect(notifX, notifY, notifWidth, 20, BLUE);
    
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(notifX + 5, notifY + 5);
    tft.print(title);

    closeButtonX = notifX + notifWidth - closeButtonSize - 5;
    closeButtonY = notifY + 2;
    tft.fillRect(closeButtonX, closeButtonY, closeButtonSize, closeButtonSize, RED);
    tft.setTextColor(WHITE);
    tft.setCursor(closeButtonX + 5, closeButtonY + 4);
    tft.print("X");

    int messageBoxX = notifX + 5;
    int messageBoxY = notifY + 25;
    int messageBoxWidth = notifWidth - 10;
    int messageBoxHeight = notifHeight - 45;

    tft.fillRect(messageBoxX, messageBoxY, messageBoxWidth, messageBoxHeight, WHITE);
    tft.setTextColor(BLACK);
    printWrappedText(messageBoxX + 2, messageBoxY + 5, messageBoxWidth + 2, message);

    okButtonX = notifX + (notifWidth - okButtonWidth) / 2;
    okButtonY = notifY + notifHeight - 25;

    tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
    tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
    tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
    tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);

    tft.setTextColor(BLACK);
    tft.setCursor(okButtonX + 20, okButtonY + 5);
    tft.print("OK");

    notificationVisible = true;
}

void hideNotification() {
    tft.fillRect(notifX, notifY, notifWidth, notifHeight, BLACK);
    notificationVisible = false;
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


/*
 * 
 * StatusBar
 * 
 */

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
uint8_t temprature_sens_read();

unsigned long lastStatusBarUpdate = 0;
const int STATUS_BAR_UPDATE_INTERVAL = 1000; 
float lastBatteryVoltage = 0.0;
bool sdAvailable = false;

float readBatteryVoltage() {
  uint8_t temprature_sens_read();
  const int sampleCount = 10;  
  long sum = 0;
  
  for (int i = 0; i < sampleCount; i++) {
    sum += analogRead(36);  
    delay(5);  
  }
  
  float averageADC = sum / sampleCount;
  float voltage = (averageADC / 4095.0) * 3.3 * 2;  
  return voltage;
}

float readInternalTemperature() {
  float temperature = ((temprature_sens_read() - 32) / 1.8); 
  return temperature;
}

// Check if SD card is available
bool isSDCardAvailable() {
  return SD.begin();
}

void drawStatusBar(float batteryVoltage, bool forceUpdate) {
  static int lastBatteryPercentage = -1;
  static int lastWiFiStrength = -1;
  static String lastDisplayedTime = "";

  int batteryPercentage = map(batteryVoltage * 100, 300, 420, 0, 100);
  batteryPercentage = constrain(batteryPercentage, 0, 100);

  int wifiStrength = random(0, 101);
  wifiStrength = constrain(wifiStrength, 0, 100);

  float internalTemp = readInternalTemperature();
  bool sdAvailable = false;
  //bool sdAvailable = isSDCardAvailable();

  if (batteryPercentage != lastBatteryPercentage || wifiStrength != lastWiFiStrength || forceUpdate) {
    int barHeight = 20;  // Status bar height
    int x = 7;           // Padding for battery icon
    int y = 4;           // Vertical offset

    // **Dark Background with Neon Green Edge**
    tft.fillRect(0, 0, tft.width(), barHeight, DARK_GRAY);
    //tft.fillRect(0, barHeight - 2, tft.width(), 3, ORANGE); 

    // **Draw Battery Icon (Hacker/Techy Look)**
    tft.drawRoundRect(x, y, 22, 10, 2, TFT_WHITE);        // Battery border
    tft.fillRect(x + 22, y + 3, 2, 4, TFT_WHITE);         // Battery terminal
    
    int batteryLevelWidth = map(batteryPercentage, 0, 100, 0, 20);
    uint16_t batteryColor = (batteryPercentage > 20) ? TFT_GREEN : TFT_RED;  
    tft.fillRoundRect(x + 2, y + 2, batteryLevelWidth, 6, 1, batteryColor);

    // **Display Battery Percentage (Neon Green)**
    tft.setCursor(x + 30, y + 2);
    tft.setTextColor(TFT_GREEN, DARK_GRAY);  
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print(String(batteryPercentage) + "%");

    // **Draw Wi-Fi Signal Bars (Neon Green)**
    int wifiX = 180;
    int wifiY = y + 11;
    for (int i = 0; i < 4; i++) {
      int barHeight = (i + 1) * 3;
      int barWidth = 4;
      int barX = wifiX + i * 6;
      if (wifiStrength > i * 25) {
        tft.fillRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_GREEN);  
      } else {
        tft.drawRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_WHITE);  
      }
    }

    if (internalTemp = 53.33) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_YELLOW);
    } else if (internalTemp > 55) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_RED);
    } else {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_GREEN);
    }

    // **Display SD Card Icon (If Available)**
    if (sdAvailable) {
      tft.drawBitmap(220, y - 3, bitmap_icon_sdcard, 16, 16, TFT_GREEN);
    } else {
      tft.drawBitmap(220, y - 3, bitmap_icon_nullsdcard, 16, 16, TFT_RED);
    }

    // **Bottom Line for Aesthetic (Neon Green)**
    //tft.drawLine(0, barHeight - 1, tft.width(), barHeight - 1, ORANGE);  

    // **Update Last Values**
    lastBatteryPercentage = batteryPercentage;
    lastWiFiStrength = wifiStrength;
  }
}

void updateStatusBar() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastStatusBarUpdate > STATUS_BAR_UPDATE_INTERVAL) {
    float batteryVoltage = readBatteryVoltage();

    if (abs(batteryVoltage - lastBatteryVoltage) > 0.05 || lastBatteryVoltage == 0) {
      drawStatusBar(batteryVoltage);
      lastBatteryVoltage = batteryVoltage;
    }

    lastStatusBarUpdate = currentMillis;
  }
}

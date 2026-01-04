#include "utils.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"


/*
 * 
 * Notification
 * 
 */


/*
    showNotification("New Message!", "Task Failed Successfully.");
    
    if (notificationVisible && ts.touched()) {
      int x, y, z;
        TS_Point p = ts.getPoint();
        x = ::map(p.x, 300, 3800, 0, 239);
        y = ::map(p.y, 3800, 300, 0, 319);
        
    if (x >= closeButtonX && x <= (closeButtonX + closeButtonSize) &&
        y >= closeButtonY && y <= (closeButtonY + closeButtonSize)) {
        hideNotification();
    }
    
    if (x >= okButtonX && x <= (okButtonX + okButtonWidth) &&
        y >= okButtonY && y <= (okButtonY + okButtonHeight)) {
        hideNotification();
    }
     delay(100);
  }
  
*/

bool notificationVisible = true;
int notifX, notifY, notifWidth, notifHeight;
int closeButtonX, closeButtonY, closeButtonSize = 15;
int okButtonX, okButtonY, okButtonWidth = 60, okButtonHeight = 20;

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


/*
 * 
 * Loading
 * 
 */

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

  // Array of bitmaps
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


/*
 * 
 * Display Logo
 * 
 */

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
  int16_t textWidth = tft.textWidth("ESP32-DIV", 2);
  int16_t textX = screenWidth / 3.5;
  int16_t textY = logoY + bitmapHeight + 10;
  tft.setCursor(textX, textY);
  tft.print("ESP32-DIV");

  tft.setTextSize(1);
  textWidth = tft.textWidth("by CiferTech", 1);
  textX = screenWidth / 3.5;
  textY += 20;
  tft.setCursor(textX, textY);
  tft.print("by CiferTech");

  textWidth = tft.textWidth("v1.1.0", 1);
  textX = screenWidth / 2.5;
  textY += 50;
  tft.setCursor(textX, textY);
  tft.print("v1.1.0");

  Serial.println("==================================");
  Serial.println("ESP32-DIV                         ");
  Serial.println("Developed by: CiferTech           ");
  Serial.println("Version:      1.1.0               ");
  Serial.println("Contact:      cifertech@gmail.com ");
  Serial.println("GitHub:       github.com/cifertech");
  Serial.println("Website:      CiferTech.net       ");
  Serial.println("==================================");

  delay(displayTime);
}


/*
 * 
 * Terminal
 * 
 */

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
        if (ts.touched() && feature_active) { 
            TS_Point p = ts.getPoint();
            int x = ::map(p.x, 300, 3800, 0, SCREEN_WIDTH - 1);
            int y = ::map(p.y, 3800, 300, 0, SCREENHEIGHT - 1);

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
  drawStatusBar(currentBatteryVoltage, false);

  uiDrawn = false;

  Serial.begin(baudRates[baudIndex]);

  setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);

  for (byte i = 0; i < 19; i++) blank[i] = 0;

}

void terminalLoop() {
  
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

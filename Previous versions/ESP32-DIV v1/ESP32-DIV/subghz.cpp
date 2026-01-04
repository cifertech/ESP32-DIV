#include "subconfig.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"

/*
 * ReplayAttack
 * 
 * 
 * 
 */

namespace replayat {

#define EEPROM_SIZE 1440
#define ADDR_VALUE 1280    // 4 bytes
#define ADDR_BITLEN 1284   // 2 bytes
#define ADDR_PROTO 1286    // 2 bytes
#define ADDR_FREQ 1288     // 4 bytes 

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define SCREEN_HEIGHT 320

static bool uiDrawn = false;

#define MAX_NAME_LENGTH 16 // Maximum length for profile name (including null terminator)

// Keyboard layout (4 rows)
const char* keyboardLayout[] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM<-" // '<' for backspace, '-' for clear
};

// Random name suggestions for shuffle
const char* randomNames[] = {
  "Signal", "Remote", "KeyFob", "GateOpener", "DoorLock",
  "RFTest", "Profile", "Control", "Switch", "Beacon"
};
const int numRandomNames = 10;
int randomNameIndex = 0;

// Keyboard dimensions
const int keyWidth = 22;
const int keyHeight = 22;
const int keySpacing = 2;
const int yOffsetStart = 95;

// Cursor blink state
static bool cursorState = true;
static unsigned long lastCursorBlink = 0;
const unsigned long cursorBlinkInterval = 500;

struct Profile {
    uint32_t frequency;
    unsigned long value;
    int bitLength;
    int protocol;
    char name[MAX_NAME_LENGTH]; // Custom name field
};

#define PROFILE_SIZE sizeof(Profile) // Size of the updated Profile struct
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES 5
#define ADDR_PROFILE_COUNT 0 

#define MAX_PROFILES 5         

int profileCount = 0;

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

#define RX_PIN 16         
#define TX_PIN 26 

#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_UP     6
#define BTN_DOWN   3

unsigned long receivedValue = 0; 
int receivedBitLength = 0;       
int receivedProtocol = 0;       
const int rssi_threshold = -75; 

static const uint32_t subghz_frequency_list[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000,  
    390000000, 418000000, 433075000, 433420000, 433920000, 434420000, 
    434775000, 438900000, 868350000, 915000000, 925000000
};

int currentFrequencyIndex = 0; 
int yshift = 20;


void updateDisplay() {
    uiDrawn = false;

    tft.fillRect(0, 40, 240, 40, TFT_BLACK);  
    tft.drawLine(0, 80, 240, 80, TFT_WHITE);

    // Frequency
    tft.setCursor(5, 20 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Freq:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(50, 20 + yshift);
    tft.print(subghz_frequency_list[currentFrequencyIndex] / 1000000.0, 2);
    tft.print(" MHz");
    
    // Bit Length
    tft.setCursor(5, 35 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Bit:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(50, 35 + yshift);
    tft.printf("%d", receivedBitLength);

    // RSSI
    tft.setCursor(130, 35 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("RSSI:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(170, 35 + yshift);
    tft.printf("%d", ELECHOUSE_cc1101.getRssi());    

    // Protocol
    tft.setCursor(130, 20 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Ptc:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(170, 20 + yshift);
    tft.printf("%d", receivedProtocol);

    // Received Value
    tft.setCursor(5, 50 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Val:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(50, 50 + yshift);
    tft.print(receivedValue);

    ELECHOUSE_cc1101.setSidle();  
    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
    ELECHOUSE_cc1101.SetRx();     
}


void drawInputField(String& inputName) {
  tft.fillRect(10, 55, 220, 25, TFT_DARKGREY);
  tft.drawRect(9, 54, 222, 27, ORANGE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(15, 60);
  String displayText = inputName;
  if (cursorState) {
    displayText += "|";
  }
  tft.println(displayText);
}

void drawKeyboard(String& inputName) {
  tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, TFT_BLACK);

  // Instructional text
  tft.setTextColor(ORANGE);
  tft.setTextSize(1);
  tft.setCursor(1, 235);
  tft.println("[!] Set a name for the saved profile.");
  tft.setCursor(23, 250);
  tft.println("(max 15 chars)");

  tft.setCursor(1, 275);
  tft.println("[!] Shuffle: Suggests random profile");
  tft.setCursor(23, 290);
  tft.println("names for your signal.");

  drawInputField(inputName);

  // Draw keyboard
  int yOffset = yOffsetStart;
  for (int row = 0; row < 4; row++) {
    int xOffset = 1;
    for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
      tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, TFT_DARKGREY);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(1);
      tft.setCursor(xOffset + 6, yOffset + 5);
      tft.print(keyboardLayout[row][col]);
      xOffset += keyWidth + keySpacing;
    }
    yOffset += keyHeight + keySpacing;
  }

  // Draw buttons
  tft.setTextColor(ORANGE);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);

  // Back Button
  tft.fillRoundRect(5, 195, 70, 25, 4, DARK_GRAY);
  tft.drawRoundRect(5, 195, 70, 25, 4, ORANGE);
  tft.drawString("Back", 40, 208);

  // Shuffle Button
  tft.fillRoundRect(85, 195, 70, 25, 4, DARK_GRAY);
  tft.drawRoundRect(85, 195, 70, 25, 4, ORANGE);
  tft.drawString("Shuffle", 120, 208);

  // OK Button
  tft.fillRoundRect(165, 195, 70, 25, 4, DARK_GRAY);
  tft.drawRoundRect(165, 195, 70, 25, 4, ORANGE);
  tft.drawString("OK", 200, 208);

  tft.setTextDatum(TL_DATUM); // Reset to top-left for other text
}

String getUserInputName() {
  String inputName = "";
  bool keyboardActive = true;

  drawKeyboard(inputName);

  while (keyboardActive) {
    // Blink cursor
    if (millis() - lastCursorBlink >= cursorBlinkInterval) {
      cursorState = !cursorState;
      drawInputField(inputName);
      lastCursorBlink = millis();
    }

    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int x = map(p.x, 300, 3800, 0, SCREEN_WIDTH - 1);
      int y = map(p.y, 3800, 300, 0, SCREEN_HEIGHT - 1);

      // Handle keyboard keys
      int yOffset = yOffsetStart;
      for (int row = 0; row < 4; row++) {
        int xOffset = 1;
        for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
          if (x >= xOffset && x <= xOffset + keyWidth && y >= yOffset && y <= yOffset + keyHeight) {
            char c = keyboardLayout[row][col];
            // Highlight key
            tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, ORANGE);
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(1);
            tft.setCursor(xOffset + 6, yOffset + 5);
            tft.print(c);
            delay(100); // Visual feedback
            tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, TFT_DARKGREY);
            tft.setTextColor(TFT_WHITE);
            tft.setCursor(xOffset + 6, yOffset + 5);
            tft.print(c);

            if (c == '<') { // Backspace
              if (inputName.length() > 0) {
                inputName = inputName.substring(0, inputName.length() - 1);
              }
            } else if (c == '-') { // Clear
              inputName = "";
            } else if (inputName.length() < MAX_NAME_LENGTH - 1) {
              inputName += c;
            }
            drawInputField(inputName);
            delay(200); // Debounce
          }
          xOffset += keyWidth + keySpacing;
        }
        yOffset += keyHeight + keySpacing;
      }

      // Handle buttons
      if (x >= 5 && x <= 75 && y >= 195 && y <= 210) { // Back
        keyboardActive = false;
        inputName = ""; // Cancel input
        tft.fillScreen(TFT_BLACK);
        updateDisplay();
      }

      if (x >= 85 && x <= 155 && y >= 195 && y <= 210) { // Shuffle
        inputName = randomNames[randomNameIndex];
        randomNameIndex = (randomNameIndex + 1) % numRandomNames;
        drawInputField(inputName);
        delay(200); // Debounce
      }

      if (x >= 165 && x <= 235 && y >= 195 && y <= 210) { // OK
        if (inputName.length() > 0) {
          keyboardActive = false;
          return inputName;
        } else {
          tft.fillRect(10, 80, 220, 10, TFT_BLACK);
          tft.setTextColor(TFT_RED);
          tft.setTextSize(1);
          tft.setCursor(10, 85);
          tft.println("Name cannot be empty!");
          tft.setTextColor(TFT_WHITE);
          delay(500);
          drawInputField(inputName); // Redraw to clear error
          delay(200); // Debounce
        }
      }
    }
    delay(10);
  }
  return inputName; // Return empty string if cancelled
}




void sendSignal() {
  
    mySwitch.disableReceive(); 
    delay(100);
    mySwitch.enableTransmit(TX_PIN); 
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0,40,240,37, TFT_BLACK);
    
    tft.setCursor(10, 30 + yshift);
    tft.print("Sending...");
    tft.setCursor(10, 40 + yshift);
    tft.print(receivedValue);

    mySwitch.setProtocol(receivedProtocol);
    mySwitch.send(receivedValue, receivedBitLength); 

    delay(500);
    tft.fillRect(0,40,240,37, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    ELECHOUSE_cc1101.SetRx(); 
    mySwitch.disableTransmit(); 
    delay(100);
    mySwitch.enableReceive(RX_PIN);
    
    delay(500);
    updateDisplay();
}

void do_sampling() {
  
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
unsigned int graph_y_offset = 81; 
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
    EEPROM.get(ADDR_PROFILE_START - sizeof(int), profileCount);
    if (profileCount > MAX_PROFILES || profileCount < 0) {
        profileCount = 0;  
    }
}

void saveProfile() {
    readProfileCount();  

    if (profileCount < MAX_PROFILES) {
        // Get custom name from user
        String customName = getUserInputName();

        tft.setTextSize(1);

        Profile newProfile;
        newProfile.frequency = subghz_frequency_list[currentFrequencyIndex];
        newProfile.value = receivedValue;
        newProfile.bitLength = receivedBitLength;
        newProfile.protocol = receivedProtocol;
        strncpy(newProfile.name, customName.c_str(), MAX_NAME_LENGTH - 1);
        newProfile.name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination

        int addr = ADDR_PROFILE_START + (profileCount * PROFILE_SIZE);
        EEPROM.put(addr, newProfile); 
        EEPROM.commit();

        profileCount++;  

        EEPROM.put(ADDR_PROFILE_START - sizeof(int), profileCount);
        EEPROM.commit();  

        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Profile saved!");
        tft.setCursor(10, 40 + yshift);
        tft.print("Name: ");
        tft.print(newProfile.name);
        tft.setCursor(10, 50 + yshift);
        tft.print("Profiles saved: ");
        tft.println(profileCount);

    } else {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Profile storage full!");
    }
    
    delay(2000);
    updateDisplay();
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
}

void loadProfileCount() {
    EEPROM.get(ADDR_PROFILE_START, profileCount);
    if (profileCount > MAX_PROFILES) {
        profileCount = MAX_PROFILES;  
    }
}

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 5 // Increased to include the back icon
    
    static int iconX[ICON_NUM] = {90, 130, 170, 210, 10}; // Added back icon at x=10
    static int iconY = STATUS_BAR_Y_OFFSET;
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,    
        bitmap_icon_sort_down_minus,      
        bitmap_icon_antenna,    
        bitmap_icon_floppy,
        bitmap_icon_go_back // Added back icon
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
                    currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
                    EEPROM.commit();
                    updateDisplay();
                    break;
                case 1: 
                    currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
                    EEPROM.commit();
                    updateDisplay();
                    break;
                case 2: 
                    sendSignal();
                    break;
                case 3: 
                    saveProfile();
                    break;
                case 4: // Back icon action (exit to submenu)
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

void ReplayAttackSetup() {  
  Serial.begin(115200);
  
  tft.fillScreen(TFT_BLACK); 
  tft.setRotation(0);

  setupTouchscreen();

  ELECHOUSE_cc1101.Init(); 
  
  //ELECHOUSE_cc1101.setModulation(0);
  //ELECHOUSE_cc1101.setRxBW(812);    
  
  ELECHOUSE_cc1101.SetRx();

  mySwitch.enableReceive(RX_PIN); 
  mySwitch.enableTransmit(TX_PIN); 

  EEPROM.begin(EEPROM_SIZE);  
  readProfileCount();        

  EEPROM.get(ADDR_VALUE, receivedValue);
  EEPROM.get(ADDR_BITLEN, receivedBitLength);
  EEPROM.get(ADDR_PROTO, receivedProtocol);
  EEPROM.get(ADDR_FREQ, currentFrequencyIndex);
      
  //ELECHOUSE_cc1101.setSpiPin (SCK, MISO, MOSI, CSN);
  //ELECHOUSE_cc1101.setSpiPin (18, 19, 23, 27);

  //ELECHOUSE_cc1101.setGDO(gdo0, gdo2);
  //ELECHOUSE_cc1101.setGDO(26, 16);
  //mySwitch.enableReceive(16); 
  //mySwitch.enableTransmit(26); 
  
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);

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
  
   float currentBatteryVoltage = readBatteryVoltage();
   drawStatusBar(currentBatteryVoltage, false);
   updateDisplay();
   uiDrawn = false;

}

void ReplayAttackLoop() {

    runUI();
    //updateStatusBar();
  
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    int btnLeftState = pcf.digitalRead(BTN_LEFT);
    int btnRightState = pcf.digitalRead(BTN_RIGHT);
    int btnSelectState = pcf.digitalRead(BTN_UP);
    int btndownState = pcf.digitalRead(BTN_DOWN);
    
    do_sampling();
    delay(10);
    epochSUB++;
    
    if (epochSUB >= tft.width())
      epochSUB = 0;

    if (btnRightState == LOW && millis() - lastDebounceTime > debounceDelay) {
        currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
        EEPROM.commit();
        updateDisplay();
        lastDebounceTime = millis();
    }
    
    if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {       
        currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
        EEPROM.commit();
        updateDisplay();
        lastDebounceTime = millis();
    }

    if (mySwitch.available()) { 
        receivedValue = mySwitch.getReceivedValue(); 
        receivedBitLength = mySwitch.getReceivedBitlength(); 
        receivedProtocol = mySwitch.getReceivedProtocol(); 

        EEPROM.put(ADDR_VALUE, receivedValue);
        EEPROM.put(ADDR_BITLEN, receivedBitLength);
        EEPROM.put(ADDR_PROTO, receivedProtocol);
        EEPROM.commit();
        
        updateDisplay();
        mySwitch.resetAvailable(); 
    }

     if (btnSelectState == LOW && receivedValue != 0 && millis() - lastDebounceTime > debounceDelay) {
        sendSignal();
        lastDebounceTime = millis();
    }
     if (pcf.digitalRead(BTN_DOWN) == LOW && millis() - lastDebounceTime > debounceDelay) {
         saveProfile();
         lastDebounceTime = millis();
    }
  } 
}


/*
 * 
 * Saved Profile
 * 
 * 
 */

namespace SavedProfile {

static bool uiDrawn = false;

#define RX_PIN 16         
#define TX_PIN 26        

#define EEPROM_SIZE 1440  // Increased to accommodate larger profiles
#define ADDR_PROFILE_START 1300 
#define MAX_PROFILES 5         
#define MAX_NAME_LENGTH 16 // Maximum length for profile name (including null terminator)

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 320 

RCSwitch mySwitch = RCSwitch();

struct Profile {
    uint32_t frequency;
    unsigned long value;
    int bitLength;
    int protocol;
    char name[MAX_NAME_LENGTH]; // Custom name field
};

#define PROFILE_SIZE sizeof(Profile) // Size of the updated Profile struct

int profileCount = 0;
int currentProfileIndex = 0;
int yshift = 40;

void updateDisplay() {
    tft.fillRect(0, 40, 240, 280, TFT_BLACK); // Adjusted to clear only necessary area
    tft.setCursor(5, 5 + yshift);
    tft.setTextColor(TFT_YELLOW);
    tft.print("Saved Profiles");

    if (profileCount == 0) {
        tft.setCursor(10, 35 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("No profiles saved.");
        return;
    }

    Profile selectedProfile;
    int addr = ADDR_PROFILE_START + (currentProfileIndex * PROFILE_SIZE);
    EEPROM.get(addr, selectedProfile);

    if (selectedProfile.value == 0) {
        tft.setCursor(10, 50 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("No valid profile.");
        return;
    }

    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.printf("Profile %d/%d", currentProfileIndex + 1, profileCount);

    tft.setCursor(10, 50 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.print("Name: ");
    tft.print(selectedProfile.name);

    tft.setCursor(10, 70 + yshift);
    tft.printf("Freq: %.2f MHz", selectedProfile.frequency / 1000000.0);

    tft.setCursor(10, 90 + yshift);
    tft.printf("Val: %lu", selectedProfile.value);

    tft.setCursor(10, 110 + yshift);
    tft.printf("BitLen: %d", selectedProfile.bitLength);

    tft.setCursor(10, 130 + yshift);
    tft.printf("Protocol: %d", selectedProfile.protocol);
}

void transmitProfile(int index) {
    Profile profileToSend;
    int addr = ADDR_PROFILE_START + (index * PROFILE_SIZE);
    EEPROM.get(addr, profileToSend);

    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(profileToSend.frequency / 1000000.0);

    mySwitch.disableReceive(); 
    delay(100);
    mySwitch.enableTransmit(TX_PIN); 
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, 240, 280, TFT_BLACK); 
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
    tft.fillRect(0, 40, 240, 280, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    ELECHOUSE_cc1101.SetRx(); 
    mySwitch.disableTransmit(); 
    delay(100);
    mySwitch.enableReceive(RX_PIN);
    
    delay(500);
    updateDisplay();
}

void loadProfileCount() {
    EEPROM.get(ADDR_PROFILE_START - 4, profileCount);

    if (profileCount < 0 || profileCount > MAX_PROFILES) {
        profileCount = 0;
        EEPROM.put(ADDR_PROFILE_START - 4, profileCount);  
        EEPROM.commit();  
    }
}

void printProfiles() {
    Serial.println("Saved Profiles:");
    for (int i = 0; i < profileCount; i++) {
        Profile savedProfile;
        int addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
        EEPROM.get(addr, savedProfile);

        if (savedProfile.value != 0) {
            Serial.printf("Profile %d:\n", i + 1);
            Serial.printf("  Name: %s\n", savedProfile.name);
            Serial.printf("  Frequency: %.2f MHz\n", savedProfile.frequency / 1000000.0);
            Serial.printf("  Value: %lu\n", savedProfile.value);
            Serial.printf("  Bit Length: %d\n", savedProfile.bitLength);
            Serial.printf("  Protocol: %d\n", savedProfile.protocol);
            Serial.println("-------------------------");
        }
    }
}

void deleteProfile(int index) {
    if (index >= profileCount || index < 0) return;  

    Profile deletedProfile;
    int addr = ADDR_PROFILE_START + (index * PROFILE_SIZE);
    EEPROM.get(addr, deletedProfile); // Get profile for display

    for (int i = index; i < profileCount - 1; i++) {
        Profile nextProfile;
        int addr = ADDR_PROFILE_START + ((i + 1) * PROFILE_SIZE);
        EEPROM.get(addr, nextProfile);

        addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
        EEPROM.put(addr, nextProfile);
    }

    Profile emptyProfile = {0, 0, 0, 0, ""};
    EEPROM.put(ADDR_PROFILE_START + ((profileCount - 1) * PROFILE_SIZE), emptyProfile);

    profileCount--;
    EEPROM.put(ADDR_PROFILE_START - 4, profileCount);  
    EEPROM.commit();

    if (currentProfileIndex >= profileCount) {
        currentProfileIndex = profileCount - 1;  
    }

    tft.fillRect(0, 40, 240, 280, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.print("Removed: ");
    tft.print(deletedProfile.name);

    delay(1000);
    updateDisplay();
}

void runUI() {
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 5
    
    static int iconX[ICON_NUM] = {90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_down_minus,    
        bitmap_icon_sort_up_plus,       
        bitmap_icon_antenna,     
        bitmap_icon_recycle,
        bitmap_icon_go_back // Back icon
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
                case 0: // Next profile
                    if (profileCount > 0) {
                        currentProfileIndex = (currentProfileIndex + 1) % profileCount;
                        updateDisplay();
                    }
                    break;
                case 1: // Previous profile
                    if (profileCount > 0) {
                        currentProfileIndex = (currentProfileIndex - 1 + profileCount) % profileCount;
                        updateDisplay();
                    }
                    break;
                case 2: // Transmit profile
                    if (profileCount > 0) {
                        transmitProfile(currentProfileIndex);
                    }
                    break;
                case 3: // Delete profile
                    if (profileCount > 0) {
                        deleteProfile(currentProfileIndex);
                    }
                    break;
                case 4: // Back icon (exit to submenu)
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
            int y = ::map(p.y, 3800, 300, 0, SCREEN_HEIGHT - 1);

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

void saveSetup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
    loadProfileCount();
    printProfiles();

    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_UP, INPUT_PULLUP);

    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);

    setupTouchscreen();

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    uiDrawn = false;

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.SetRx();

    mySwitch.enableReceive(RX_PIN);
    mySwitch.enableTransmit(TX_PIN);

    updateDisplay();
}

void saveLoop() {
    runUI();
    
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    int btnLeftState = pcf.digitalRead(BTN_LEFT);
    int btnRightState = pcf.digitalRead(BTN_RIGHT);
    int btnSelectState = pcf.digitalRead(BTN_DOWN);
    int btnUpState = pcf.digitalRead(BTN_UP);

    if (profileCount > 0) {
        if (btnRightState == LOW && millis() - lastDebounceTime > debounceDelay) {
            currentProfileIndex = (currentProfileIndex + 1) % profileCount;
            updateDisplay();
            lastDebounceTime = millis();
        }

        if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {
            currentProfileIndex = (currentProfileIndex - 1 + profileCount) % profileCount;
            updateDisplay();
            lastDebounceTime = millis();
        }

        if (btnSelectState == LOW && millis() - lastDebounceTime > debounceDelay) {
            transmitProfile(currentProfileIndex);
            lastDebounceTime = millis();
        }

        if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
            deleteProfile(currentProfileIndex);  
            lastDebounceTime = millis();
        }
    } else {
        //tft.fillRect(0, 40, 240, 280, TFT_BLACK);
        tft.setCursor(10, 50 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("No profiles to select.");
    }
}

}


/*
 * 
 * subGHz jammer
 * 
 * 
 */

namespace subjammer {

static bool uiDrawn = false;

static unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

#define TX_PIN 26        
 
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 64

#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_DOWN   3
#define BTN_UP     6


bool jammingRunning = false;
bool continuousMode = true;
bool autoMode = false;     
unsigned long lastSweepTime = 0;
const unsigned long sweepInterval = 1000; 

static const uint32_t subghz_frequency_list[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000,  
    390000000, 418000000, 433075000, 433420000, 433920000, 434420000, 
    434775000, 438900000, 868350000, 915000000, 925000000
};
const int numFrequencies = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);
int currentFrequencyIndex = 4; 
float targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;


void updateDisplay() {

    int yshift = 20;
    
    tft.fillRect(0, 40, 240, 80, TFT_BLACK); 
    tft.drawLine(0, 79, 235, 79, TFT_WHITE);

    // Frequency section
    tft.setTextSize(1);
    tft.setCursor(5, 22 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Freq:");
    tft.setCursor(40, 22 + yshift);
    if (autoMode) {
        tft.setTextColor(ORANGE);
        tft.print("Auto: ");
        tft.setTextColor(TFT_WHITE);
        tft.print(targetFrequency, 1);
        // Progress bar
        int progress = map(currentFrequencyIndex, 0, numFrequencies - 1, 0, 240);
        tft.fillRect(0, 60 + yshift, 240, 4, TFT_BLACK);
        tft.fillRect(0, 60 + yshift, progress, 4, ORANGE);
        // Blinking sweep indicator
        if (jammingRunning && millis() % 1000 < 500) {
            tft.fillCircle(220, 22 + yshift, 2, TFT_GREEN);
        }
    } else {
        tft.setTextColor(TFT_WHITE);
        tft.print(targetFrequency, 2);
        tft.print(" MHz");
    }

    // Mode section
    tft.setCursor(130, 22 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Mode:");
    tft.setCursor(165, 22 + yshift);
    tft.setTextColor(continuousMode ? TFT_GREEN : TFT_YELLOW);
    tft.print(continuousMode ? "Cont" : "Noise");

    // Status section
    tft.setCursor(5, 42 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Status:");
    tft.setCursor(50, 42 + yshift);
    if (jammingRunning) {
        tft.setTextColor(TFT_RED);
        tft.print("Jamming");

    } else {
        tft.setTextColor(TFT_GREEN);
        tft.print("Idle   "); 
    }
}


void runUI() {
    #define SCREEN_WIDTH  240
    #define SCREENHEIGHT 320
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 6 
    
    static int iconX[ICON_NUM] = {50, 90, 130, 170, 210, 10}; // Added back icon at x=10
    static int iconY = STATUS_BAR_Y_OFFSET;
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_power,    
        bitmap_icon_antenna,      
        bitmap_icon_random,    
        bitmap_icon_sort_down_minus,
        bitmap_icon_sort_up_plus,
        bitmap_icon_go_back // Added back icon
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
                      ELECHOUSE_cc1101.setMHZ(targetFrequency);
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
                case 5: // Back icon action (exit to submenu)
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

void subjammerSetup() {
    Serial.begin(115200);

    ELECHOUSE_cc1101.Init(); 
    ELECHOUSE_cc1101.setModulation(0); 
    ELECHOUSE_cc1101.setRxBW(500.0);  
    ELECHOUSE_cc1101.setPA(12);       
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    ELECHOUSE_cc1101.SetTx();

    randomSeed(analogRead(0));

    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
    delay(100);

    tft.setRotation(0); 
    tft.fillScreen(TFT_BLACK);

    setupTouchscreen();

   float currentBatteryVoltage = readBatteryVoltage();
   drawStatusBar(currentBatteryVoltage, false);
   updateDisplay();
   uiDrawn = false;
}

void subjammerLoop() {
  
    runUI();
    
    int btnLeftState = pcf.digitalRead(BTN_LEFT);
    int btnRightState = pcf.digitalRead(BTN_RIGHT);
    int btnUpState = pcf.digitalRead(BTN_UP);
    int btnDownState = pcf.digitalRead(BTN_DOWN);

    if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
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

    if (btnRightState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
        targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
        ELECHOUSE_cc1101.setMHZ(targetFrequency);
        Serial.print("Switched to: ");
        Serial.print(targetFrequency);
        Serial.println(" MHz");
        updateDisplay();
        lastDebounceTime = millis();
    }
      
    if (btnLeftState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        continuousMode = !continuousMode;
        Serial.print("Jamming mode: ");
        Serial.println(continuousMode ? "Continuous Carrier" : "Noise");
        updateDisplay();
        lastDebounceTime = millis();
    }

    if (btnDownState == LOW && millis() - lastDebounceTime > debounceDelay) {
        autoMode = !autoMode;
        Serial.print("Frequency mode: ");
        Serial.println(autoMode ? "Automatic" : "Manual");
        if (autoMode) {
            currentFrequencyIndex = 0;
            targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
            ELECHOUSE_cc1101.setMHZ(targetFrequency);
        }
        updateDisplay();
        lastDebounceTime = millis();
    }

    if (jammingRunning) {
        if (autoMode) {
            if (millis() - lastSweepTime >= sweepInterval) {
                currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
                targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                ELECHOUSE_cc1101.setMHZ(targetFrequency);
                Serial.print("Sweeping: ");
                Serial.print(targetFrequency);
                Serial.println(" MHz");
                updateDisplay();
                lastSweepTime = millis();
            }
        }

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

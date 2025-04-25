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

#define EEPROM_SIZE 100

#define ADDR_VALUE 0    
#define ADDR_BITLEN 4   
#define ADDR_PROTO 6    
#define ADDR_FREQ 10    

static bool uiDrawn = false;



struct Profile {
    uint32_t frequency;
    unsigned long value;
    int bitLength;
    int protocol;
};

#define ADDR_PROFILE_START 20  
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
        Profile newProfile;
        newProfile.frequency = subghz_frequency_list[currentFrequencyIndex];
        newProfile.value = receivedValue;
        newProfile.bitLength = receivedBitLength;
        newProfile.protocol = receivedProtocol;

        int addr = ADDR_PROFILE_START + (profileCount * sizeof(Profile));
        EEPROM.put(addr, newProfile); 
        EEPROM.commit();

        profileCount++;  

        EEPROM.put(ADDR_PROFILE_START - sizeof(int), profileCount);
        EEPROM.commit();  

        delay(500);
        tft.fillRect(0,40,240,37, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Profile saved!");

        tft.setCursor(10, 40 + yshift);
        tft.print("Profiles saved so far: ");
        tft.println(profileCount);

    } else {
        tft.fillRect(0,40,240,37, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Profile storage full!");
    }
    
    delay(1000);
    updateDisplay();
}

void loadProfileCount() {
    EEPROM.get(ADDR_PROFILE_START, profileCount);
    if (profileCount > MAX_PROFILES) {
        profileCount = MAX_PROFILES;  
    }
}

void runUI() {
    #define SCREEN_WIDTH  240
    #define SCREENHEIGHT 320
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

#define EEPROM_SIZE 100  
#define ADDR_PROFILE_START 20  
#define MAX_PROFILES 5         

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 

RCSwitch mySwitch = RCSwitch();

struct Profile {
    uint32_t frequency;
    unsigned long value;
    int bitLength;
    int protocol;
};

int profileCount = 0;
int currentProfileIndex = 0;
int yshift = 40;

void updateDisplay() {
    tft.fillRect(0, 40, 240, 320, TFT_BLACK);
    tft.setCursor(5, 5 + yshift);
    tft.setTextColor(TFT_YELLOW);
    tft.print("Saved Profiles");

    if (profileCount == 0) {
        tft.setCursor(10, 50 + yshift);
        tft.print("No profiles saved.");
        return;
    }

    Profile selectedProfile;
    int addr = ADDR_PROFILE_START + (currentProfileIndex * sizeof(Profile));
    EEPROM.get(addr, selectedProfile);

    if (selectedProfile.value == 0) {
        tft.setCursor(10, 40 + yshift);
        tft.print("No valid profile.");
        return;
    }

    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.printf("Profile %d/%d", currentProfileIndex + 1, profileCount);

    tft.setCursor(10, 50 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.printf("Freq: %.2f MHz", selectedProfile.frequency / 1000000.0);

    tft.setCursor(10, 70 + yshift);
    tft.printf("Val: %lu", selectedProfile.value);

    tft.setCursor(10, 90 + yshift);
    tft.printf("BitLen: %d", selectedProfile.bitLength);

    tft.setCursor(10, 110 + yshift);
    tft.printf("Protocol: %d", selectedProfile.protocol);
}

void transmitProfile(int index) {
    Profile profileToSend;
    int addr = ADDR_PROFILE_START + (index * sizeof(Profile));
    EEPROM.get(addr, profileToSend);

    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(profileToSend.frequency / 1000000.0);

    mySwitch.disableReceive(); 
    delay(100);
    mySwitch.enableTransmit(TX_PIN); 
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, 240, 320, TFT_BLACK); 
    tft.setCursor(10, 30 + yshift);
    tft.print("Sending...");
    tft.setCursor(10, 60 + yshift);
    tft.print(profileToSend.value);

    mySwitch.setProtocol(profileToSend.protocol);
    mySwitch.send(profileToSend.value, profileToSend.bitLength); 

    delay(500);
    tft.fillRect(0, 40, 240, 320, TFT_BLACK);
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
        int addr = ADDR_PROFILE_START + (i * sizeof(Profile));
        EEPROM.get(addr, savedProfile);

        if (savedProfile.value != 0) {
            Serial.printf("Profile %d:\n", i + 1);
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

    for (int i = index; i < profileCount - 1; i++) {
        Profile nextProfile;
        int addr = ADDR_PROFILE_START + ((i + 1) * sizeof(Profile));
        EEPROM.get(addr, nextProfile);

        addr = ADDR_PROFILE_START + (i * sizeof(Profile));
        EEPROM.put(addr, nextProfile);
    }

    Profile emptyProfile = {0, 0, 0, 0};
    EEPROM.put(ADDR_PROFILE_START + ((profileCount - 1) * sizeof(Profile)), emptyProfile);

    profileCount--;
    EEPROM.put(ADDR_PROFILE_START - 4, profileCount);  
    EEPROM.commit();

    if (currentProfileIndex >= profileCount) {
        currentProfileIndex = profileCount - 1;  
    }

    delay(1000);
    tft.fillRect(0, 40, 240, 320, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Profile Removed.");

    delay(500);
    updateDisplay();
}


void runUI() {
    #define SCREEN_WIDTH  240
    #define SCREEN_HEIGHT 320
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 5
    
    static int iconX[ICON_NUM] = {90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_RIGHT,    
        bitmap_icon_LEFT,       
        bitmap_icon_antenna,     
        bitmap_icon_recycle,
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
                if (profileCount > 0) {
                  currentProfileIndex = (currentProfileIndex + 1) % profileCount;
                  updateDisplay();
                  break;
                }
                case 1: 
                if (profileCount > 0) {
                  currentProfileIndex = (currentProfileIndex - 1 + profileCount) % profileCount;
                  updateDisplay();
                }
                  break;
                case 2: 
                  transmitProfile(currentProfileIndex);
                  break;
                case 3: 
                  deleteProfile(currentProfileIndex);
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
    //updateStatusBar();
    
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
        tft.setCursor(10, 30 + yshift);
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

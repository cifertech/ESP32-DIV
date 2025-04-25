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

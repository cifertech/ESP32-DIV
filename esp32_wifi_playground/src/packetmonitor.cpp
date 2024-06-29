/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#include <Arduino.h> 
#include "packetmonitor.h"

extern Adafruit_ST7735 tft;

arduinoFFT FFT =ArduinoFFT();
extern Adafruit_NeoPixel pixels;

#define MAX_CH 14       
#define SNAP_LEN 2324   

#define BUTTON_PIN 22   
#define BACK 26 

#define MAX_X 128
#define MAX_Y 51

const uint16_t GRAY = 303131;
const uint16_t BLUE = 0x001f;
const uint16_t RED = 0xf800;
const uint16_t GREEN = 0x07e0;
const uint16_t BLACK = 0;
const uint16_t YELLOW = RED + GREEN;
const uint16_t CYAN = GREEN + BLUE;
const uint16_t MAGENTA = RED + BLUE;
const uint16_t WHITE = RED + BLUE + GREEN;
const uint16_t ORANGE = 0xfbe4;

esp_err_t event_handler(void* ctx, system_event_t* event) {
  return ESP_OK;
}

Preferences preferences;

const int analogInputPin = 39;
const uint16_t samples = 256; 
const double samplingFrequency = 5000;

double attenuation = 10;

unsigned int sampling_period_us;
unsigned long microseconds;

double vReal[samples];
double vImag[samples];

byte palette_red[128], palette_green[128], palette_blue[128];

bool useSD = false;
bool buttonPressed = false;
bool buttonEnabled = true;
uint32_t lastDrawTime;
uint32_t lastButtonTime;
uint32_t tmpPacketCounter;
uint32_t pkts[MAX_X];       // here the packets per second will be saved
uint32_t deauths = 0;       // deauth frames per second
unsigned int ch = 1;        // current 802.11 channel
int rssiSum;

unsigned int epoch = 0;
unsigned int color_cursor = 2016;

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
      pixels.setPixelColor(0, pixels.Color(64, 20, 0));
      pixels.show(); 
  uint32_t packetLength = ctrl.sig_len;
  if (type == WIFI_PKT_MGMT) packetLength -= 4;  
  //Serial.print(".");
  tmpPacketCounter++;
  rssiSum += ctrl.rssi;
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();     
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


void do_sampling_FFT() {
   
  microseconds = micros();
  for(int i=0; i < samples; i++) {
      vReal[i] = tmpPacketCounter*300;
      vImag[i] = 1;
      while(micros() - microseconds < sampling_period_us){
        
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

  unsigned int top_y = 155;
  int max_k = 0;
  for (int j = 0; j < samples >> 1; j++) {
      int k = vReal[j] / attenuation;
      if (k > max_k)
        max_k = k;      
      if (k > 127) k = 127;
      unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k]; 
      tft.drawPixel(epoch, top_y - j, color);
  }
  double tattenuation = max_k / 127.0;
  if (tattenuation > attenuation)
    attenuation = tattenuation;

          tft.fillRect(0,0,128,10, ORANGE);
          //tft.fillScreen(ST7735_BLACK);
          tft.setTextWrap(false);
          tft.setCursor(20, 2);
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(1);
          tft.print(ch);
            
          tft.setCursor(2, 2);
          tft.setTextColor(ST7735_BLACK);
          tft.print("Ch:");

          //tft.fillRect(50,0,15,15, ST7735_BLACK);
          tft.setTextWrap(true);
          tft.setCursor(80, 2);
          tft.setTextColor(ST7735_BLACK);
          tft.setTextSize(1);
          tft.print(tmpPacketCounter);

          tft.setCursor(50, 2);
          tft.setTextColor(ST7735_BLACK);
          tft.print("Pkts:");
 
          delay(10);
}



void packetmonitorSetup(){
  
   Serial.begin(115200);

   pinMode(BACK, INPUT_PULLUP);
     
   //tft.initR(INITR_BLACKTAB);
  // tft.setRotation(0);
   tft.fillScreen(ST7735_BLACK);
      
  sampling_period_us = round(1000000*(1.0/samplingFrequency));

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
   
  sampling_period_us = round(1000000*(1.0/samplingFrequency));

  preferences.begin("packetmonitor32", false);
  ch = preferences.getUInt("channel", 1);
  preferences.end();

  //nvs_flash_init();
  tcpip_adapter_init();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  //ESP_ERROR_CHECK(esp_wifi_set_country(WIFI_COUNTRY_EU));
  //ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);
}


void packetmonitorLoop(){
 
  tft.drawPixel(epoch, 0, color_cursor);
  do_sampling_FFT();
  delay(10);
  
  tft.drawPixel(epoch, 0, 0);
  epoch++;
  if (epoch >= tft.width())
    epoch = 0;



    if (digitalRead(BUTTON_PIN) == LOW) {
      if (buttonEnabled) {
        if (!buttonPressed) {
          buttonPressed = true;
        }
      }
    
      if (buttonPressed) {
        setChannel(ch + 1);
                 
      }
      buttonPressed = false;
      buttonEnabled = true;
    }
   
      pkts[127] = tmpPacketCounter;

      Serial.println((String)pkts[MAX_X - 1]);

      tmpPacketCounter = 0;
      deauths = 0;
      rssiSum = 0;
    
    
    if (Serial.available()) {
      ch = Serial.readString().toInt();
      if (ch < 1 || ch > 14) ch = 1;
      setChannel(ch);
    }
}

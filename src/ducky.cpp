#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <PCF8574.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <vector>
#include "Touchscreen.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"


#define pcf_ADDR 0x20

extern PCF8574 pcf;

extern bool feature_exit_requested;

extern TFT_eSPI tft;

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

namespace Ducky {

static const char* DUCKY_DIR = "/ducky";
static const char* DEV_NAME  = "ESP32S3 Ducky";

#define COL_BG     TFT_BLACK
#define COL_FG     TFT_WHITE
#define COL_ACCENT FEATURE_TEXT
#define COL_WARN   UI_WARN
#define COL_ROWSEL L_Dark

static const int HEADER_H    = 20;

static const int TEXT_HEADER_H = HEADER_H;
static const int FOOTER_H    = FeatureUI::FOOTER_H;
static const int BTN_H       = FeatureUI::BTN_H;
static const int BTN_GAP     = FeatureUI::GAP_X;

static const int LIST_ROW_H  = 40;

static const int LIST_ROW_GAP = 6;
static const int PADDING     = FeatureUI::PAD_X;

static const int TOOLBAR_Y      = 37;
static const int TOOLBAR_HEIGHT = HEADER_H;

static bool sd_mounted = false;
static bool ui_inited  = false;
static bool ui_busy    = false;
static bool needsRedraw = false;

static bool ducky_active = false;

struct EdgeBtn {
  uint8_t pin;
  bool last;
  uint32_t tLast;
  EdgeBtn(): pin(0), last(true), tLast(0) {}
  explicit EdgeBtn(uint8_t p): pin(p), last(true), tLast(0) {}
};

static EdgeBtn ebUp{BTN_UP}, ebDown{BTN_DOWN}, ebLeft{BTN_LEFT}, ebRight{BTN_RIGHT}, ebSelect{BTN_SELECT};

static bool uiDrawn = false;
static unsigned long lastAnimationTime = 0;
static int animationState = 0;
static int activeIcon = -1;

static const int ICON_NUM = 2;
static int iconX[ICON_NUM] = {210, 10};
static int iconY = 20;

static const unsigned char* icons[ICON_NUM] = {
    bitmap_icon_list,
    bitmap_icon_go_back
};

static const int ICON_SIZE = 16;

static bool pcfPressedEdge(EdgeBtn& eb, uint16_t debounceMs=40) {
  bool cur = pcf.digitalRead(eb.pin);
  uint32_t now = millis();
  bool pressedEvent = (!cur && eb.last && (now - eb.tLast) > debounceMs);
  if (pressedEvent) eb.tLast = now;
  eb.last = cur;
  return pressedEvent;
}

static NimBLEHIDDevice*      hid = nullptr;
static NimBLECharacteristic* inputChr = nullptr;
static NimBLECharacteristic* outputChr = nullptr;
static volatile bool         bleConnected = false;

struct ScriptItem { String path, name, desc; uint32_t size = 0; };
static std::vector<ScriptItem> items;
static int  sel = 0;

enum class Page { List, Details, Settings };
static Page page = Page::List;

enum class Dialog { None, ConfirmDelete };
static Dialog dialog = Dialog::None;

static uint32_t defaultDelay = 0;
static String   lastError;

static String trimCR(const String& s) {
  String x = s; if (x.endsWith("\r")) x.remove(x.length() - 1); return x;
}
static void clampSel() {
  int last = (int)items.size() - 1; if (last < 0) last = 0;
  if (sel > last) sel = last; if (sel < 0) sel = 0;
}
static String fitText(const String& s, int pixWidth, int font = 2) {
  String out = s;
  while (tft.textWidth(out, font) > pixWidth && out.length() > 1) {
    out.remove(out.length() - 1);
  }
  if (out.length() < s.length() && pixWidth >= tft.textWidth("…", font) + 4) out += "…";
  return out;
}

static int wrapLineNoEllipsis(const String& s, int pixWidth, int font, String& outLine) {
  if (s.length() == 0) { outLine = ""; return 0; }

  int nl = s.indexOf('\n');
  if (nl >= 0) {
    String pre = s.substring(0, nl);
    if (tft.textWidth(pre, font) <= pixWidth) {
      outLine = pre;
      outLine.trim();
      return nl + 1;
    }
  }

  int best = 0;
  int len = (int)s.length();
  for (int i = 1; i <= len; i++) {
    String sub = s.substring(0, i);
    if (tft.textWidth(sub, font) <= pixWidth) best = i;
    else break;
  }
  if (best <= 0) best = 1;

  int breakAt = best;
  int lastSpace = -1;
  for (int i = best - 1; i >= 0; i--) {
    if (s[i] == ' ') { lastSpace = i; break; }
  }
  if (lastSpace > 0) {
    const int tail = best - lastSpace;

    if (tail <= 12) breakAt = lastSpace;
  }
  if (breakAt <= 0) breakAt = best;

  outLine = s.substring(0, breakAt);
  outLine.trim();
  return breakAt;
}

struct Tap {
  bool has = false;
  int  x = 0, y = 0;
  unsigned long time = 0;
};

static bool touching = false;
static uint32_t touchDownMs = 0;
static int lastX = 0, lastY = 0;
static const uint32_t TOUCH_DEBOUNCE_MS = 120;
static const uint32_t DOUBLE_TAP_MS = 300;

static Tap readTap() {
  Tap t{};
  int x, y;
  bool now = readTouchXY(x, y);
  uint32_t ms = millis();

  if (!touching && now) {
    touching = true; touchDownMs = ms; lastX = x; lastY = y;
  } else if (touching && now) {
    lastX = x; lastY = y;
  } else if (touching && !now) {
    touching = false;
    if (ms - touchDownMs >= 20 && ms - touchDownMs <= 800) {
      t.has = true; t.x = lastX; t.y = lastY; t.time = ms;
    }
  }
  return t;
}

static uint32_t lastTapTime = 0;
static int lastTapX = 0, lastTapY = 0;

static bool isDoubleTap(const Tap& tap) {
  if (!tap.has) return false;

  uint32_t timeDiff = tap.time - lastTapTime;
  int dist = abs(tap.x - lastTapX) + abs(tap.y - lastTapY);

  lastTapTime = tap.time;
  lastTapX = tap.x;
  lastTapY = tap.y;

  return (timeDiff < DOUBLE_TAP_MS && dist < 50);
}

static bool execFile(const String& path);
static void drawListPage(bool forceReload = false);
static void updateListSelection(int newSel);
static void drawDetailsPage();
static void drawSettingsPage();

static void drawIconHeader() {
    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(0, 20, 240, 16, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            }
        }
        tft.drawLine(0, 36, 240, 36, ORANGE);
        uiDrawn = true;
    }
}

static void handleIconTouch(int x, int y) {

    const int y0 = iconY - 4;
    const int y1 = iconY + ICON_SIZE + 4;
    if (y >= y0 && y <= y1) {
        for (int i = 0; i < ICON_NUM; i++) {
            const int x0 = iconX[i] - 6;
            const int x1 = iconX[i] + ICON_SIZE + 6;
            if (x >= x0 && x <= x1) {
                if (icons[i] != NULL && animationState == 0) {

                    if (i == 1) {
                        feature_exit_requested = true;
                        break;
                    }

                    if (i == 0) {
                        if (page != Page::Settings) {
                            page = Page::Settings;
                            drawSettingsPage();
                        } else {
                            page = Page::List;
                            drawListPage();
                        }

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

static void updateIconAnimation() {
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
}

static bool mountSD() {
  if (sd_mounted) return true;
  pinMode(SD_CD, INPUT_PULLUP);
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) { sd_mounted = true; return true; }
  #ifdef SD_CS_PIN

  #ifdef CC1101_CS
  if (SD_CS_PIN != CC1101_CS) {
    if (SD.begin(SD_CS_PIN)) { sd_mounted = true; return true; }
  }
  #else
  if (SD.begin(SD_CS_PIN)) { sd_mounted = true; return true; }
  #endif
  #endif
  return false;
}
static bool ensureDuckyDir() {
  if (!mountSD()) return false;
  if (!SD.exists(DUCKY_DIR)) return SD.mkdir(DUCKY_DIR);
  return true;
}
static void parseMeta(File& f, ScriptItem& it) {
  it.desc = "";
  f.seek(0);
  for (int i = 0; i < 10 && f.available(); ++i) {
    String line = trimCR(f.readStringUntil('\n'));
    if (!line.startsWith("REM")) continue;
    String body = line.substring(3); body.trim();
    if (body.startsWith("Name:"))        { it.name = body.substring(5); it.name.trim(); }
    else if (body.startsWith("Description:")) { it.desc = body.substring(12); it.desc.trim(); }
  }
  if (!it.name.length()) {
    String fn = it.path.substring(it.path.lastIndexOf('/') + 1);
    int dot = fn.lastIndexOf('.'); if (dot > 0) fn.remove(dot);
    it.name = fn;
  }
}
static void reloadList() {
  items.clear();
  if (!ensureDuckyDir()) return;
  File d = SD.open(DUCKY_DIR); if (!d) return;
  for (;;) {
    File f = d.openNextFile(); if (!f) break;
    if (f.isDirectory()) { f.close(); continue; }
    String p = String(DUCKY_DIR) + "/" + f.name();
    String ext = p.substring(p.lastIndexOf('.') + 1); ext.toLowerCase();
    if (!(ext == "duck" || ext == "txt")) { f.close(); continue; }
    ScriptItem it; it.path = p; it.size = f.size();
    parseMeta(f, it);
    items.push_back(it);
    f.close();
  }
  d.close();
  clampSel();
}

enum : uint8_t {
  KEY_A = 0x04, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
  KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
  KEY_1 = 0x1E, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
  KEY_ENTER = 0x28, KEY_ESC = 0x29, KEY_BACKSPACE = 0x2A, KEY_TAB = 0x2B, KEY_SPACE = 0x2C,
  KEY_MINUS = 0x2D, KEY_EQUAL = 0x2E, KEY_LEFTBRACE = 0x2F, KEY_RIGHTBRACE = 0x30, KEY_BACKSLASH = 0x31,
  KEY_SEMICOLON = 0x33, KEY_APOSTROPHE = 0x34, KEY_GRAVE = 0x35, KEY_COMMA = 0x36, KEY_DOT = 0x37, KEY_SLASH = 0x38,
  KEY_CAPSLOCK = 0x39,
  KEY_F1 = 0x3A, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
  KEY_HOME = 0x4A, KEY_PAGEUP = 0x4B, KEY_DELETE = 0x4C, KEY_END = 0x4D, KEY_PAGEDOWN = 0x4E,
  KEY_RIGHT = 0x4F, KEY_LEFT = 0x50, KEY_DOWN = 0x51, KEY_UP = 0x52
};
enum : uint8_t { MOD_CTRL=0x01, MOD_SHIFT=0x02, MOD_ALT=0x04, MOD_GUI=0x08 };
struct __attribute__((packed)) KbdReport { uint8_t modifiers; uint8_t reserved; uint8_t keys[6]; };

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override { bleConnected = true; }
  void onDisconnect(NimBLEServer*) override {
    bleConnected = false;

    if (ducky_active) {
      NimBLEDevice::startAdvertising();
    }
  }
};
class OutputCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override { (void)c; }
};

static void sendKey(uint8_t usage, uint8_t mods = 0, uint16_t downMs = 10) {
  if (!bleConnected) return;
  KbdReport idle{};
  inputChr->setValue((uint8_t*)&idle, sizeof(idle)); inputChr->notify(); delay(3);
  KbdReport rpt{}; rpt.modifiers = mods; rpt.keys[0]=usage;
  inputChr->setValue((uint8_t*)&rpt, sizeof(rpt)); inputChr->notify(); delay(downMs);
  inputChr->setValue((uint8_t*)&idle, sizeof(idle)); inputChr->notify(); delay(5);
}

static uint8_t keyFromChar(char c, bool& needShift) {
  needShift = false;
  if (c >= 'a' && c <= 'z') return KEY_A + (c - 'a');
  if (c >= 'A' && c <= 'Z') { needShift = true; return KEY_A + (c - 'A'); }
  if (c >= '1' && c <= '9') return KEY_1 + (c - '1');
  if (c == '0') return KEY_0;
  if (c == ' ') return KEY_SPACE;
  if (c == '\n' || c == '\r') return KEY_ENTER;
  if (c == '-') return 0x2D;  if (c == '_') { needShift = true; return 0x2D; }
  if (c == '=') return 0x2E;  if (c == '+') { needShift = true; return 0x2E; }
  if (c == '[') return 0x2F;  if (c == '{') { needShift = true; return 0x2F; }
  if (c == ']') return 0x30;  if (c == '}') { needShift = true; return 0x30; }
  if (c == '\\') return 0x31; if (c == '|') { needShift = true; return 0x31; }
  if (c == ';') return 0x33;  if (c == ':') { needShift = true; return 0x33; }
  if (c == '\'') return 0x34; if (c == '"'){ needShift = true; return 0x34; }
  if (c == ',') return 0x36;  if (c == '<') { needShift = true; return 0x36; }
  if (c == '.') return 0x37;  if (c == '>') { needShift = true; return 0x37; }
  if (c == '/') return 0x38;  if (c == '?') { needShift = true; return 0x38; }
  if (c == '`') return 0x35;  if (c == '~') { needShift = true; return 0x35; }
  return 0;
}

static void typeASCII(const String& s, uint16_t charDown = 10) {
  for (size_t i=0;i<s.length();++i){ bool shift=false; uint8_t u=keyFromChar(s[i],shift); if(u) sendKey(u, shift?MOD_SHIFT:0, charDown); }
}

static bool resolveKeyWord(const String& kw, uint8_t& usage, uint8_t& mods) {
  String k=kw; k.toUpperCase(); usage=0; mods=0;
  if (k=="ENTER"||k=="RETURN") usage=KEY_ENTER;
  else if (k=="ESC"||k=="ESCAPE") usage=KEY_ESC;
  else if (k=="TAB") usage=KEY_TAB;
  else if (k=="SPACE") usage=KEY_SPACE;
  else if (k=="BACKSPACE"||k=="DELETE") usage=KEY_BACKSPACE;
  else if (k=="UP") usage=KEY_UP;
  else if (k=="DOWN") usage=KEY_DOWN;
  else if (k=="LEFT") usage=KEY_LEFT;
  else if (k=="RIGHT") usage=KEY_RIGHT;
  else if (k=="HOME") usage=KEY_HOME;
  else if (k=="END") usage=KEY_END;
  else if (k=="PGUP"||k=="PAGEUP") usage=KEY_PAGEUP;
  else if (k=="PGDN"||k=="PAGEDOWN") usage=KEY_PAGEDOWN;
  else if (k.startsWith("F")) { int n=k.substring(1).toInt(); if(n>=1&&n<=12) usage = KEY_F1+(n-1); }
  if (usage) return true;
  if (k.length()==1){ char c=k[0]; bool sh=false; uint8_t u=keyFromChar(c,sh); if(u){usage=u; mods=sh?MOD_SHIFT:0; return true;} }
  return false;
}
static uint8_t modsFromTokens(const std::vector<String>& toks, size_t& i) {
  uint8_t m=0;
  while(i<toks.size()){
    String t=toks[i]; t.toUpperCase();
    if(t=="CTRL"||t=="CONTROL") m|=MOD_CTRL;
    else if(t=="ALT") m|=MOD_ALT;
    else if(t=="SHIFT") m|=MOD_SHIFT;
    else if(t=="GUI"||t=="WIN"||t=="WINDOWS"||t=="CMD") m|=MOD_GUI;
    else break;
    ++i;
  }
  return m;
}
static uint32_t clampDelay(int v){ if(v<0)v=0; if(v>600000)v=600000; return (uint32_t)v; }
static void pressCombo(uint8_t usage, uint8_t mods, uint16_t downMs=50){
  if(!bleConnected) return;
  KbdReport idle{}; inputChr->setValue((uint8_t*)&idle,sizeof(idle)); inputChr->notify(); delay(3);
  KbdReport rpt{}; rpt.modifiers=mods; rpt.keys[0]=usage;
  inputChr->setValue((uint8_t*)&rpt,sizeof(rpt)); inputChr->notify(); delay(downMs);
  inputChr->setValue((uint8_t*)&idle,sizeof(idle)); inputChr->notify(); delay(5);
}
static bool execLine(const String& raw, uint32_t& interDelay, String& err){
  String line=raw; line.trim(); if(!line.length()) return true;
  if(line.startsWith("REM")) return true;

  std::vector<String> tok; { String tmp; for(size_t i=0;i<line.length();++i){ char c=line[i]; if(c==' '||c=='\t'){ if(tmp.length()){tok.push_back(tmp); tmp="";}} else tmp+=c;} if(tmp.length()) tok.push_back(tmp); }
  if(tok.empty()) return true;

  String cmd=tok[0]; String rest=line.substring(cmd.length()); rest.trim();
  String ucmd=cmd; ucmd.toUpperCase();

  if(ucmd=="DELAY"){ interDelay=clampDelay(rest.toInt()); delay(interDelay); return true; }
  if(ucmd=="DEFAULT_DELAY"||ucmd=="DEFAULTDELAY"){ defaultDelay=clampDelay(rest.toInt()); return true; }
  if(ucmd=="WAIT_FOR"){ delay(clampDelay(rest.toInt())); return true; }
  if(ucmd=="STRING"){ typeASCII(rest); if(defaultDelay) delay(defaultDelay); return true; }
  if(ucmd=="ENTER"){ sendKey(KEY_ENTER); if(defaultDelay) delay(defaultDelay); return true; }

  if(ucmd=="GUI"||ucmd=="WINDOWS"||ucmd=="WIN"){
    uint8_t usage=0; if(rest.length()){ uint8_t m=0; if(!resolveKeyWord(rest,usage,m)){ err=String("Unknown key: ")+rest; return false; } }
    if(!usage) usage=KEY_R; pressCombo(usage, MOD_GUI); if(defaultDelay) delay(defaultDelay); return true;
  }
  if(ucmd=="CTRL"||ucmd=="CONTROL"||ucmd=="ALT"||ucmd=="SHIFT"){
    size_t i=0; std::vector<String> tokens; { String s=line; String t; for(size_t j=0;j<s.length();++j){ char ch=s[j]; if(ch==' '||ch=='\t'){ if(t.length()){tokens.push_back(t); t="";}} else t+=ch;} if(t.length()) tokens.push_back(t); }
    i=0; uint8_t mods=modsFromTokens(tokens,i);
    if(i>=tokens.size()){ err="Missing key after modifiers"; return false; }
    uint8_t usage=0,dummy=0; if(!resolveKeyWord(tokens[i],usage,dummy)){ err=String("Unknown key: ")+tokens[i]; return false; }
    pressCombo(usage,mods); if(defaultDelay) delay(defaultDelay); return true;
  }
  if(ucmd=="TAB"||ucmd=="ESC"||ucmd=="SPACE"||ucmd=="BACKSPACE"||ucmd=="DELETE"||
     ucmd=="UP"||ucmd=="DOWN"||ucmd=="LEFT"||ucmd=="RIGHT"||
     ucmd=="HOME"||ucmd=="END"||ucmd=="PGUP"||ucmd=="PAGEUP"||ucmd=="PGDN"||ucmd=="PAGEDOWN"||
     (ucmd.length()==2&&ucmd[0]=='F'&&isDigit(ucmd[1]))||
     (ucmd.length()==3&&ucmd[0]=='F'&&isDigit(ucmd[1])&&isDigit(ucmd[2]))){
    uint8_t usage=0,m=0; if(!resolveKeyWord(ucmd,usage,m)){ err=String("Unknown key: ")+ucmd; return false; }
    pressCombo(usage,m); if(defaultDelay) delay(defaultDelay); return true;
  }
  if(ucmd=="REPEAT"){
    int n=(int)rest.toInt(); if(n<0)n=0; while(n--){ sendKey(KEY_ENTER); if(defaultDelay) delay(defaultDelay); }
    return true;
  }
  err=String("Unknown command: ")+cmd; return false;
}

static bool execFile(const String& path) {
  lastError = "";
  if (!bleConnected) { lastError="BLE not connected"; return false; }
  if (!mountSD())    { lastError="SD not mounted";   return false; }
  File f = SD.open(path, FILE_READ); if(!f){ lastError="Open failed"; return false; }

  defaultDelay = 0; uint32_t interDelay = 0;

  const int execTop = TOOLBAR_Y + HEADER_H + HEADER_H + 1;
  tft.fillRect(0, execTop, DISPLAY_WIDTH, DISPLAY_HEIGHT - execTop, COL_BG);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawCentreString("Executing…", DISPLAY_WIDTH/2, execTop + 4, 2);
  tft.drawString("File:", PADDING, execTop + 24, 2);
  tft.drawString(path, 48, execTop + 24, 2);

  if (bleConnected) { KbdReport idle{}; inputChr->setValue((uint8_t*)&idle,sizeof(idle)); inputChr->notify(); delay(10); }

  int y = execTop + 44;
  const int yMax = DISPLAY_HEIGHT - 6;

  ui_busy = true;

  while (f.available()) {
    String line = trimCR(f.readStringUntil('\n'));
    String err;
    if (!execLine(line, interDelay, err)) {
      lastError = err; f.close();
      if (bleConnected) { KbdReport idle{}; inputChr->setValue((uint8_t*)&idle,sizeof(idle)); inputChr->notify(); delay(10); }
      ui_busy = false;
      return false;
    }
    if (defaultDelay) delay(defaultDelay);

    if (y < yMax) {
      String disp = fitText(line, DISPLAY_WIDTH - 2*PADDING, 2);
      tft.drawString(disp, PADDING, y, 2); y += 18;
    }
    yield();
  }
  f.close();

  if (bleConnected) { KbdReport idle{}; inputChr->setValue((uint8_t*)&idle,sizeof(idle)); inputChr->notify(); delay(20); }

  tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - 40, DISPLAY_WIDTH - 2*PADDING, 28, 6, COL_ACCENT);
  tft.setTextColor(COL_BG, COL_ACCENT);
  tft.drawCentreString("Done!", DISPLAY_WIDTH/2, DISPLAY_HEIGHT - 36, 2);

  ui_busy = false;
  delay(500);
  return true;
}

static FeatureUI::Button listBtns[3];
static FeatureUI::Button detailsBtns[3];
static FeatureUI::Button confirmBtns[2];
static FeatureUI::Button infoBtn;

enum class HeaderType { TextOnly, IconBar, TextWithInfo };

static void drawHeader(HeaderType type, const String& title = "", bool drawFooterBg = true) {
  int currentY = TOOLBAR_Y;

  if (type == HeaderType::IconBar) {
    drawIconHeader();

    currentY = TOOLBAR_Y;
  }

  if (type == HeaderType::TextOnly || type == HeaderType::TextWithInfo) {

    tft.fillRect(0, currentY, DISPLAY_WIDTH, TEXT_HEADER_H, COL_BG);

    if (type == HeaderType::TextOnly && !title.isEmpty()) {

      tft.setTextColor(COL_FG, COL_BG);
      tft.setTextFont(2);
      tft.drawString(title, PADDING, currentY + 1, 2);
    }

    if (type == HeaderType::TextWithInfo) {

      int infoY = currentY + 6;
      tft.setTextFont(1);

      tft.setTextColor(bleConnected ? UI_OK : UI_WARN, COL_BG);
      const char* bleTxt = bleConnected ? "BLE: Connected" : "BLE: Not connected";
      tft.drawString(bleTxt, PADDING, infoY, 1);

      tft.setTextColor(sd_mounted ? UI_OK : UI_WARN, COL_BG);
      const char* sdTxt = sd_mounted ? "SD: OK" : "SD: Not mounted";
      int sdTw = tft.textWidth(sdTxt, 1);
      tft.drawString(sdTxt, DISPLAY_WIDTH - PADDING - sdTw, infoY, 1);
    }

    tft.drawLine(
      0,
      currentY + TEXT_HEADER_H,
      DISPLAY_WIDTH,
      currentY + TEXT_HEADER_H,
      UI_LINE
    );
  }

  if (drawFooterBg && type != HeaderType::IconBar) {
    FeatureUI::drawFooterBg();
  }
}

static uint32_t lastStatusPollMs = 0;
static bool lastHeaderBle = false;
static bool lastHeaderSd  = false;

static void refreshHeaderStatusIfNeeded(bool force = false) {
  auto showAndExitNoSD = [&](const char* title, const char* subtitle) {
    sd_mounted = false;

    tft.fillRect(0, TOOLBAR_Y - 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - (TOOLBAR_Y - 1), COL_BG);
    tft.setTextColor(UI_WARN, COL_BG);
    tft.setTextFont(2);
    tft.drawCentreString(title, DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 14, 2);
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString(subtitle, DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 10, 1);
    delay(900);

    ducky_active = false;
    feature_exit_requested = true;
  };

  static bool lastCardPresent = true;
  const uint32_t now = millis();
  if (force || (now - lastStatusPollMs) > 500) {
    lastStatusPollMs = now;
    pinMode(SD_CD, INPUT_PULLUP);
    bool cardPresent = !digitalRead(SD_CD);

    if (lastCardPresent && !cardPresent) {
      showAndExitNoSD("SD card removed", "Returning...");
      lastCardPresent = cardPresent;
      return;
    }
    lastCardPresent = cardPresent;
    if (!cardPresent) {
      sd_mounted = false;
    } else if (cardPresent && !sd_mounted) {

      mountSD();
    }
  }

  if (force || lastHeaderBle != bleConnected || lastHeaderSd != sd_mounted) {
    uiDrawn = false;
    drawIconHeader();

    drawHeader(HeaderType::TextWithInfo, "", false);
    lastHeaderBle = bleConnected;
    lastHeaderSd  = sd_mounted;
  }
}

static bool listUiValid = false;
static bool listLoadedOnce = false;
static int  lastListCount = -1;
static int  lastListStartItem = -1;
static int  lastListMaxVisible = -1;
static bool lastListHasScrollbar = false;
static int  lastListHeaderBottom = 0;
static int  lastListHintY = 0;
static int  lastListTop = 0;
static int  lastListRowStride = 0;
static int  lastListRw = 0;
static int  lastListRh = 0;

static void calcListLayout(int& headerBottom, int& hintY, int& listTop,
                           int& rowStride, int& maxVisibleItems,
                           bool& hasScrollbar, int& rw, int& rh) {
  const int barY = TOOLBAR_Y;
  headerBottom = barY + TEXT_HEADER_H + 1;
  rowStride = LIST_ROW_H + LIST_ROW_GAP;

  hintY   = headerBottom + 8;
  listTop = headerBottom + 22;
  maxVisibleItems = (DISPLAY_HEIGHT - FOOTER_H - listTop - 4) / rowStride;
  if (maxVisibleItems < 1) maxVisibleItems = 1;
  hasScrollbar = ((int)items.size() > maxVisibleItems);
  rw = DISPLAY_WIDTH - 2*PADDING - (hasScrollbar ? 12 : 0);
  rh = LIST_ROW_H - 2;
}

static void calcListWindow(int maxVisibleItems, int& startItem, int& endItem) {
  startItem = max(0, sel - maxVisibleItems / 2);
  endItem = min((int)items.size(), startItem + maxVisibleItems);
}

static void drawListHints(int hintY) {

  tft.fillRect(0, hintY - 1, DISPLAY_WIDTH, 14, COL_BG);
  if (items.empty()) return;

  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, COL_BG);

  char counter[20];
  snprintf(counter, sizeof(counter), "%d/%d", sel + 1, (int)items.size());
  int counterWidth = tft.textWidth(counter, 1);
  tft.drawString(counter, DISPLAY_WIDTH - counterWidth - PADDING, hintY, 1);

  tft.drawString("Double-tap to execute", PADDING, hintY, 1);
}

static void drawListScrollbar(int listTop, int maxVisibleItems, int startItem) {
  if ((int)items.size() <= maxVisibleItems) return;
  int scrollBarX = DISPLAY_WIDTH - 8;
  int scrollBarHeight = DISPLAY_HEIGHT - FOOTER_H - listTop - 8;
  int scrollBarY = listTop + 4;
  int indicatorHeight = (maxVisibleItems * scrollBarHeight) / (int)items.size();
  int indicatorY = scrollBarY + (startItem * (scrollBarHeight - indicatorHeight)) / (int)items.size();

  tft.fillRect(scrollBarX, scrollBarY, 4, scrollBarHeight, UI_LINE);
  tft.fillRect(scrollBarX, indicatorY, 4, indicatorHeight, COL_ACCENT);
}

static void drawListRow(int i, int startItem, int listTop, int rowStride,
                        int rx, int rw, int rh) {
  const int maxVisibleItems = lastListMaxVisible;
  const int endItem = min((int)items.size(), startItem + maxVisibleItems);
  if (i < startItem || i >= endItem) return;

  int y = listTop + (i - startItem) * rowStride;
  bool selected = (i == sel);
  uint16_t body = selected ? COL_ROWSEL : COL_BG;
  uint16_t border = selected ? COL_ACCENT : UI_LINE;

  int wedgeX = rx - 8; if (wedgeX < 0) wedgeX = 0;
  tft.fillRect(wedgeX, y, rx - wedgeX, rh, COL_BG);

  tft.fillRoundRect(rx, y, rw, rh, 6, body);
  tft.drawRoundRect(rx, y, rw, rh, 6, border);

  if (selected) {
    tft.fillTriangle(rx - 6, y + rh/2 - 4, rx - 6, y + rh/2 + 4, rx - 2, y + rh/2, COL_ACCENT);
  }

  tft.setTextColor(COL_FG, body);
  tft.setTextFont(2);
  String name = fitText(items[i].name, rw - 40, 2);
  tft.drawString(name, rx + 12, y + 6, 2);

  uint32_t kb = (items[i].size + 1023) / 1024;
  char sbuf[16];
  snprintf(sbuf, sizeof(sbuf), "%lukB", (unsigned long)kb);
  tft.setTextFont(1);
  int sx = rx + rw - tft.textWidth(sbuf, 1) - 8;
  tft.setTextColor(selected ? UI_OK : UI_TEXT, body);
  tft.drawString(sbuf, sx, y + 8, 1);

  tft.setTextFont(1);
  String desc = items[i].desc.length() ? items[i].desc : "(no description)";
  desc = fitText(desc, rw - 24, 1);
  tft.setTextColor(UI_TEXT, body);
  tft.drawString(desc, rx + 12, y + 26, 1);
}

static void drawListContentFull() {
  const int rx = PADDING;

  int headerBottom, hintY, listTop, rowStride, maxVisibleItems, rw, rh;
  bool hasScrollbar;
  calcListLayout(headerBottom, hintY, listTop, rowStride, maxVisibleItems, hasScrollbar, rw, rh);

  tft.fillRect(0, headerBottom, DISPLAY_WIDTH, DISPLAY_HEIGHT - headerBottom - FOOTER_H, COL_BG);

  int startItem, endItem;
  calcListWindow(maxVisibleItems, startItem, endItem);

  drawListScrollbar(listTop, maxVisibleItems, startItem);

  int y = listTop;
  for (int i = startItem; i < endItem; ++i) {
    bool selected = (i == sel);
    uint16_t body = selected ? COL_ROWSEL : COL_BG;
    uint16_t border = selected ? COL_ACCENT : UI_LINE;

    tft.fillRoundRect(rx, y, rw, rh, 6, body);
    tft.drawRoundRect(rx, y, rw, rh, 6, border);
    if (selected) {
      tft.fillTriangle(rx - 6, y + rh/2 - 4, rx - 6, y + rh/2 + 4, rx - 2, y + rh/2, COL_ACCENT);
    }

    tft.setTextColor(COL_FG, body);
    tft.setTextFont(2);
    String name = fitText(items[i].name, rw - 40, 2);
    tft.drawString(name, rx + 12, y + 6, 2);

    uint32_t kb = (items[i].size + 1023) / 1024;
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%lukB", (unsigned long)kb);
    tft.setTextFont(1);
    int sx = rx + rw - tft.textWidth(sbuf, 1) - 8;
    tft.setTextColor(selected ? UI_OK : UI_TEXT, body);
    tft.drawString(sbuf, sx, y + 8, 1);

    tft.setTextFont(1);
    String desc = items[i].desc.length() ? items[i].desc : "(no description)";
    desc = fitText(desc, rw - 24, 1);
    tft.setTextColor(UI_TEXT, body);
    tft.drawString(desc, rx + 12, y + 26, 1);

    y += rowStride;
  }

  drawListHints(hintY);

  listUiValid = true;
  lastListCount = (int)items.size();
  lastListStartItem = startItem;
  lastListMaxVisible = maxVisibleItems;
  lastListHasScrollbar = hasScrollbar;
  lastListHeaderBottom = headerBottom;
  lastListHintY = hintY;
  lastListTop = listTop;
  lastListRowStride = rowStride;
  lastListRw = rw;
  lastListRh = rh;
}

static void updateListSelection(int newSel) {
  if (newSel == sel) return;
  int oldSel = sel;
  sel = newSel;
  clampSel();

  if (!sd_mounted || items.empty() || !listUiValid) {
    drawListPage(false);
    return;
  }

  int headerBottom, hintY, listTop, rowStride, maxVisibleItems, rw, rh;
  bool hasScrollbar;
  calcListLayout(headerBottom, hintY, listTop, rowStride, maxVisibleItems, hasScrollbar, rw, rh);

  int startItem, endItem;
  calcListWindow(maxVisibleItems, startItem, endItem);

  if ((int)items.size() != lastListCount ||
      startItem != lastListStartItem ||
      maxVisibleItems != lastListMaxVisible ||
      hasScrollbar != lastListHasScrollbar ||
      headerBottom != lastListHeaderBottom ||
      hintY != lastListHintY ||
      listTop != lastListTop ||
      rowStride != lastListRowStride ||
      rw != lastListRw ||
      rh != lastListRh) {
    drawListContentFull();
    return;
  }

  const int rx = PADDING;
  drawListRow(oldSel, startItem, listTop, rowStride, rx, rw, rh);
  drawListRow(sel,    startItem, listTop, rowStride, rx, rw, rh);
  drawListHints(hintY);
}

static void drawListPage(bool forceReload) {
  dialog = Dialog::None;

  mountSD();

  drawHeader(HeaderType::IconBar);
  drawHeader(HeaderType::TextWithInfo, "BLE Ducky");

  listUiValid = false;

  if (!sd_mounted) {
    listLoadedOnce = false;
    tft.setTextColor(COL_FG, COL_BG);
    tft.drawCentreString("SD not mounted", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 8, 2);
  } else {
    if (forceReload || !listLoadedOnce) {
      reloadList();
      listLoadedOnce = true;
    }
    clampSel();
    drawListContentFull();
  }
  FeatureUI::layoutFooter3(
    listBtns,
    "Open",    FeatureUI::ButtonStyle::Primary,
    "Refresh", FeatureUI::ButtonStyle::Secondary,
    "Exit",    FeatureUI::ButtonStyle::Secondary
  );
  for (auto& b: listBtns) FeatureUI::drawButton(b);
}

static void drawDetailsPage() {

  drawHeader(HeaderType::IconBar);
  drawHeader(HeaderType::TextWithInfo, "Script details");

  tft.setTextColor(COL_FG, COL_BG);
  const int barY = TOOLBAR_Y;
  const int baseY = barY + TEXT_HEADER_H + 1;

  tft.fillRect(0, baseY, DISPLAY_WIDTH, DISPLAY_HEIGHT - baseY - FOOTER_H, COL_BG);

  if (items.empty()) {
    tft.drawCentreString("No script selected", DISPLAY_WIDTH/2, baseY+24, 2);
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString("Use 'Refresh' to scan for scripts", DISPLAY_WIDTH/2, baseY+48, 1);
  }
  else {
    auto& it = items[sel];
    int y = baseY + 8;

    tft.setTextFont(2);

    tft.setTextColor(UI_TEXT, COL_BG);
    String displayName = fitText(it.name, DISPLAY_WIDTH - 2*PADDING, 2);
    tft.drawString(displayName, PADDING, y, 2);
    y += 24;

    tft.drawLine(PADDING, y, DISPLAY_WIDTH - PADDING, y, UI_LINE);
    y += 8;

    tft.setTextFont(2);
    tft.setTextColor(COL_FG, COL_BG);

    tft.drawString("Size:", PADDING, y, 2);
    tft.drawNumber(it.size, 100, y, 2);
    tft.drawString("bytes", 140, y, 2);
    y += 28;

    tft.drawString("Path:", PADDING, y, 2);
    tft.setTextFont(1);
    String path = fitText(it.path, DISPLAY_WIDTH - 100 - PADDING, 1);
    tft.drawString(path, 100, y + 2, 1);
    y += 28;

    tft.setTextFont(2);
    tft.drawString("Description:", PADDING, y, 2);
    y += 24;

    tft.setTextFont(1);
    String desc = it.desc.length() ? it.desc : "No description available";

    int descY = y;
    int maxWidth = DISPLAY_WIDTH - 2*PADDING;
    String remaining = desc;
    const int bottomY = DISPLAY_HEIGHT - FOOTER_H - 6;
    while (remaining.length() > 0 && descY < bottomY) {
      String line;
      int consumed = wrapLineNoEllipsis(remaining, maxWidth, 1, line);
      if (consumed <= 0) break;
      tft.drawString(line, PADDING, descY, 1);
      remaining = remaining.substring(consumed);
      remaining.trim();
      descY += 14;
    }

  }
  FeatureUI::layoutFooter3(
    detailsBtns,
    "Execute", FeatureUI::ButtonStyle::Primary,
    "Delete",  FeatureUI::ButtonStyle::Danger,
    "Back",    FeatureUI::ButtonStyle::Secondary
  );
  for (auto& b: detailsBtns) FeatureUI::drawButton(b);
}

static void drawSettingsPage() {

  drawHeader(HeaderType::IconBar);
  drawHeader(HeaderType::TextWithInfo, "Info");

  tft.setTextColor(COL_FG, COL_BG);
  const int barY = TOOLBAR_Y;
  const int baseY = barY + TEXT_HEADER_H + 1;

  tft.fillRect(0, baseY, DISPLAY_WIDTH, DISPLAY_HEIGHT - baseY - FOOTER_H, COL_BG);

  const int rowGap = 24;
  int y = baseY + 8;

  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, COL_BG);
  tft.drawString("Info", PADDING, y, 2);
  y += rowGap;

  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("Default Delay:", PADDING, y, 2);
  tft.drawNumber(defaultDelay, 150, y, 2);
  tft.drawString("ms", 190, y, 2);
  y += rowGap;

  tft.drawString("BLE Status:", PADDING, y, 2);
  tft.setTextColor(bleConnected ? UI_OK : UI_WARN, COL_BG);
  tft.drawString(bleConnected ? "Connected" : "Disconnected", 150, y, 2);
  tft.setTextColor(COL_FG, COL_BG);
  y += rowGap;

  tft.drawString("SD Status:", PADDING, y, 2);
  tft.setTextColor(sd_mounted ? UI_OK : UI_WARN, COL_BG);
  tft.drawString(sd_mounted ? "Mounted" : "Not mounted", 150, y, 2);
  tft.setTextColor(COL_FG, COL_BG);
  y += rowGap;

  tft.drawString("Scripts Found:", PADDING, y, 2);
  tft.drawNumber((int)items.size(), 150, y, 2);
  y += rowGap;

  tft.drawString("Device Name:", PADDING, y, 2);
  tft.drawString(DEV_NAME, 150, y, 2);
  y += rowGap;

  y += 4;
  tft.setTextFont(1);
  tft.drawString("Use icons above for quick actions", PADDING, y, 1);
  y += 14;
  tft.drawString("Touch and hold for more options", PADDING, y, 1);

  FeatureUI::layoutFooter1(infoBtn, "Back", FeatureUI::ButtonStyle::Secondary);
  FeatureUI::drawButton(infoBtn);
}

static void drawConfirmDelete() {

  const int w = DISPLAY_WIDTH - 2*PADDING;
  const int h = 96;
  const int x = PADDING;
  const int y = DISPLAY_HEIGHT/2 - h/2;

  tft.fillRoundRect(x, y, w, h, 8, UI_FG);
  tft.drawRoundRect(x, y, w, h, 8, UI_LINE);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawCentreString("Delete this script?", DISPLAY_WIDTH/2, y + 10, 2);
  tft.setTextColor(UI_TEXT, UI_FG);
  tft.drawCentreString("(This cannot be undone)", DISPLAY_WIDTH/2, y + 30, 2);

  int bw = (w - 3*PADDING)/2;
  int bx1 = x + PADDING;
  int bx2 = x + 2*PADDING + bw;
  int by  = y + h - PADDING - BTN_H;

  confirmBtns[0] = { (int16_t)bx1, (int16_t)by, (int16_t)bw, (int16_t)BTN_H, "Yes", FeatureUI::ButtonStyle::Danger, false };
  confirmBtns[1] = { (int16_t)bx2, (int16_t)by, (int16_t)bw, (int16_t)BTN_H, "No",  FeatureUI::ButtonStyle::Secondary, false };

  FeatureUI::drawButton(confirmBtns[0]);
  FeatureUI::drawButton(confirmBtns[1]);
}

static void startBleKeyboard(const char* devName) {

  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  hid = new NimBLEHIDDevice(server);
  inputChr  = hid->inputReport(1);
  outputChr = hid->outputReport(1);
  outputChr->setCallbacks(new OutputCB());

  static const uint8_t REPORT_MAP[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x85,0x01, 0x05,0x07, 0x19,0xE0, 0x29,0xE7,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x01,
    0x75,0x08, 0x95,0x06, 0x15,0x00, 0x25,0x65, 0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x95,0x05, 0x75,0x01, 0x91,0x02,
    0x95,0x01, 0x75,0x03, 0x91,0x01,
    0xC0
  };

  hid->manufacturer()->setValue("ESP32 Community");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x02);
  hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  hid->startServices();
  hid->setBatteryLevel(100);

  auto* sec = new NimBLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  sec->setCapability(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setAppearance(0x03C1);

  advData.setName(devName);

  std::string msd; msd.push_back(0x06); msd.push_back(0x00); msd.push_back(0x00); msd.push_back(0x00); msd.push_back((char)0x80);
  advData.setManufacturerData(msd);

  NimBLEAdvertisementData scanRsp; scanRsp.setName(devName);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv) adv->stop();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanRsp);
  adv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
  adv->setMinInterval(0x00A0);
  adv->setMaxInterval(0x0140);
  adv->start();
}

void enter() {

  pinMode(SD_CD, INPUT_PULLUP);
  if (digitalRead(SD_CD)) {

    tft.fillRect(0, TOOLBAR_Y - 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - (TOOLBAR_Y - 1), COL_BG);
    tft.setTextColor(UI_WARN, COL_BG);
    tft.setTextFont(2);
    tft.drawCentreString("SD card not detected", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 14, 2);
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString("Returning...", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 10, 1);
    delay(900);
    ducky_active = false;
    feature_exit_requested = true;
    return;
  }

  ducky_active = true;
  ui_busy = false;
  dialog = Dialog::None;
  ui_inited = false;

  if (!hid) startBleKeyboard(DEV_NAME);
  else {
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setAppearance(0x03C1);
    advData.setName(DEV_NAME);
    std::string msd; msd.push_back(0x06); msd.push_back(0x00); msd.push_back(0x00); msd.push_back(0x00); msd.push_back((char)0x80);
    advData.setManufacturerData(msd);
    NimBLEAdvertisementData scanRsp; scanRsp.setName(DEV_NAME);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (adv) {
      adv->stop();
      adv->setAdvertisementData(advData);
      adv->setScanResponseData(scanRsp);
      adv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
      adv->setMinInterval(0x00A0);
      adv->setMaxInterval(0x0140);
      adv->start();
    }
  }

  if (hid && inputChr && bleConnected) {
    KbdReport idle{}; inputChr->setValue((uint8_t*)&idle, sizeof(idle)); inputChr->notify();
  }

  if (!mountSD() || !ensureDuckyDir()) {
    tft.fillRect(0, TOOLBAR_Y - 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - (TOOLBAR_Y - 1), COL_BG);
    tft.setTextColor(UI_WARN, COL_BG);
    tft.setTextFont(2);
    tft.drawCentreString("SD mount failed", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 14, 2);
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString("Returning...", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 10, 1);
    delay(900);
    ducky_active = false;
    feature_exit_requested = true;
    return;
  }
  needsRedraw = true;
}

bool active() { return ducky_active; }

void exit() {

  ducky_active = false;
  ui_busy = false;
  dialog = Dialog::None;
  uiDrawn = false;

  if (hid && inputChr && bleConnected) {
    KbdReport idle{};
    inputChr->setValue((uint8_t*)&idle, sizeof(idle));
    inputChr->notify();
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv) {
    adv->stop();

    NimBLEAdvertisementData empty;
    adv->setAdvertisementData(empty);
    adv->setScanResponseData(empty);
  }
}
void setup() {
  ducky_active = false;
  tft.setTextDatum(TL_DATUM);
  mountSD(); ensureDuckyDir();

  ui_inited = false; needsRedraw = true; ui_busy = false; dialog = Dialog::None; uiDrawn = false;
}

void loop() {

  if (!ui_inited) {

    tft.fillRect(0, TOOLBAR_Y - 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - (TOOLBAR_Y - 1), COL_BG);
    sel = 0;
    page = Page::List;
    uiDrawn = false;
    drawListPage();
    ui_inited = true;
    needsRedraw = false;
    refreshHeaderStatusIfNeeded(true);
  }

  updateIconAnimation();

  refreshHeaderStatusIfNeeded(false);
  if (feature_exit_requested) return;

  if (ui_busy) { delay(5); return; }

  if (dialog != Dialog::ConfirmDelete) {
    if (page == Page::List) {
      if (pcfPressedEdge(ebUp))   { if (sel>0){ updateListSelection(sel - 1); } else {  } return; }
      if (pcfPressedEdge(ebDown)) { if (sel < (int)items.size()-1){ updateListSelection(sel + 1); } return; }
      if (pcfPressedEdge(ebRight) || pcfPressedEdge(ebSelect)) { if (!items.empty()) { page = Page::Details; drawDetailsPage(); } return; }
      if (pcfPressedEdge(ebLeft)) {

        ducky_active = false;
        feature_exit_requested = true;
        return;
      }
    } else if (page == Page::Details) {
      if (pcfPressedEdge(ebRight) || pcfPressedEdge(ebSelect)) {
        if (!bleConnected) {
          tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - FOOTER_H - 28, DISPLAY_WIDTH - 2*PADDING, 22, 6, COL_WARN);
          tft.setTextColor(COL_BG, COL_WARN);
          tft.drawCentreString("Not paired/connected", DISPLAY_WIDTH/2, DISPLAY_HEIGHT - FOOTER_H - 24, 2);
        } else if (!items.empty()) {
          bool ok = execFile(items[sel].path);
          drawDetailsPage();
          if (!ok) {
            tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - FOOTER_H - 28, DISPLAY_WIDTH - 2*PADDING, 22, 6, COL_WARN);
            tft.setTextColor(COL_BG, COL_WARN);
            tft.drawCentreString(String("Error: ") + lastError, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - FOOTER_H - 24, 2);
          }
        }
        return;
      }
      if (pcfPressedEdge(ebLeft)) { page = Page::List; drawListPage(false); return; }
    } else if (page == Page::Settings) {
      if (pcfPressedEdge(ebLeft)) { page = Page::List; drawListPage(false); return; }
    }
  }
Tap tap = readTap();
  if (!tap.has) { delay(5); return; }

  if (dialog == Dialog::ConfirmDelete) {
    int id = FeatureUI::hit(confirmBtns, 2, tap.x, tap.y);
    if (id == 0) {
      if (!items.empty() && mountSD() && SD.exists(items[sel].path)) {
        SD.remove(items[sel].path.c_str());
        reloadList();
        page = Page::List;
        dialog = Dialog::None;
        drawListPage(false);
      } else {
        dialog = Dialog::None;
        drawDetailsPage();
      }
    } else if (id == 1) {
      dialog = Dialog::None;
      drawDetailsPage();
    } else {

    }
    return;
  }

  handleIconTouch(tap.x, tap.y);

  bool doubleTap = isDoubleTap(tap);

  if (page == Page::List) {
    int id = FeatureUI::hit(listBtns, 3, tap.x, tap.y);
    if (id == 0) {
      if (!items.empty()) { page = Page::Details; drawDetailsPage(); }
    } else if (id == 1) {
      drawListPage(true);
    } else if (id == 2) {
      ducky_active = false;
      feature_exit_requested = true;
      return;
    } else {

      int headerBottom, hintY, listTop, rowStride, maxVisibleItems, rw, rh;
      bool hasScrollbar;
      calcListLayout(headerBottom, hintY, listTop, rowStride, maxVisibleItems, hasScrollbar, rw, rh);

      int y = tap.y;
      if (y >= listTop && y < DISPLAY_HEIGHT - FOOTER_H) {
        int startItem, endItem;
        calcListWindow(maxVisibleItems, startItem, endItem);
        int clickedRow = (y - listTop) / rowStride;
        int actualIdx = startItem + clickedRow;

        if (actualIdx >= 0 && actualIdx < (int)items.size()) {
          if (doubleTap && bleConnected) {

            bool ok = execFile(items[actualIdx].path);
            drawListPage(false);
            if (!ok) {
              tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - FOOTER_H - 28, DISPLAY_WIDTH - 2*PADDING, 22, 6, COL_WARN);
              tft.setTextColor(COL_BG, COL_WARN);
              tft.drawCentreString(String("Error: ") + lastError, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - FOOTER_H - 24, 2);
            }
          } else {

            updateListSelection(actualIdx);
          }
        }
      }
    }

  } else if (page == Page::Details) {
    int id = FeatureUI::hit(detailsBtns, 3, tap.x, tap.y);
    if (id == 0) {
      if (!bleConnected) {
        tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - FOOTER_H - 28, DISPLAY_WIDTH - 2*PADDING, 22, 6, COL_WARN);
        tft.setTextColor(COL_BG, COL_WARN);
        tft.drawCentreString("Not paired/connected", DISPLAY_WIDTH/2, DISPLAY_HEIGHT - FOOTER_H - 24, 2);
      } else if (!items.empty()) {
        bool ok = execFile(items[sel].path);

        drawDetailsPage();
        if (!ok) {
          tft.fillRoundRect(PADDING, DISPLAY_HEIGHT - FOOTER_H - 28, DISPLAY_WIDTH - 2*PADDING, 22, 6, COL_WARN);
          tft.setTextColor(COL_BG, COL_WARN);
          tft.drawCentreString(String("Error: ") + lastError, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - FOOTER_H - 24, 2);
        }
      }
    } else if (id == 1) {
      dialog = Dialog::ConfirmDelete;
      drawConfirmDelete();
    } else if (id == 2) {
      page = Page::List; drawListPage(false);
    }
  } else if (page == Page::Settings) {
    int id = FeatureUI::hit(&infoBtn, 1, tap.x, tap.y);
    if (id == 0) {
      page = Page::List; drawListPage(false);
    }
  }

  delay(5);
}

}

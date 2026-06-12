#include "shared.h"
#include "SettingsStore.h"


UiPalette UI = { BG_Dark, FG_Dark, ICON_Dark, TEXT_Dark, 0x3166, LINE_Dark, L_Dark, 0xFBE4, GREEN };

uint16_t uiUniversalColor() {
  return UI.warn;
}

void applyThemeToPalette(Theme t) {
  const uint16_t universal = accentColor565(settings().accentColor);

  if (t == Theme::Light) {
    UI.bg     =  BG_Light;
    UI.fg     =  FG_Light;
    UI.icon   =  universal;
    UI.text   =  TEXT_Light;
    UI.line   =  LINE_Light;
    UI.accent =  UI_ACCENT;
    UI.lable  =  L_Light;
    UI.warn   =  universal;
    UI.ok     =  GREEN;

  } else {
    UI.bg     =  BG_Dark;
    UI.fg     =  FG_Dark;
    UI.icon   =  universal;
    UI.text   =  TEXT_Dark;
    UI.line   =  LINE_Dark;
    UI.accent =  UI_ACCENT;
    UI.lable  =  L_Dark;
    UI.warn   =  universal;
    UI.ok     =  GREEN;
  }
}

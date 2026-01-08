#include "shared.h"


UiPalette UI = { UI_BG, UI_FG, UI_ICON, UI_TEXT, UI_ACCENT, UI_LINE, UI_LABLE, UI_WARN, UI_OK };

void applyThemeToPalette(Theme t) {
  if (t == Theme::Light) {
    UI.bg = BG_Light;
    UI.fg = FG_Light;
    UI.icon = ICON_Light;
    UI.text = TEXT_Light;
    UI.line = LINE_Light;
    UI.accent = UI_ACCENT;
    UI.lable = L_Light;
    UI.warn = ORANGE;
    UI.ok = GREEN;

  } else {
    UI.bg = BG_Dark;
    UI.fg = FG_Dark;
    UI.icon = ICON_Dark;
    UI.text = TEXT_Dark;
    UI.line = LINE_Dark;
    UI.accent = UI_ACCENT;
    UI.lable = L_Dark;
    UI.warn = ORANGE;
    UI.ok = GREEN;
  }
}

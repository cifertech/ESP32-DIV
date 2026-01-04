#ifndef SHARED_H
#define SHARED_H

// ========== SHREDDY COLOR PALETTE ==========
// Core colors
const uint16_t SHREDDY_TEAL = 0x2698;       // #23D2C3 - Primary teal/aqua
const uint16_t SHREDDY_PINK = 0xF8B4;       // #FF16A0 - Hot pink/magenta
const uint16_t SHREDDY_BLACK = 0x0000;      // #000000 - Pure black

// Accent colors
const uint16_t SHREDDY_BLUE = 0x067F;       // #00CFFF - Electric blue
const uint16_t SHREDDY_PURPLE = 0x895C;     // #8A2BE2 - Neon purple
const uint16_t SHREDDY_GREEN = 0x3FE2;      // #39FF14 - Neon green
const uint16_t SHREDDY_GUNMETAL = 0x18E3;   // #1C1C1C - Gunmetal gray

// Legacy colors (mapped to shreddy palette)
const uint16_t ORANGE = SHREDDY_PINK;       // Use pink instead of orange
const uint16_t GRAY = 0x8410;
const uint16_t BLUE = SHREDDY_BLUE;
const uint16_t RED = 0xF800;
const uint16_t GREEN = SHREDDY_GREEN;
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GRAY = 0xC618;
const uint16_t DARK_GRAY = SHREDDY_GUNMETAL;

#define TFT_DARKBLUE  0x3166
#define TFT_LIGHTBLUE SHREDDY_BLUE
#define TFTWHITE     0xFFFF
#define TFT_GRAY      0x8410
#define SELECTED_ICON_COLOR SHREDDY_PINK

void displaySubmenu();

extern bool in_sub_menu;                
extern bool feature_active;             
extern bool submenu_initialized;        
extern bool is_main_menu;              
extern bool feature_exit_requested;


#endif // SHARED_H

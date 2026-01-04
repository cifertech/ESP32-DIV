#ifndef SHARED_H
#define SHARED_H


const uint16_t ORANGE = 0xfbe4;
const uint16_t GRAY = 0x8410;
const uint16_t BLUE = 0x001F;
const uint16_t RED = 0xF800;
const uint16_t GREEN = 0x07E0;
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GRAY = 0xC618;
const uint16_t DARK_GRAY = 0x4208;

#define TFT_DARKBLUE  0x3166  
#define TFT_LIGHTBLUE 0x051F  
#define TFTWHITE     0xFFFF  
#define TFT_GRAY      0x8410  
#define SELECTED_ICON_COLOR 0xfbe4

void displaySubmenu();

extern bool in_sub_menu;                
extern bool feature_active;             
extern bool submenu_initialized;        
extern bool is_main_menu;              
extern bool feature_exit_requested;


#endif // SHARED_H

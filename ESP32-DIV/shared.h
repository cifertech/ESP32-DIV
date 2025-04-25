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

void displaySubmenu();

extern bool in_sub_menu;        // Declare in_sub_menu globally
extern bool feature_active;     // Already declared in Touchscreen.h, but include for clarity
extern bool submenu_initialized; // If used elsewhere, declare it here
extern bool is_main_menu;       // If used, declare it here
extern bool feature_exit_requested;


#endif // SHARED_H

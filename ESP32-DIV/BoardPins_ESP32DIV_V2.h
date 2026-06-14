#pragma once

/*
 * ESP32-DIV V2/V2.1 hardware map.
 *
 * This file is intentionally documentary for now. Do not include it from
 * shared.h until each pin has been validated on the target hardware.
 */

#define ESP32DIV_V2_HW_INTERNAL_DISPLAY_TOUCH 1
#define ESP32DIV_V2_HW_INTERNAL_SD            1
#define ESP32DIV_V2_HW_INTERNAL_BUTTONS       1
#define ESP32DIV_V2_HW_INTERNAL_BUZZER        1
#define ESP32DIV_V2_HW_INTERNAL_WS2812B       1

#define ESP32DIV_V2_HW_EXTERNAL_CC1101        1
#define ESP32DIV_V2_HW_EXTERNAL_NRF24         1
#define ESP32DIV_V2_HW_EXTERNAL_IR            1

#define ESP32DIV_V2_HW_FUTURE_PN532           1
#define ESP32DIV_V2_HW_FUTURE_GPS             1

/*
 * Buzzer
 *
 * Validated on target hardware: buzzer is on IO2.
 */
#define ESP32DIV_V2_BUZZER_PIN_CANDIDATE      2
#define ESP32DIV_V2_BUZZER_PIN_CONFIRMED      1

/*
 * WS2812B status LEDs
 *
 * V2 schematic: D1 DIN is driven from IO1 through R33 (1k). D1/D2 then chain
 * through WSD to D3/D4.
 */
#define ESP32DIV_V2_WS2812B_COUNT             4
#define ESP32DIV_V2_WS2812B_PIN_CANDIDATE     1
#define ESP32DIV_V2_WS2812B_PIN_CONFIRMED     1

/*
 * Future PN532 RFID/NFC module
 *
 * PN532 is not installed yet. Existing defaults in shared.h use SPI and can
 * conflict with NRF24 if the chip-select line overlaps.
 */
#define ESP32DIV_V2_PN532_INSTALLED           0
#define ESP32DIV_V2_PN532_INTERFACE_SPI       1
#define ESP32DIV_V2_PN532_CONFLICTS_NRF24_CS  1

/*
 * Future GPS module
 *
 * GPS is not installed yet. Existing default UART pins for the ESP32-S3 target
 * are GPIO 47/48, which overlap the default NRF24 #2 CE/CSN pins.
 */
#define ESP32DIV_V2_GPS_INSTALLED             0
#define ESP32DIV_V2_GPS_INTERFACE_UART        1
#define ESP32DIV_V2_GPS_RX_PIN_CANDIDATE      47
#define ESP32DIV_V2_GPS_TX_PIN_CANDIDATE      48
#define ESP32DIV_V2_GPS_CONFLICTS_NRF24_2     1

/*
 * Known pin conflicts from current shared.h defaults.
 *
 * - PN532_SS defaults to GPIO 4, same as NRF24 #1 CSN on the ESP32-S3 profile.
 * - GPS UART defaults to GPIO 47/48, same as NRF24 #2 CE/CSN.
 * - IR defaults to GPIO 21/14, same as NRF24 #3 CSN/CE.
 */
#define ESP32DIV_V2_CONFLICT_PN532_NRF24_1    1
#define ESP32DIV_V2_CONFLICT_GPS_NRF24_2      1
#define ESP32DIV_V2_CONFLICT_IR_NRF24_3       1

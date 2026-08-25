#pragma once
#include <Adafruit_NeoPixel.h>

// 定义NeoPixel引脚和数量
#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN 1  // ESP32-DIV开发板的NeoPixel引脚
#endif

#ifndef NEOPIXEL_COUNT
#define NEOPIXEL_COUNT 4  // NeoPixel数量
#endif

// 声明NeoPixel对象
extern Adafruit_NeoPixel strip;

// 初始化NeoPixel
void initNeoPixel();

// 更新NeoPixel状态
void updateNeoPixel();

// 设置NeoPixel颜色
void setNeoPixelColor(uint8_t r, uint8_t g, uint8_t b);

// 切换NeoPixel开关
void toggleNeoPixel(bool enabled);

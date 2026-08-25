#include "neopixel.h"
#include "SettingsStore.h"
#include "shared.h"

// 创建NeoPixel对象
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// 颜色渐变变量
static uint8_t hue = 0;
static bool neoPixelEnabled = false;

// 初始化NeoPixel
void initNeoPixel() {
  strip.begin();
  strip.setBrightness(NEOPIXEL_BRIGHT_MAX);
  strip.show(); // 初始化时关闭

  // 从设置中获取初始状态
  neoPixelEnabled = settings().neopixelEnabled;
  if (neoPixelEnabled) {
    updateNeoPixel();
  }
}

// 更新NeoPixel状态
void updateNeoPixel() {
  if (neoPixelEnabled) {
    // 生成彩虹渐变颜色
    uint32_t color = strip.ColorHSV(hue * 65536 / 256, 255, NEOPIXEL_BRIGHT_MAX);
    strip.fill(color);
    strip.show();

    // 增加色相值，实现颜色变化
    hue++;
  } else {
    // 关闭NeoPixel
    strip.clear();
    strip.show();
  }
}

// 设置NeoPixel颜色
void setNeoPixelColor(uint8_t r, uint8_t g, uint8_t b) {
  if (neoPixelEnabled) {
    strip.fill(strip.Color(r, g, b));
    strip.show();
  }
}

// 切换NeoPixel开关
void toggleNeoPixel(bool enabled) {
  neoPixelEnabled = enabled;
  updateNeoPixel();
}

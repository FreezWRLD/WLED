#ifndef OLED_BASE_H
#define OLED_BASE_H

#include "wled.h"
#include <U8x8lib.h>

class OledBaseUsermod : public Usermod {
public:
  static U8X8_SSD1306_128X64_NONAME_HW_I2C* display;
  static bool ready;

  static U8X8_SSD1306_128X64_NONAME_HW_I2C* getDisplay();
  static bool isReady();
  static uint8_t getActiveView();
  static void setViewActive(uint8_t viewId, bool enabled);
  static bool isViewActive(uint8_t viewId);
  static bool isCurrentView(uint8_t viewId);
  static void nextView();
};

#endif

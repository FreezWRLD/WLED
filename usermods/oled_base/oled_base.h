#ifndef OLED_BASE_H
#define OLED_BASE_H

#include "wled.h"
#include <U8x8lib.h>

class OledBaseUsermod : public Usermod {
public:
  static U8X8_SSD1306_128X64_NONAME_HW_I2C* getDisplay();
  static U8X8_SSD1306_128X64_NONAME_HW_I2C* display;
  static bool ready;
  uint16_t getId() override;
};

extern OledBaseUsermod oled_base_mod;

#endif

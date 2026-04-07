#include "wled.h"
#include "oled_base.h"
#include <U8x8lib.h>
#include <Wire.h>

#ifndef OLED_SDA_PIN
  #define OLED_SDA_PIN 21
#endif
#ifndef OLED_SCL_PIN
  #define OLED_SCL_PIN 22
#endif

class OledBaseUsermodImpl : public OledBaseUsermod {
public:
  static uint8_t activeView;
  static bool autoCycle;
  static uint16_t cycleSeconds;
  static uint32_t viewMask;

private:
  int8_t  _sda          = OLED_SDA_PIN;
  int8_t  _scl          = OLED_SCL_PIN;
  bool    _flip         = false;
  uint8_t _contrast     = 200;
  bool    _enabled      = true;
  bool    _autoCycleCfg = false;
  uint16_t _cycleSecCfg = 10;
  uint32_t _lastCycleMs = 0;
  bool    _initDone     = false;

  void initDisplay() {
    if (display) {
      delete display;
      display = nullptr;
      ready = false;
    }

    Wire.begin(_sda, _scl);
    display = new U8X8_SSD1306_128X64_NONAME_HW_I2C(U8X8_PIN_NONE);
    if (display->begin()) {
      display->setFlipMode(_flip ? 1 : 0);
      display->setContrast(_contrast);
      display->clearDisplay();
      ready = true;
      display->setFont(u8x8_font_7x14_1x2_r);
      display->drawString(0, 3, "WLED ready");
    } else {
      delete display;
      display = nullptr;
      ready = false;
    }
  }

public:
  void setup() override {
    if (_enabled) initDisplay();
    autoCycle = _autoCycleCfg;
    cycleSeconds = _cycleSecCfg;
    _lastCycleMs = millis();
    _initDone = true;
  }

  void loop() override {
    if (!_enabled || !ready || !autoCycle) return;
    uint32_t now = millis();
    if ((uint32_t)(now - _lastCycleMs) >= (uint32_t)cycleSeconds * 1000UL) {
      _lastCycleMs = now;
      nextView();
    }
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledBase");
    top["enabled"]      = _enabled;
    top["autoCycle"]    = _autoCycleCfg;
    top["cycleSeconds"] = _cycleSecCfg;
    top["sda"]          = _sda;
    top["scl"]          = _scl;
    top["flip"]         = _flip;
    top["contrast"]     = _contrast;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["OledBase"];
    if (top.isNull()) return false;

    bool displayChanged = false;

    if (top.containsKey("enabled") && top["enabled"].as<bool>() != _enabled) {
      _enabled = top["enabled"];
      displayChanged = true;
    }
    if (top.containsKey("autoCycle")) _autoCycleCfg = top["autoCycle"];
    if (top.containsKey("cycleSeconds")) _cycleSecCfg = top["cycleSeconds"];
    if (top.containsKey("sda") && top["sda"].as<int8_t>() != _sda) {
      _sda = top["sda"];
      displayChanged = true;
    }
    if (top.containsKey("scl") && top["scl"].as<int8_t>() != _scl) {
      _scl = top["scl"];
      displayChanged = true;
    }
    if (top.containsKey("flip") && top["flip"].as<bool>() != _flip) {
      _flip = top["flip"];
      displayChanged = true;
    }
    if (top.containsKey("contrast") && top["contrast"].as<uint8_t>() != _contrast) {
      _contrast = top["contrast"];
      displayChanged = true;
    }

    autoCycle = _autoCycleCfg;
    cycleSeconds = _cycleSecCfg < 1 ? 1 : _cycleSecCfg;

    if (_initDone && displayChanged && _enabled) initDisplay();
    if (_initDone && displayChanged && !_enabled && display) {
      display->clearDisplay();
      delete display;
      display = nullptr;
      ready = false;
    }

    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");

    JsonArray oled = user.createNestedArray("OLED");
    oled.add(ready ? "connecté" : "non détecté");

    JsonArray view = user.createNestedArray("OLED vue");
    view.add(activeView);
    view.add(autoCycle ? "auto" : "fixe");
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

U8X8_SSD1306_128X64_NONAME_HW_I2C* OledBaseUsermod::display = nullptr;
bool OledBaseUsermod::ready = false;
uint8_t OledBaseUsermodImpl::activeView = 1;
bool OledBaseUsermodImpl::autoCycle = false;
uint16_t OledBaseUsermodImpl::cycleSeconds = 10;
uint32_t OledBaseUsermodImpl::viewMask = 0;

U8X8_SSD1306_128X64_NONAME_HW_I2C* OledBaseUsermod::getDisplay() {
  return ready ? display : nullptr;
}

bool OledBaseUsermod::isReady() {
  return ready;
}

uint8_t OledBaseUsermod::getActiveView() {
  return OledBaseUsermodImpl::activeView;
}

void OledBaseUsermod::setViewActive(uint8_t viewId, bool enabled) {
  if (viewId < 1 || viewId > 31) return;
  uint32_t bit = (1UL << (viewId - 1));
  if (enabled) OledBaseUsermodImpl::viewMask |= bit;
  else OledBaseUsermodImpl::viewMask &= ~bit;

  if (OledBaseUsermodImpl::viewMask == 0) {
    OledBaseUsermodImpl::activeView = 0;
    if (display && ready) display->clearDisplay();
    return;
  }

  if (!isViewActive(OledBaseUsermodImpl::activeView)) {
    for (uint8_t i = 1; i <= 31; i++) {
      if (isViewActive(i)) {
        OledBaseUsermodImpl::activeView = i;
        break;
      }
    }
  }
}

bool OledBaseUsermod::isViewActive(uint8_t viewId) {
  if (viewId < 1 || viewId > 31) return false;
  return (OledBaseUsermodImpl::viewMask & (1UL << (viewId - 1))) != 0;
}

bool OledBaseUsermod::isCurrentView(uint8_t viewId) {
  return viewId == OledBaseUsermodImpl::activeView;
}

void OledBaseUsermod::nextView() {
  if (OledBaseUsermodImpl::viewMask == 0) return;
  uint8_t start = OledBaseUsermodImpl::activeView;
  for (uint8_t step = 1; step <= 31; step++) {
    uint8_t candidate = ((start + step - 1) % 31) + 1;
    if (isViewActive(candidate)) {
      OledBaseUsermodImpl::activeView = candidate;
      if (display && ready) display->clearDisplay();
      return;
    }
  }
}

static OledBaseUsermodImpl oled_base_mod;
REGISTER_USERMOD(oled_base_mod);

#include "wled.h"
#include <U8x8lib.h>
#include <Wire.h>

// ─── Constantes par défaut (overridables via build_flags) ───
#ifndef OLED_SDA_PIN
  #define OLED_SDA_PIN 21
#endif
#ifndef OLED_SCL_PIN
  #define OLED_SCL_PIN 22
#endif

class OledBaseUsermod : public Usermod {
public:
  // ── Pointeur statique global ──────────────────────
  // Les autres usermods récupèrent l'écran via OledBaseUsermod::getDisplay()
  static U8X8_SSD1306_128X64_NONAME_HW_I2C* display;
  static bool ready;

private:
  // ── Paramètres configurables depuis le frontend ──
  int8_t  _sda       = OLED_SDA_PIN;
  int8_t  _scl       = OLED_SCL_PIN;
  bool    _flip      = false;   // rotation 180°
  uint8_t _contrast  = 200;
  bool    _enabled   = true;

  // ── Config changée ? ─────────────────────────────
  bool    _initDone  = false;

  void initDisplay() {
    if (display) {
      delete display;
      display = nullptr;
      ready   = false;
    }
    Wire.begin(_sda, _scl);
    display = new U8X8_SSD1306_128X64_NONAME_HW_I2C(U8X8_PIN_NONE);
    if (display->begin()) {
      display->setFlipMode(_flip ? 1 : 0);
      display->setContrast(_contrast);
      display->clearDisplay();
      display->setFont(u8x8_font_7x14_1x2_r);
      display->drawString(0, 3, "WLED ready");
      ready = true;
    } else {
      delete display;
      display = nullptr;
    }
  }

public:
  // ── Accesseur pour les autres usermods ───────────
  static U8X8_SSD1306_128X64_NONAME_HW_I2C* getDisplay() {
    return ready ? display : nullptr;
  }

  // ────────────────────────────────────────────────
  void setup() override {
    if (_enabled) initDisplay();
    _initDone = true;
  }

  void loop() override {
    // rien — l'écran est géré par les usermods enfants
  }

  // ── Sauvegarde config → flash ────────────────────
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledBase");
    top["enabled"]  = _enabled;
    top["sda"]      = _sda;
    top["scl"]      = _scl;
    top["flip"]     = _flip;
    top["contrast"] = _contrast;
  }

  // ── Lecture config depuis flash ──────────────────
  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["OledBase"];
    if (top.isNull()) return false;

    bool changed = false;

    if (top.containsKey("enabled")  && top["enabled"].as<bool>()   != _enabled)  { _enabled  = top["enabled"];  changed = true; }
    if (top.containsKey("sda")      && top["sda"].as<int8_t>()     != _sda)      { _sda      = top["sda"];      changed = true; }
    if (top.containsKey("scl")      && top["scl"].as<int8_t>()     != _scl)      { _scl      = top["scl"];      changed = true; }
    if (top.containsKey("flip")     && top["flip"].as<bool>()      != _flip)     { _flip     = top["flip"];     changed = true; }
    if (top.containsKey("contrast") && top["contrast"].as<uint8_t>()!= _contrast){ _contrast = top["contrast"]; changed = true; }

    // Si la config change après le boot → réinitialiser l'écran
    if (_initDone && changed && _enabled) initDisplay();
    if (_initDone && changed && !_enabled && display) {
      display->clearDisplay();
      delete display;
      display = nullptr;
      ready   = false;
    }

    return true;
  }

  // ── Info dans l'onglet "Info" de WLED ────────────
  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray  info = user.createNestedArray("OLED");
    info.add(ready ? "connecté" : "non détecté");
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

// Définition des membres statiques
U8X8_SSD1306_128X64_NONAME_HW_I2C* OledBaseUsermod::display = nullptr;
bool OledBaseUsermod::ready = false;

static OledBaseUsermod oled_base_mod;
REGISTER_USERMOD(oled_base_mod);

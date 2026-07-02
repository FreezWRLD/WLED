#include "wled.h"
#include "wled_oled_api.h"   // extern g_showLedStatusUntil

// ============================================================
// Pins des boutons physiques — D1 Mini
//   D6 = GPIO12 → Bouton POWER (on/off LED)
//   D5 = GPIO14 → Bouton EFFECT (effet suivant)
// ============================================================
#ifndef BTN_GPIO_POWER
  #define BTN_GPIO_POWER  12   // D6
#endif
#ifndef BTN_GPIO_EFFECT
  #define BTN_GPIO_EFFECT 14   // D5
#endif

#define BTN_DEBOUNCE_MS  200UL

// ============================================================
// Usermod boutons
// ============================================================
class WledButtonsUsermod : public Usermod {
public:

  void setup() override {
    pinMode(_gpioP, INPUT_PULLUP);
    pinMode(_gpioE, INPUT_PULLUP);
    _lastStateP = digitalRead(_gpioP);
    _lastStateE = digitalRead(_gpioE);
  }

  void loop() override {
    uint32_t now = millis();

    // --- Bouton POWER ---
    bool stateP = digitalRead(_gpioP);
    if (stateP == LOW && _lastStateP == HIGH && now - _debounceP > _debounceMs) {
      _debounceP = now;
      onPowerPress();
    }
    _lastStateP = stateP;

    // --- Bouton EFFECT ---
    bool stateE = digitalRead(_gpioE);
    if (stateE == LOW && _lastStateE == HIGH && now - _debounceE > _debounceMs) {
      _debounceE = now;
      onEffectPress();
    }
    _lastStateE = stateE;
  }

  // ---- Config ----
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("WledButtons");
    top["gpioP"]       = _gpioP;
    top["gpioE"]       = _gpioE;
    top["debounceMs"]  = _debounceMs;
    top["ledStatusMs"] = _ledStatusMs;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["WledButtons"];
    if (top.isNull()) return false;
    if (top.containsKey("gpioP")) {
      pinMode(_gpioP, INPUT);    // libère l'ancien pin
      _gpioP = top["gpioP"];
      pinMode(_gpioP, INPUT_PULLUP);
    }
    if (top.containsKey("gpioE")) {
      pinMode(_gpioE, INPUT);
      _gpioE = top["gpioE"];
      pinMode(_gpioE, INPUT_PULLUP);
    }
    _debounceMs  = top["debounceMs"]  | BTN_DEBOUNCE_MS;
    _ledStatusMs = top["ledStatusMs"] | 5000UL;
    return true;
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }

private:
  int8_t   _gpioP       = BTN_GPIO_POWER;
  int8_t   _gpioE       = BTN_GPIO_EFFECT;
  uint32_t _debounceMs  = BTN_DEBOUNCE_MS;
  uint32_t _ledStatusMs = 5000UL;

  bool     _lastStateP  = HIGH;
  bool     _lastStateE  = HIGH;
  uint32_t _debounceP   = 0;
  uint32_t _debounceE   = 0;

  // Affiche l'état LED sur l'OLED pendant _ledStatusMs
  void triggerLedStatusPage() {
    g_showLedStatusUntil = millis() + _ledStatusMs;
  }

  // Bouton POWER — toggle on/off le ruban LED
  void onPowerPress() {
    toggleOnOff();
    stateUpdated(CALL_MODE_BUTTON);
    triggerLedStatusPage();
  }

  // Bouton EFFECT — passe à l'effet suivant WLED
  // Même logique que button.cpp ligne 23
  void onEffectPress() {
    ++effectCurrent %= strip.getModeCount();
    stateChanged = true;
    colorUpdated(CALL_MODE_BUTTON);
    triggerLedStatusPage();
  }
};

static WledButtonsUsermod wled_buttons_mod;
REGISTER_USERMOD(wled_buttons_mod);

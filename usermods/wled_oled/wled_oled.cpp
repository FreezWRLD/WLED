#include "wled.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "wled_oled_api.h"
#include "wled_weather_data.h"   // struct WeatherData + extern g_weatherData

// ============================================================
// Pins I2C — D1 Mini : D1=GPIO5 (SCL), D2=GPIO4 (SDA)
// ============================================================
#ifndef OLED_SDA
  #define OLED_SDA 4
#endif
#ifndef OLED_SCL
  #define OLED_SCL 5
#endif

// ============================================================
// Durée d'affichage automatique de chaque page (ms)
// ============================================================
#define PAGE_CYCLE_MS     5000UL
#define LED_STATUS_MS     5000UL
#define SENSOR_READ_MS   30000UL

// ============================================================
// Variable partagée (définie ici, extern dans wled_oled_api.h)
// ============================================================
uint32_t g_showLedStatusUntil = 0;

// ============================================================
// Jours de la semaine en français
// ============================================================
static const char* const JOURS[] = {
  "Dim", "Lun", "Mar", "Mer",
  "Jeu", "Ven", "Sam"
};

// ============================================================
// Usermod principal OLED
// ============================================================
class WledOledUsermod : public Usermod {
public:

  // ---- Lifecycle WLED ----
  void setup() override {
    Wire.begin(OLED_SDA, OLED_SCL);

    // Initialisation U8g2
    _u8g2 = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(
      U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

    if (_u8g2->begin()) {
      _oledReady = true;
      _u8g2->setContrast(_contrast);
      _u8g2->clearBuffer();
      _u8g2->setFont(u8g2_font_7x13_mr);
      _u8g2->drawStr(14, 36, "WLED  ready");
      _u8g2->sendBuffer();
    }

    // Initialisation AHT20
    _ahtReady = _aht.begin();

    // Initialisation BMP280 (adresse 0x76 par défaut)
    _bmpReady = _bmp.begin(0x76);
    if (!_bmpReady) _bmpReady = _bmp.begin(0x77); // essai adresse alternative

    _lastPageSwitch = millis();
    _lastSensorRead = millis() - SENSOR_READ_MS; // lire immédiatement

    // Initialize state tracking variables for sleep timer on startup
    _lastBri = bri;
    _lastEffect = effectCurrent;
    _lastShowLedStatusUntil = g_showLedStatusUntil;
    _lastActivityTime = millis();
    _screenAsleep = false;

    _initDone = true;
  }

  void loop() override {
    if (!_enabled || !_oledReady) return;
    uint32_t now = millis();

    // Lecture capteurs (toutes les 30s)
    if (now - _lastSensorRead >= SENSOR_READ_MS) {
      _lastSensorRead = now;
      readSensors();
    }

    // Detect activity (physical button presses or Web UI state changes)
    bool activity = false;
    if (bri != _lastBri) {
      _lastBri = bri;
      activity = true;
    }
    if (effectCurrent != _lastEffect) {
      _lastEffect = effectCurrent;
      activity = true;
    }
    if (g_showLedStatusUntil != _lastShowLedStatusUntil) {
      _lastShowLedStatusUntil = g_showLedStatusUntil;
      activity = true;
    }

    // Wake up screen on activity
    if (activity) {
      _lastActivityTime = now;
      if (_screenAsleep) {
        _screenAsleep = false;
        if (_u8g2) {
          _u8g2->setPowerSave(0);
        }
      }
    }

    // Check if timeout has been reached to turn off the screen
    if (_sleepMin > 0 && !_screenAsleep && (now - _lastActivityTime >= _sleepMin * 60000UL)) {
      _screenAsleep = true;
      if (_u8g2) {
        _u8g2->setPowerSave(1);
      }
    }

    // Do not draw anything if the screen is asleep
    if (_screenAsleep) return;

    // Cycle de pages
    if (now - _lastPageSwitch >= _cycleMs) {
      _lastPageSwitch = now;
      _currentPage = (_currentPage + 1) % 3; // pages 0..2
    }

    // Rendu (toutes les ~50ms = 20 fps pour les animations)
    if (now - _lastDraw >= 50) {
      _lastDraw = now;
      drawCurrentPage(now);
    }
  }

  // ---- Config ----
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("WledOled");
    top["enabled"]   = _enabled;
    top["sda"]       = _sda;
    top["scl"]       = _scl;
    top["contrast"]  = _contrast;
    top["flip"]      = _flip;
    top["cycleS"]    = _cycleMs / 1000;
    top["sleepMin"]  = _sleepMin;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["WledOled"];
    if (top.isNull()) return false;

    bool changed = false;
    if (top.containsKey("enabled"))  _enabled  = top["enabled"];
    if (top.containsKey("contrast") && (uint8_t)top["contrast"] != _contrast) {
      _contrast = top["contrast"]; changed = true;
    }
    if (top.containsKey("flip") && (bool)top["flip"] != _flip) {
      _flip = top["flip"]; changed = true;
    }
    if (top.containsKey("sda")) _sda = top["sda"];
    if (top.containsKey("scl")) _scl = top["scl"];
    uint32_t cs = top["cycleS"] | 5;
    _cycleMs = constrain(cs, 1, 300) * 1000UL;

    if (top.containsKey("sleepMin")) {
      uint8_t sm = top["sleepMin"];
      if (sm != _sleepMin) {
        _sleepMin = sm;
        _lastActivityTime = millis();
        // If we disabled the sleep timer, ensure screen wakes up immediately
        if (_sleepMin == 0 && _screenAsleep) {
          _screenAsleep = false;
          if (_u8g2) {
            _u8g2->setPowerSave(0);
          }
        }
      }
    }

    if (_initDone && changed && _u8g2) {
      _u8g2->setContrast(_contrast);
      if (_flip) _u8g2->setDisplayRotation(U8G2_R2);
      else       _u8g2->setDisplayRotation(U8G2_R0);
    }
    return true;
  }

  // Add parameter description in settings page for the sleep timeout setting
  void appendConfigData() override {
    oappend(F("addInfo('WledOled:sleepMin',1,'Eteindre l\\'ecran (minutes, 0 = infini)');"));
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray arr = user.createNestedArray(F("OLED"));
    arr.add(_oledReady ? F("OK") : F("absent"));
    if (_ahtReady) {
      JsonArray aArr = user.createNestedArray(F("AHT20 int."));
      char tStr[8], hStr[8], buf[24];
      formatFloat(tStr, sizeof(tStr), _tempAht, 1);
      formatFloat(hStr, sizeof(hStr), _humAht, 0);
      snprintf(buf, sizeof(buf), "%s°C %s%%", tStr, hStr);
      aArr.add(buf);
    }
    if (_bmpReady) {
      JsonArray bArr = user.createNestedArray(F("BMP280 int."));
      char tStr[8], pStr[8], buf[24];
      formatFloat(tStr, sizeof(tStr), _tempBmp, 1);
      formatFloat(pStr, sizeof(pStr), _presBmp, 0);
      snprintf(buf, sizeof(buf), "%s°C %shPa", tStr, pStr);
      bArr.add(buf);
    }
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }

private:
  // ---- Helper de formatage de float sans %f ----
  void formatFloat(char* buf, size_t bufSize, float val, int decimals) {
    if (isnan(val)) {
      snprintf(buf, bufSize, "---");
      return;
    }
    
    float rounding = 0.5;
    for (int i = 0; i < decimals; i++) rounding /= 10.0;
    
    float roundedVal = val + (val >= 0 ? rounding : -rounding);
    int ipart = (int)roundedVal;
    
    bool isNegative = (roundedVal < 0);
    
    if (decimals == 0) {
      if (isNegative && ipart == 0) {
        snprintf(buf, bufSize, "-0");
      } else {
        snprintf(buf, bufSize, "%d", ipart);
      }
    } else {
      float frac = roundedVal - ipart;
      if (frac < 0) frac = -frac;
      
      int multiplier = 1;
      for (int i = 0; i < decimals; i++) multiplier *= 10;
      int fpart = (int)(frac * multiplier);
      
      if (isNegative && ipart == 0) {
        snprintf(buf, bufSize, "-0.%0*d", decimals, fpart);
      } else {
        snprintf(buf, bufSize, "%d.%0*d", ipart, decimals, fpart);
      }
    }
  }

  // ---- Objets matériels ----
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C* _u8g2 = nullptr;
  Adafruit_AHTX0 _aht;
  Adafruit_BMP280 _bmp;

  // ---- État ----
  bool     _oledReady   = false;
  bool     _ahtReady    = false;
  bool     _bmpReady    = false;
  bool     _initDone    = false;
  bool     _enabled     = true;
  bool     _flip        = false;
  uint8_t  _contrast    = 200;
  int8_t   _sda         = OLED_SDA;
  int8_t   _scl         = OLED_SCL;
  uint8_t  _currentPage = 0;
  uint32_t _cycleMs     = PAGE_CYCLE_MS;
  uint32_t _lastPageSwitch = 0;
  uint32_t _lastSensorRead = 0;
  uint32_t _lastDraw    = 0;

  // Configurable screen timeout settings and tracking variables
  uint8_t  _sleepMin    = 0;                     // Time in minutes before screen enters power save (0 = disabled)
  bool     _screenAsleep = false;                // Tracks if screen is currently in power save mode
  uint8_t  _lastBri     = 0;                     // Cached brightness value to detect user brightness adjustment activity
  uint8_t  _lastEffect  = 0;                     // Cached effect value to detect user changing effect activity
  uint32_t _lastShowLedStatusUntil = 0;          // Cached timestamp of button actions to detect physical button activity
  uint32_t _lastActivityTime = 0;                // Milliseconds timestamp of the last detected user or state activity

  // ---- Données capteurs ----
  float _tempAht = NAN, _humAht = NAN;
  float _tempBmp = NAN, _presBmp = NAN;

  // ---- Animation frame counter ----
  int _frame = 0;



  // =====================================================
  // Lecture capteurs
  // =====================================================
  void readSensors() {
    if (!_ahtReady) {
      _ahtReady = _aht.begin();
    }
    if (_ahtReady) {
      sensors_event_t hEvt, tEvt;
      if (_aht.getEvent(&hEvt, &tEvt)) {
        _tempAht = tEvt.temperature;
        _humAht  = hEvt.relative_humidity;
      } else {
        // En cas d'échec de lecture, on marque comme non-prêt pour forcer une réinitialisation au prochain cycle
        _ahtReady = false;
        _tempAht = NAN;
        _humAht = NAN;
      }
    }

    if (!_bmpReady) {
      _bmpReady = _bmp.begin(0x76);
      if (!_bmpReady) _bmpReady = _bmp.begin(0x77);
    }
    if (_bmpReady) {
      _tempBmp = _bmp.readTemperature();
      _presBmp = _bmp.readPressure() / 100.0f; // Pa → hPa
      // Si la lecture renvoie des valeurs aberrantes ou échoue (ex: débranché à chaud)
      if (isnan(_tempBmp) || _tempBmp < -50.0f || _tempBmp > 150.0f) {
        _bmpReady = false;
        _tempBmp = NAN;
        _presBmp = NAN;
      }
    }
  }

  // =====================================================
  // Dispatching des pages
  // =====================================================
  void drawCurrentPage(uint32_t now) {
    if (!_u8g2) return;
    _u8g2->clearBuffer();

    // Vue LED prioritaire (5s après appui bouton)
    if (now < g_showLedStatusUntil) {
      drawPageLedStatus();
    } else {
      switch (_currentPage) {
        case 0: drawPageClock();   break;
        case 1: drawPageWeather(); break;
        case 2: drawPageSensors(); break;
      }
    }

    _u8g2->sendBuffer();
    _frame++;
  }

  // =====================================================
  // Helpers graphiques
  // =====================================================

  // Ligne horizontale de séparation
  void drawSeparator(int y) {
    _u8g2->drawHLine(0, y, 128);
  }

  // Soleil statique
  void drawSun(int cx, int cy) {
    _u8g2->drawDisc(cx, cy, 6);
    for (int i = 0; i < 8; i++) {
      float angle = (float)(i * 45) * 0.01745329f;
      int x1 = cx + (int)(cosf(angle) * 8.0f);
      int y1 = cy + (int)(sinf(angle) * 8.0f);
      int x2 = cx + (int)(cosf(angle) * 12.0f);
      int y2 = cy + (int)(sinf(angle) * 12.0f);
      _u8g2->drawLine(x1, y1, x2, y2);
    }
  }

  // Croissant de lune statique
  void drawMoon(int cx, int cy) {
    _u8g2->setDrawColor(1);
    _u8g2->drawDisc(cx, cy, 8);
    _u8g2->setDrawColor(0); // efface (noir)
    _u8g2->drawDisc(cx + 4, cy - 2, 8);
    _u8g2->setDrawColor(1);
  }

  // Contour de nuage (ressemble à l'image du user)
  void drawCloud(int cx, int cy) {
    _u8g2->setDrawColor(1);
    _u8g2->drawDisc(cx - 10, cy + 2, 7);
    _u8g2->drawDisc(cx,      cy - 4, 9);
    _u8g2->drawDisc(cx + 10, cy + 2, 7);
    _u8g2->drawBox (cx - 10, cy - 2, 20, 11);

    _u8g2->setDrawColor(0);
    _u8g2->drawDisc(cx - 10, cy + 2, 6);
    _u8g2->drawDisc(cx,      cy - 4, 8);
    _u8g2->drawDisc(cx + 10, cy + 2, 6);
    _u8g2->drawBox (cx - 9,  cy - 2, 18, 10);

    _u8g2->setDrawColor(1);
  }

  // Pluie statique sous le nuage
  void drawRain(int cx, int cy) {
    drawCloud(cx, cy);
    _u8g2->drawLine(cx - 8, cy + 10, cx - 10, cy + 14);
    _u8g2->drawLine(cx,      cy + 10, cx - 2,  cy + 14);
    _u8g2->drawLine(cx + 8,  cy + 10, cx + 6,  cy + 14);
  }

  // Neige statique sous le nuage
  void drawSnow(int cx, int cy) {
    drawCloud(cx, cy);
    _u8g2->drawPixel(cx - 6, cy + 10);
    _u8g2->drawPixel(cx,      cy + 13);
    _u8g2->drawPixel(cx + 6,  cy + 10);
  }

  // Éclair statique
  void drawLightning(int x, int y) {
    _u8g2->drawLine(x,     y,     x + 2, y + 6);
    _u8g2->drawLine(x + 2, y + 6, x - 1, y + 12);
    _u8g2->drawLine(x - 1, y + 12, x + 3, y + 18);
  }

  // Brouillard statique
  void drawFog(int cx, int cy) {
    _u8g2->drawHLine(cx - 15, cy - 8, 30);
    _u8g2->drawHLine(cx - 10, cy - 2, 20);
    _u8g2->drawHLine(cx - 18, cy + 4, 36);
    _u8g2->drawHLine(cx - 12, cy + 10, 24);
  }

  // Détection heure de jour (entre sunrise et sunset OWM)
  bool isDayTime() {
    if (year(localTime) < 2010) return true; // Défaut jour si non synchronisé
    int curMin = (int)hour(localTime) * 60 + (int)minute(localTime);
    int srMin  = g_weatherData.sunriseH * 60 + g_weatherData.sunriseM;
    int ssMin  = g_weatherData.sunsetH  * 60 + g_weatherData.sunsetM;
    return curMin >= srMin && curMin < ssMin;
  }

  // =====================================================
  // PAGE 0 — HORLOGE + DATE
  // =====================================================
  void drawPageClock() {
    if (year(localTime) < 2010) {
      _u8g2->setFont(u8g2_font_7x13_mr);
      _u8g2->drawStr(12, 32, "Synchro NTP...");
      _u8g2->setFont(u8g2_font_5x7_mr);
      _u8g2->drawStr(15, 48, "Verifier WiFi WLED");
      return;
    }

    // Heure HH:MM en grand, centré en haut (Y baseline = 28)
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
             (int)hour(localTime), (int)minute(localTime));
    _u8g2->setFont(u8g2_font_logisoso22_tf);
    int tw = (int)_u8g2->getStrWidth(timeBuf);
    _u8g2->drawStr((128 - tw) / 2, 28, timeBuf);

    // Secondes (moyennes, une taille de plus -> u8g2_font_7x13_mr)
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), "%02d", (int)second(localTime));
    _u8g2->setFont(u8g2_font_7x13_mr);
    _u8g2->drawStr(106, 26, secBuf);

    // Un seul séparateur
    drawSeparator(34);

    // Jour de la semaine abrégé + date (plus grand -> u8g2_font_7x13_mr)
    char dateBuf[24];
    uint8_t wd = weekday(localTime) - 1; // 0=Dim..6=Sam
    snprintf(dateBuf, sizeof(dateBuf), "%s %02d/%02d/%04d",
             JOURS[wd],
             (int)day(localTime), (int)month(localTime), (int)year(localTime));
    _u8g2->setFont(u8g2_font_7x13_mr);
    int dw = (int)_u8g2->getStrWidth(dateBuf);
    _u8g2->drawStr((128 - dw) / 2, 54, dateBuf);
  }

  // =====================================================
  // PAGE 1 — MÉTÉO ANIMÉE
  // =====================================================
  void drawPageWeather() {
    // Construct dynamic title based on the loaded weather city name in uppercase
    char titleBuf[64];
    if (g_weatherData.valid && g_weatherData.city[0] != '\0') {
      char cityUpper[32];
      strlcpy(cityUpper, g_weatherData.city, sizeof(cityUpper));
      for (int i = 0; cityUpper[i]; i++) {
        if (cityUpper[i] >= 'a' && cityUpper[i] <= 'z') {
          cityUpper[i] -= 32;
        }
      }
      snprintf(titleBuf, sizeof(titleBuf), "METEO %s", cityUpper);
    } else {
      strlcpy(titleBuf, "METEO", sizeof(titleBuf));
    }
    _u8g2->setFont(u8g2_font_5x7_mr);
    _u8g2->drawStr(0, 7, titleBuf);
    drawSeparator(9);

    if (!g_weatherData.valid) {
      _u8g2->setFont(u8g2_font_7x13_mr);
      _u8g2->drawStr(4, 30, "Chargement...");
      return;
    }

    // Icône à gauche (cx = 32, cy = 26)
    const char* main = g_weatherData.main;

    if (strcmp(main, "Clear") == 0) {
      if (isDayTime()) {
        drawSun(32, 26);
      } else {
        drawMoon(32, 26);
      }
    }
    else if (strcmp(main, "Clouds") == 0) {
      drawCloud(32, 26);
    }
    else if (strcmp(main, "Rain") == 0 || strcmp(main, "Drizzle") == 0) {
      drawRain(32, 26);
    }
    else if (strcmp(main, "Thunderstorm") == 0) {
      drawCloud(32, 24);
      drawLightning(32, 28);
    }
    else if (strcmp(main, "Snow") == 0) {
      drawSnow(32, 26);
    }
    else if (strcmp(main, "Mist") == 0 ||
             strcmp(main, "Fog")  == 0 ||
             strcmp(main, "Haze") == 0) {
      drawFog(32, 26);
    }
    else {
      if (isDayTime()) drawSun(32, 26); else drawMoon(32, 26);
    }

    // Température extérieure à droite (aussi grosse que l'heure)
    if (!isnan(g_weatherData.temp)) {
      char tStr[8], tbuf[16];
      formatFloat(tStr, sizeof(tStr), g_weatherData.temp, 1);
      snprintf(tbuf, sizeof(tbuf), "%s", tStr);
      _u8g2->setFont(u8g2_font_logisoso22_tf);
      
      int tw = (int)_u8g2->getStrWidth(tbuf);
      int startX = 94 - tw / 2;
      _u8g2->drawStr(startX, 36, tbuf);
      
      // Petit °C à côté
      _u8g2->setFont(u8g2_font_5x7_mr);
      _u8g2->drawStr(startX + tw + 2, 20, "o");
      _u8g2->setFont(u8g2_font_7x13_mr);
      _u8g2->drawStr(startX + tw + 8, 28, "C");
    }

    // Description en bas
    drawSeparator(44);
    _u8g2->setFont(u8g2_font_5x7_mr);
    int dw = (int)_u8g2->getStrWidth(g_weatherData.desc);
    _u8g2->drawStr((128 - dw) / 2, 55, g_weatherData.desc);
  }

  // =====================================================
  // PAGE 2 — TEMPERATURES & CAPTEURS INTERIEURS (AHT20 & BMP280)
  // =====================================================
  void drawPageSensors() {
    // Ligne 1 : "Température" (Centré)
    _u8g2->setFont(u8g2_font_7x13_mr);
    const char* label = "Temperature";
    int lw = (int)_u8g2->getStrWidth(label);
    _u8g2->drawStr((128 - lw) / 2, 10, label);

    // Ligne 2 : Valeur Température (Grande, Centrée, y=36)
    float temp = NAN;
    if (_ahtReady) temp = _tempAht;
    else if (_bmpReady) temp = _tempBmp;

    char tStr[8], tbuf[16];
    formatFloat(tStr, sizeof(tStr), temp, 1);
    
    if (isnan(temp)) {
      snprintf(tbuf, sizeof(tbuf), "---");
    } else {
      snprintf(tbuf, sizeof(tbuf), "%s", tStr);
    }
    
    _u8g2->setFont(u8g2_font_logisoso22_tf);
    int tw = (int)_u8g2->getStrWidth(tbuf);
    int startX = (128 - tw) / 2;
    _u8g2->drawStr(startX, 36, tbuf);

    if (!isnan(temp)) {
      // Degré Celsius à côté de la température
      _u8g2->setFont(u8g2_font_5x7_mr);
      _u8g2->drawStr(startX + tw + 2, 20, "o");
      _u8g2->setFont(u8g2_font_7x13_mr);
      _u8g2->drawStr(startX + tw + 8, 28, "C");
    }

    // Séparateur à y=40
    drawSeparator(40);

    // Ligne 3 : "XX% Hum | XX hPa"
    char humStr[16], presStr[16], bottomBuf[40];
    
    if (_ahtReady && !isnan(_humAht)) {
      char hVal[8];
      formatFloat(hVal, sizeof(hVal), _humAht, 0);
      snprintf(humStr, sizeof(humStr), "%s%% Hum", hVal);
    } else {
      strlcpy(humStr, "--% Hum", sizeof(humStr));
    }

    if (_bmpReady && !isnan(_presBmp)) {
      char pVal[8];
      formatFloat(pVal, sizeof(pVal), _presBmp, 0);
      snprintf(presStr, sizeof(presStr), "%s hPa", pVal);
    } else {
      strlcpy(presStr, "--- hPa", sizeof(presStr));
    }

    snprintf(bottomBuf, sizeof(bottomBuf), "%s | %s", humStr, presStr);
    
    _u8g2->setFont(u8g2_font_7x13_mr);
    int bw = (int)_u8g2->getStrWidth(bottomBuf);
    _u8g2->drawStr((128 - bw) / 2, 57, bottomBuf);
  }

  // =====================================================
  // PAGE LED STATUS — affichée 5s après appui bouton
  // =====================================================
  void drawPageLedStatus() {
    _u8g2->setFont(u8g2_font_5x7_mr);
    _u8g2->drawStr(0, 7, "RUBAN LED");
    drawSeparator(9);

    // ON / OFF en grand
    _u8g2->setFont(u8g2_font_logisoso22_tf);
    const char* state = bri ? "ON" : "OFF";
    int sw = (int)_u8g2->getStrWidth(state);
    _u8g2->drawStr((128 - sw) / 2, 36, state);

    drawSeparator(40);

    // Nom de l'effet courant
    if (bri) {
      _u8g2->setFont(u8g2_font_7x13_mr);
      const char* fxName = strip.getModeData(effectCurrent);
      char fxBuf[24];
      int i = 0;
      while (fxName && fxName[i] && fxName[i] != '\n' && fxName[i] != '@' && i < 23) {
        fxBuf[i] = fxName[i];
        i++;
      }
      fxBuf[i] = '\0';
      int fw = (int)_u8g2->getStrWidth(fxBuf);
      _u8g2->drawStr((128 - fw) / 2, 57, fxBuf);
    }
  }
};

static WledOledUsermod wled_oled_mod;
REGISTER_USERMOD(wled_oled_mod);

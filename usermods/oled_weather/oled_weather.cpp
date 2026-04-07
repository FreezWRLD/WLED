#include "wled.h"
#include "../oled_base/oled_base.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

class OledWeatherUsermod : public Usermod {
private:
  bool    _enabled       = false;   

  // ── Paramètres configurables dans le frontend ────
  char    _apiKey[64]    = "";       
  char    _city[32]      = "Epernay";
  char    _country[4]    = "FR";
  uint8_t _updateHours   = 1;        
  bool    _showTemp      = true;
  bool    _showDesc      = true;
  bool    _showFeelsLike = false;
  bool    _useCelsius    = true;

  // ── État interne ─────────────────────────────────
  float   _temp          = NAN;
  float   _feelsLike     = NAN;
  float   _tempMin       = NAN;
  float   _tempMax       = NAN;
  char    _desc[32]      = "";
  char    _icon[4]       = "";
  bool    _fetchOk       = false;
  unsigned long _lastFetch = 0;

  // ── Fetch HTTP ───────────────────────────────────
  void fetchWeather() {
    if (strlen(_apiKey) < 10) return;     
    if (WiFi.status() != WL_CONNECTED)   return;

    char url[200];
    const char* units = _useCelsius ? "metric" : "imperial";
    snprintf(url, sizeof(url),
      "http://api.openweathermap.org/data/2.5/weather"
      "?q=%s,%s&appid=%s&units=%s&lang=fr",
      _city, _country, _apiKey, units);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();

    if (code == 200) {
      String payload = http.getString();
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        _temp      = doc["main"]["temp"]       | NAN;
        _feelsLike = doc["main"]["feels_like"] | NAN;
        _tempMin   = doc["main"]["temp_min"]   | NAN;
        _tempMax   = doc["main"]["temp_max"]   | NAN;
        const char* d = doc["weather"][0]["description"] | "";
        strlcpy(_desc, d, sizeof(_desc));
        const char* ic = doc["weather"][0]["icon"] | "";
        strlcpy(_icon, ic, sizeof(_icon));
        _fetchOk = true;
      }
    } else {
      _fetchOk = false;
    }
    http.end();
  }

  // ── Affichage ────────────────────────────────────
  void draw() {
    auto* d = OledBaseUsermod::getDisplay();
    if (!d) return;

    d->clearDisplay();
    d->setFont(u8x8_font_7x14_1x2_r);

    if (!_fetchOk || isnan(_temp)) {
      d->drawString(0, 3, strlen(_apiKey) < 10
        ? "Clé API manquante"
        : "Fetch météo...");
      return;
    }

    char buf[32];
    uint8_t row = 0;
    const char* unit = _useCelsius ? "C" : "F";

    // Ville
    d->setFont(u8x8_font_5x7_r);
    snprintf(buf, sizeof(buf), "%s,%s", _city, _country);
    d->drawString(0, row, buf); row++;

    // Température
    if (_showTemp) {
      d->setFont(u8x8_font_courB18_2x3_r);
      snprintf(buf, sizeof(buf), "%.1f %s", _temp, unit);
      d->drawString(0, row, buf); row += 3;
    }

    // Min/Max
    d->setFont(u8x8_font_5x7_r);
    if (!isnan(_tempMin) && !isnan(_tempMax)) {
      snprintf(buf, sizeof(buf), "%.0f / %.0f %s", _tempMin, _tempMax, unit);
      d->drawString(0, row, buf); row++;
    }

    // Ressenti
    if (_showFeelsLike && !isnan(_feelsLike)) {
      snprintf(buf, sizeof(buf), "Ressenti %.1f%s", _feelsLike, unit);
      d->drawString(0, row, buf); row++;
    }

    // Description
    if (_showDesc && strlen(_desc) > 0) {
      // Première lettre en majuscule
      char desc[32];
      strlcpy(desc, _desc, sizeof(desc));
      if (desc[0] >= 'a' && desc[0] <= 'z') desc[0] -= 32;
      d->setFont(u8x8_font_5x7_r);
      d->drawString(0, row, desc);
    }
  }

public:
  void setup() override {
    // Premier fetch différé de 10s (WiFi pas encore stable au boot)
    _lastFetch = millis() - (_updateHours * 3600000UL) + 10000UL;
  }

  void loop() override {
    if (!_enabled) return;
    unsigned long now     = millis();
    unsigned long interval = (unsigned long)_updateHours * 3600000UL;
    if (now - _lastFetch >= interval) {
      _lastFetch = now;
      fetchWeather();
      draw();
    }
  }

  // ── Config → frontend ────────────────────────────
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledWeather");
    top["enabled"]       = _enabled;
    top["apiKey"]        = _apiKey;     
    top["city"]          = _city;
    top["country"]       = _country;
    top["updateHours"]   = _updateHours;
    top["showTemp"]      = _showTemp;
    top["showDesc"]      = _showDesc;
    top["showFeelsLike"] = _showFeelsLike;
    top["useCelsius"]    = _useCelsius;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["OledWeather"];
    if (top.isNull()) return false;

    if (top.containsKey("enabled"))       _enabled      = top["enabled"];
    if (top.containsKey("updateHours"))   _updateHours  = top["updateHours"];
    if (top.containsKey("showTemp"))      _showTemp     = top["showTemp"];
    if (top.containsKey("showDesc"))      _showDesc     = top["showDesc"];
    if (top.containsKey("showFeelsLike")) _showFeelsLike= top["showFeelsLike"];
    if (top.containsKey("useCelsius"))    _useCelsius   = top["useCelsius"];

    strlcpy(_apiKey,  top["apiKey"]  | "",        sizeof(_apiKey));
    strlcpy(_city,    top["city"]    | "Epernay", sizeof(_city));
    strlcpy(_country, top["country"] | "FR",      sizeof(_country));

    return true;
  }

  // ── Info onglet ───────────────────────────────────
  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray info = user.createNestedArray("Météo");
    if (_fetchOk && !isnan(_temp)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.1f°%s %s",
        _temp, _useCelsius ? "C" : "F", _desc);
      info.add(buf);
    } else {
      info.add(_enabled ? "En attente..." : "Désactivé");
    }
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static OledWeatherUsermod oled_weather_mod;
REGISTER_USERMOD(oled_weather_mod);

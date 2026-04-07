#include "wled.h"
#include "../oled_base/oled_base.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define OLED_WEATHER_VIEW_ID 2

class OledWeatherUsermod : public Usermod {
private:
  bool    _enabled       = false;
  char    _apiKey[64]    = "";
  char    _city[32]      = "Epernay";
  char    _country[4]    = "FR";
  uint8_t _updateHours   = 1;
  bool    _showTemp      = true;
  bool    _showDesc      = true;
  bool    _showFeelsLike = false;
  bool    _useCelsius    = true;

  float   _temp          = NAN;
  float   _feelsLike     = NAN;
  float   _tempMin       = NAN;
  float   _tempMax       = NAN;
  char    _desc[32]      = "";
  bool    _fetchOk       = false;
  unsigned long _lastFetch = 0;
  unsigned long _lastDraw  = 0;

  void fetchWeather() {
    if (strlen(_apiKey) < 10) return;
    if (WiFi.status() != WL_CONNECTED) return;

    char url[220];
    const char* units = _useCelsius ? "metric" : "imperial";
    snprintf(url, sizeof(url),
      "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&appid=%s&units=%s&lang=fr",
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
        _fetchOk = true;
      }
    } else {
      _fetchOk = false;
    }
    http.end();
  }

  void draw() {
    auto* d = OledBaseUsermod::getDisplay();
    if (!d) return;

    d->clearDisplay();
    d->setFont(u8x8_font_7x14_1x2_r);

    if (!_fetchOk || isnan(_temp)) {
      d->drawString(0, 3, strlen(_apiKey) < 10 ? "Clé API manquante" : "Fetch météo...");
      return;
    }

    char buf[32];
    uint8_t row = 0;
    const char* unit = _useCelsius ? "C" : "F";

    d->setFont(u8x8_font_5x7_r);
    snprintf(buf, sizeof(buf), "%s,%s", _city, _country);
    d->drawString(0, row, buf); row++;

    if (_showTemp) {
      d->setFont(u8x8_font_courB18_2x3_r);
      snprintf(buf, sizeof(buf), "%.1f %s", _temp, unit);
      d->drawString(0, row, buf); row += 3;
    }

    d->setFont(u8x8_font_5x7_r);
    if (!isnan(_tempMin) && !isnan(_tempMax)) {
      snprintf(buf, sizeof(buf), "%.0f / %.0f %s", _tempMin, _tempMax, unit);
      d->drawString(0, row, buf); row++;
    }

    if (_showFeelsLike && !isnan(_feelsLike)) {
      snprintf(buf, sizeof(buf), "Ressenti %.1f%s", _feelsLike, unit);
      d->drawString(0, row, buf); row++;
    }

    if (_showDesc && strlen(_desc) > 0) {
      char desc[32];
      strlcpy(desc, _desc, sizeof(desc));
      if (desc[0] >= 'a' && desc[0] <= 'z') desc[0] -= 32;
      d->drawString(0, row, desc);
    }
  }

public:
  void setup() override {
    OledBaseUsermod::setViewActive(OLED_WEATHER_VIEW_ID, _enabled);
    _lastFetch = millis() - (_updateHours * 3600000UL) + 10000UL;
  }

  void loop() override {
    if (!_enabled || !OledBaseUsermod::isCurrentView(OLED_WEATHER_VIEW_ID)) return;

    unsigned long now = millis();
    unsigned long fetchInterval = (unsigned long)_updateHours * 3600000UL;
    if (now - _lastFetch >= fetchInterval) {
      _lastFetch = now;
      fetchWeather();
    }

    if (now - _lastDraw >= 30000UL) {
      _lastDraw = now;
      draw();
    }
  }

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

    OledBaseUsermod::setViewActive(OLED_WEATHER_VIEW_ID, _enabled);
    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray info = user.createNestedArray("Météo");
    info.add(_enabled ? "vue active" : "désactivée");
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static OledWeatherUsermod oled_weather_mod;
REGISTER_USERMOD(oled_weather_mod);

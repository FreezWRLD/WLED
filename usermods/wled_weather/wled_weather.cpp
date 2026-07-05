#include "wled.h"
#include "wled_weather_data.h"
#ifdef ARDUINO_ARCH_ESP32
  #include <HTTPClient.h>
#else
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#endif

// ============================================================
// Configuration OpenWeatherMap — modifier ici si besoin
// ============================================================
#define OWM_API_KEY    "35aa778ad543a33d93a9a9e9691a9a6b"
#define OWM_CITY       "Epernay"
#define OWM_COUNTRY    "FR"
#define OWM_INTERVAL   600000UL   // Intervalle de mise à jour (ms) — 10 min

// ============================================================
// Définition de l'objet météo partagé
// ============================================================
WeatherData g_weatherData;

// ============================================================
// Usermod
// ============================================================
class WledWeatherUsermod : public Usermod {
public:
  void setup() override {
    // Premier fetch 20 secondes après le démarrage (laisser le WiFi se connecter)
    _lastFetch = millis() - _interval + 20000UL;
  }

  void loop() override {
    if (WiFi.status() != WL_CONNECTED) return;
    if (millis() - _lastFetch < _interval) return;
    _lastFetch = millis();
    doFetch();
  }

  // ---- Config WLED UI ----
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("WledWeather");
    top["city"]        = _city;
    top["country"]     = _country;
    top["intervalMin"] = _interval / 60000UL;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["WledWeather"];
    if (top.isNull()) return false;
    strlcpy(_city,    top["city"]    | OWM_CITY,    sizeof(_city));
    strlcpy(_country, top["country"] | OWM_COUNTRY, sizeof(_country));
    uint32_t mins = top["intervalMin"] | 10;
    if (mins < 1) mins = 1;
    _interval = mins * 60000UL;
    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray arr = user.createNestedArray(F("Météo ext."));
    if (!g_weatherData.valid) {
      arr.add(F("En attente..."));
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.1f°C — %s", g_weatherData.temp, g_weatherData.main);
      arr.add(buf);
    }
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }

private:
  char     _city[32]    = OWM_CITY;
  char     _country[8]  = OWM_COUNTRY;
  uint32_t _interval    = OWM_INTERVAL;
  uint32_t _lastFetch   = 0;

  void doFetch() {
#ifndef ARDUINO_ARCH_ESP32
    WiFiClient wifiClient;
#endif
    HTTPClient http;

    String url = String(F("http://api.openweathermap.org/data/2.5/weather?q="))
               + _city + F(",") + _country
               + F("&appid=") + OWM_API_KEY
               + F("&units=metric&lang=fr");

    http.setTimeout(8000);
#ifdef ARDUINO_ARCH_ESP32
    if (!http.begin(url)) {
#else
    if (!http.begin(wifiClient, url)) {
#endif
      http.end();
      return;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      return;
    }

    String payload = http.getString();
    http.end();

    // Parsing JSON
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

    strlcpy(g_weatherData.main, doc["weather"][0]["main"] | "", sizeof(g_weatherData.main));
    strlcpy(g_weatherData.desc, doc["weather"][0]["description"] | "", sizeof(g_weatherData.desc));
    strlcpy(g_weatherData.city, doc["name"] | _city, sizeof(g_weatherData.city));
    // Capitaliser la première lettre
    if (g_weatherData.desc[0] >= 'a' && g_weatherData.desc[0] <= 'z')
      g_weatherData.desc[0] -= 32;

    g_weatherData.temp      = doc["main"]["temp"]       | NAN;
    g_weatherData.humidity  = doc["main"]["humidity"]   | NAN;
    g_weatherData.windKph   = (float)(doc["wind"]["speed"] | 0.0) * 3.6f; // m/s → km/h
    g_weatherData.pressure  = doc["main"]["pressure"]   | NAN;
    g_weatherData.feelsLike = doc["main"]["feels_like"] | NAN;

    // Lever/coucher du soleil — les timestamps Unix OWM sont UTC, on ajoute 7200s pour UTC+2 (CEST)
    long sr = (long)(doc["sys"]["sunrise"] | 0L) + 7200L;
    long ss = (long)(doc["sys"]["sunset"]  | 0L) + 7200L;
    g_weatherData.sunriseH = (uint8_t)((sr % 86400L) / 3600L);
    g_weatherData.sunriseM = (uint8_t)((sr % 3600L) / 60L);
    g_weatherData.sunsetH  = (uint8_t)((ss % 86400L) / 3600L);
    g_weatherData.sunsetM  = (uint8_t)((ss % 3600L) / 60L);

    g_weatherData.valid = true;
  }
};

static WledWeatherUsermod wled_weather_mod;
REGISTER_USERMOD(wled_weather_mod);

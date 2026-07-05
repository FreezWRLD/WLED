#pragma once
// ============================================================
// Données météo partagées entre wled_weather et wled_oled
// ============================================================

struct WeatherData {
  char    main[16]  = "";     // "Clear", "Rain", "Clouds", "Snow"...
  char    desc[48]  = "";     // Description localisée (ex: "Ciel dégagé")
  char    city[32]  = "";     // Nom de la ville
  float   temp      = NAN;    // Température extérieure °C
  float   humidity  = NAN;    // Humidité extérieure %
  float   windKph   = NAN;    // Vent km/h
  float   pressure  = NAN;    // Pression hPa
  float   feelsLike = NAN;    // Ressenti °C
  uint8_t sunriseH  = 6;      // Lever du soleil — heure (UTC+2)
  uint8_t sunriseM  = 0;
  uint8_t sunsetH   = 21;     // Coucher du soleil — heure (UTC+2)
  uint8_t sunsetM   = 0;
  bool    valid     = false;  // true si au moins un fetch OK
};

// Défini dans wled_weather.cpp
extern WeatherData g_weatherData;

#include "wled.h"
#include "../oled_base/oled_base.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

class OledWeatherUsermod : public Usermod {
private:
  bool    _enabled        = false;
  uint8_t _viewId         = 2;
  uint8_t _registeredId   = 2;
  char    _apiKey[64]     = "";
  char    _city[32]       = "Epernay";
  char    _country[4]     = "FR";
  char    _lang[8]        = "";       // vide = auto depuis WLED si possible, sinon en
  uint8_t _updateHours    = 1;
  bool    _showTemp       = true;
  bool    _showDesc       = true;
  bool    _showFeelsLike  = false;
  bool    _useCelsius     = true;
  bool    _useHttps       = true;

  float   _temp           = NAN;
  float   _feelsLike      = NAN;
  float   _tempMin        = NAN;
  float   _tempMax        = NAN;
  char    _desc[32]       = "";
  bool    _fetchOk        = false;
  bool    _fetchInProgress= false;
  bool    _loading        = false;
  unsigned long _lastFetchAttempt = 0;
  unsigned long _lastDraw  = 0;
  unsigned long _fetchStepStarted = 0;

  enum FetchState : uint8_t { FS_IDLE, FS_CONNECT, FS_SEND, FS_READ, FS_DONE, FS_ERROR };
  FetchState _state = FS_IDLE;

  WiFiClientSecure _client;
  String _request;
  String _response;
  String _langResolved;

  uint8_t sanitizeViewId(uint8_t id) {
    if (id < 1) return 1;
    if (id > 31) return 31;
    return id;
  }

  void syncViewRegistration() {
    _viewId = sanitizeViewId(_viewId);
    if (_registeredId != _viewId) {
      OledBaseUsermod::setViewActive(_registeredId, false);
      _registeredId = _viewId;
    }
    OledBaseUsermod::setViewActive(_registeredId, _enabled);
  }

  String detectWledLang() {
    // Pas de source interne documentée et stable trouvée ici pour exposer directement la langue UI.
    // On garde une tentative simple, sinon fallback en anglais.
    if (strlen(_lang) > 0) return String(_lang);
    return String("en");
  }

  void beginFetch() {
    if (strlen(_apiKey) < 10) {
      _fetchOk = false;
      _loading = false;
      return;
    }
    if (WiFi.status() != WL_CONNECTED) {
      _loading = false;
      return;
    }

    _langResolved = detectWledLang();
    const char* units = _useCelsius ? "metric" : "imperial";
    String scheme = _useHttps ? "https" : "http";
    String path = "/data/2.5/weather?q=" + String(_city) + "," + String(_country)
                + "&appid=" + String(_apiKey)
                + "&units=" + String(units)
                + "&lang=" + _langResolved;

    _request = String("GET ") + path + " HTTP/1.1\r\n"
             + "Host: api.openweathermap.org\r\n"
             + "User-Agent: WLED-OLED-Weather\r\n"
             + "Connection: close\r\n\r\n";

    _response = "";
    _client.stop();
    _client.setInsecure();
    _fetchInProgress = true;
    _loading = true;
    _state = FS_CONNECT;
    _fetchStepStarted = millis();
    _lastFetchAttempt = millis();
  }

  void finishFetchSuccess() {
    int bodyPos = _response.indexOf("\r\n\r\n");
    if (bodyPos < 0) {
      _state = FS_ERROR;
      return;
    }

    String body = _response.substring(bodyPos + 4);
    StaticJsonDocument<1536> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      _state = FS_ERROR;
      return;
    }

    _temp      = doc["main"]["temp"]       | _temp;
    _feelsLike = doc["main"]["feels_like"] | _feelsLike;
    _tempMin   = doc["main"]["temp_min"]   | _tempMin;
    _tempMax   = doc["main"]["temp_max"]   | _tempMax;
    const char* d = doc["weather"][0]["description"] | "";
    strlcpy(_desc, d, sizeof(_desc));

    _fetchOk = true;
    _fetchInProgress = false;
    _loading = false;
    _state = FS_IDLE;
    _client.stop();
  }

  void failFetch() {
    _fetchInProgress = false;
    _loading = false;
    _fetchOk = !isnan(_temp); // garde le cache précédent si dispo
    _state = FS_IDLE;
    _client.stop();
  }

  void processFetch() {
    if (!_fetchInProgress) return;
    unsigned long now = millis();

    if (now - _fetchStepStarted > 5000UL) {
      failFetch();
      return;
    }

    switch (_state) {
      case FS_CONNECT:
        if (_useHttps) {
          if (_client.connect("api.openweathermap.org", 443)) {
            _state = FS_SEND;
            _fetchStepStarted = now;
          }
        } else {
          failFetch();
        }
        break;

      case FS_SEND:
        _client.print(_request);
        _state = FS_READ;
        _fetchStepStarted = now;
        break;

      case FS_READ:
        while (_client.available()) {
          _response += char(_client.read());
          _fetchStepStarted = now;
        }
        if (!_client.connected() && !_client.available()) {
          finishFetchSuccess();
        }
        break;

      default:
        break;
    }
  }

  void draw() {
    auto* d = OledBaseUsermod::getDisplay();
    if (!d) return;

    d->clearDisplay();
    d->setFont(u8x8_font_7x14_1x2_r);

    if (_loading) {
      d->drawString(0, 3, "Fetch meteo...");
      return;
    }

    if ((!_fetchOk && isnan(_temp)) || strlen(_apiKey) < 10) {
      d->drawString(0, 3, strlen(_apiKey) < 10 ? "Cle API manquante" : "Meteo indispo");
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
    syncViewRegistration();
    _lastFetchAttempt = millis() - (_updateHours * 3600000UL) + 10000UL;
  }

  void loop() override {
    if (!_enabled) return;

    processFetch();

    unsigned long now = millis();
    unsigned long fetchInterval = (unsigned long)_updateHours * 3600000UL;
    if (!_fetchInProgress && now - _lastFetchAttempt >= fetchInterval) {
      beginFetch();
    }

    if (OledBaseUsermod::isCurrentView(_registeredId) && now - _lastDraw >= 1000UL) {
      _lastDraw = now;
      draw();
    }
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledWeather");
    top["enabled"]       = _enabled;
    top["viewId"]        = _viewId;
    top["apiKey"]        = _apiKey;
    top["city"]          = _city;
    top["country"]       = _country;
    top["lang"]          = _lang;
    top["updateHours"]   = _updateHours;
    top["showTemp"]      = _showTemp;
    top["showDesc"]      = _showDesc;
    top["showFeelsLike"] = _showFeelsLike;
    top["useCelsius"]    = _useCelsius;
    top["useHttps"]      = _useHttps;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["OledWeather"];
    if (top.isNull()) return false;

    if (top.containsKey("enabled"))       _enabled       = top["enabled"];
    if (top.containsKey("viewId"))        _viewId        = top["viewId"];
    if (top.containsKey("updateHours"))   _updateHours   = top["updateHours"];
    if (top.containsKey("showTemp"))      _showTemp      = top["showTemp"];
    if (top.containsKey("showDesc"))      _showDesc      = top["showDesc"];
    if (top.containsKey("showFeelsLike")) _showFeelsLike = top["showFeelsLike"];
    if (top.containsKey("useCelsius"))    _useCelsius    = top["useCelsius"];
    if (top.containsKey("useHttps"))      _useHttps      = top["useHttps"];

    strlcpy(_apiKey,  top["apiKey"]  | "",        sizeof(_apiKey));
    strlcpy(_city,    top["city"]    | "Epernay", sizeof(_city));
    strlcpy(_country, top["country"] | "FR",      sizeof(_country));
    strlcpy(_lang,    top["lang"]    | "",        sizeof(_lang));

    syncViewRegistration();
    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray info = user.createNestedArray("Meteo");
    if (_loading) {
      info.add("Chargement...");
    } else if (_fetchOk && !isnan(_temp)) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.1f%c%s %s", _temp, 176, _useCelsius ? "C" : "F", _desc);
      info.add(buf);
    } else {
      info.add(_enabled ? "En attente..." : "Desactive");
    }
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static OledWeatherUsermod oled_weather_mod;
REGISTER_USERMOD(oled_weather_mod);

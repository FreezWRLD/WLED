#include "wled.h"
#include "../oled_base/oled_base.h"

class OledClockUsermod : public Usermod {
private:
  bool    _enabled       = true;
  uint8_t _viewId        = 1;
  uint8_t _registeredId  = 1;
  bool    _showSeconds   = true;
  bool    _hour24        = true;
  bool    _blinkColon    = true;
  uint8_t _timeFontSize  = 2;
  char    _timePos[4]    = "tl";
  bool    _showDate      = true;
  uint8_t _dateFontSize  = 1;
  char    _datePos[4]    = "bl";
  uint8_t _dateFormat    = 0;
  unsigned long _lastDraw = 0;

  struct OledPos { uint8_t col; uint8_t row; };
  static OledPos resolvePos(const char* pos, bool isBig) {
    if      (strcmp(pos, "tl") == 0) return {0, 0};
    else if (strcmp(pos, "tc") == 0) return {2, 0};
    else if (strcmp(pos, "tr") == 0) return {8, 0};
    else if (strcmp(pos, "ml") == 0) return {0, 2};
    else if (strcmp(pos, "bl") == 0) return {0, isBig ? 4 : 6};
    else if (strcmp(pos, "bc") == 0) return {2, isBig ? 4 : 6};
    else if (strcmp(pos, "br") == 0) return {8, isBig ? 4 : 6};
    return {0, 0};
  }

  const uint8_t* getFont(uint8_t size) {
    switch (size) {
      case 3:  return u8x8_font_courB18_2x3_r;
      case 2:  return u8x8_font_7x14B_1x2_r;
      default: return u8x8_font_5x7_r;
    }
  }

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

  void formatTime(char* buf, size_t len, const tm* ti) {
    char sep = ':';
    if (_blinkColon && (ti->tm_sec % 2)) sep = ' ';

    if (_hour24) {
      if (_showSeconds) {
        snprintf(buf, len, "%02d%c%02d%c%02d", ti->tm_hour, sep, ti->tm_min, sep, ti->tm_sec);
      } else {
        snprintf(buf, len, "%02d%c%02d", ti->tm_hour, sep, ti->tm_min);
      }
    } else {
      int h = ti->tm_hour % 12;
      if (h == 0) h = 12;
      if (_showSeconds) {
        snprintf(buf, len, "%02d%c%02d%c%02d%s", h, sep, ti->tm_min, sep, ti->tm_sec, ti->tm_hour >= 12 ? "PM" : "AM");
      } else {
        snprintf(buf, len, "%02d%c%02d%s", h, sep, ti->tm_min, ti->tm_hour >= 12 ? "PM" : "AM");
      }
    }
  }

  void draw() {
    auto* d = OledBaseUsermod::getDisplay();
    if (!d) return;
    d->clearDisplay();

    char buf[24];
    time_t t = localTime;
    struct tm* ti = localtime(&t);
    if (ti->tm_year < 100) return;

    formatTime(buf, sizeof(buf), ti);
    OledPos tp = resolvePos(_timePos, _timeFontSize >= 3);
    d->setFont(getFont(_timeFontSize));
    d->drawString(tp.col, tp.row, buf);

    if (_showDate) {
      switch (_dateFormat) {
        case 1:  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", ti->tm_mon+1, ti->tm_mday, ti->tm_year+1900); break;
        case 2:  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday); break;
        default: snprintf(buf, sizeof(buf), "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon+1, ti->tm_year+1900); break;
      }
      OledPos dp = resolvePos(_datePos, false);
      d->setFont(getFont(_dateFontSize));
      d->drawString(dp.col, dp.row, buf);
    }
  }

public:
  void setup() override { syncViewRegistration(); }

  void loop() override {
    if (!_enabled || !OledBaseUsermod::isCurrentView(_registeredId)) return;
    unsigned long now = millis();
    uint32_t interval = (_showSeconds || _blinkColon) ? 1000 : 30000;
    if (now - _lastDraw < interval) return;
    _lastDraw = now;
    draw();
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledClock");
    top["enabled"]      = _enabled;
    top["viewId"]       = _viewId;
    top["showSeconds"]  = _showSeconds;
    top["hour24"]       = _hour24;
    top["blinkColon"]   = _blinkColon;
    top["timeFontSize"] = _timeFontSize;
    top["timePos"]      = _timePos;
    top["showDate"]     = _showDate;
    top["dateFontSize"] = _dateFontSize;
    top["datePos"]      = _datePos;
    top["dateFormat"]   = _dateFormat;
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root["OledClock"];
    if (top.isNull()) return false;

    if (top.containsKey("enabled"))      _enabled      = top["enabled"];
    if (top.containsKey("viewId"))       _viewId       = top["viewId"];
    if (top.containsKey("showSeconds"))  _showSeconds  = top["showSeconds"];
    if (top.containsKey("hour24"))       _hour24       = top["hour24"];
    if (top.containsKey("blinkColon"))   _blinkColon   = top["blinkColon"];
    if (top.containsKey("timeFontSize")) _timeFontSize = top["timeFontSize"];
    if (top.containsKey("showDate"))     _showDate     = top["showDate"];
    if (top.containsKey("dateFontSize")) _dateFontSize = top["dateFontSize"];
    if (top.containsKey("dateFormat"))   _dateFormat   = top["dateFormat"];
    if (top.containsKey("timePos")) strlcpy(_timePos, top["timePos"] | "tl", sizeof(_timePos));
    if (top.containsKey("datePos")) strlcpy(_datePos, top["datePos"] | "bl", sizeof(_datePos));

    syncViewRegistration();
    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray info = user.createNestedArray("Horloge");
    info.add(_enabled ? "active" : "désactivée");
    info.add(_registeredId);
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static OledClockUsermod oled_clock_mod;
REGISTER_USERMOD(oled_clock_mod);

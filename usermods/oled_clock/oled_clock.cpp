#include "wled.h"
#include "../oled_base/oled_base.h"  // Fixed: use header instead of cpp

// Positions disponibles pour le frontend
// "tl"=top-left "tc"=top-center "tr"=top-right
// "ml"=mid-left "bl"=bot-left   "bc"=bot-center "br"=bot-right
struct OledPos { uint8_t col; uint8_t row; };

static OledPos resolvePos(const char* pos, bool isBig) {
  // col = caractères (0-15), row = lignes (0-7)
  // une grande police (courB18 2x3) prend 3 lignes
  if      (strcmp(pos, "tl") == 0) return {0, 0};
  else if (strcmp(pos, "tc") == 0) return {2, 0};
  else if (strcmp(pos, "tr") == 0) return {8, 0};
  else if (strcmp(pos, "ml") == 0) return {0, 2};
  else if (strcmp(pos, "bl") == 0) return {0, isBig ? 4 : 6};
  else if (strcmp(pos, "bc") == 0) return {2, isBig ? 4 : 6};
  else if (strcmp(pos, "br") == 0) return {8, isBig ? 4 : 6};
  return {0, 0};
}

class OledClockUsermod : public Usermod {
private:
  bool    _enabled       = true;

  // ── Heure ────────────────────────────────────────
  bool    _showSeconds   = true;        
  bool    _hour24        = true;        
  uint8_t _timeFontSize  = 2;           
  char    _timePos[4]    = "tl";        

  // ── Date ─────────────────────────────────────────
  bool    _showDate      = true;
  uint8_t _dateFontSize  = 1;
  char    _datePos[4]    = "bl";
  uint8_t _dateFormat    = 0;           

  unsigned long _lastDraw = 0;

  const uint8_t* getFont(uint8_t size) {
    switch (size) {
      case 3:  return u8x8_font_courB18_2x3_r;
      case 2:  return u8x8_font_7x14B_1x2_r;
      default: return u8x8_font_5x7_r;
    }
  }

  void draw() {
    auto* d = OledBaseUsermod::getDisplay();
    if (!d) return;

    char buf[20];
    time_t t   = localTime;
    struct tm* ti = localtime(&t);
    if (ti->tm_year < 100) return; 

    // ── Heure ──
    if (_hour24) {
      if (_showSeconds)
        sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
      else
        sprintf(buf, "%02d:%02d", ti->tm_hour, ti->tm_min);
    } else {
      int h = ti->tm_hour % 12;
      if (h == 0) h = 12;
      if (_showSeconds)
        sprintf(buf, "%02d:%02d:%02d%s", h, ti->tm_min, ti->tm_sec, ti->tm_hour >= 12 ? "PM" : "AM");
      else
        sprintf(buf, "%02d:%02d%s", h, ti->tm_min, ti->tm_hour >= 12 ? "PM" : "AM");
    }
    OledPos tp = resolvePos(_timePos, _timeFontSize >= 3);
    d->setFont(getFont(_timeFontSize));
    d->drawString(tp.col, tp.row, buf);

    // ── Date ──
    if (_showDate) {
      switch (_dateFormat) {
        case 1:  sprintf(buf, "%02d/%02d/%04d", ti->tm_mon+1, ti->tm_mday, ti->tm_year+1900); break;
        case 2:  sprintf(buf, "%04d-%02d-%02d", ti->tm_year+1900, ti->tm_mon+1, ti->tm_mday); break;
        default: sprintf(buf, "%02d/%02d/%04d", ti->tm_mday, ti->tm_mon+1, ti->tm_year+1900); break;
      }
      OledPos dp = resolvePos(_datePos, false);
      d->setFont(getFont(_dateFontSize));
      d->drawString(dp.col, dp.row, buf);
    }
  }

public:
  void setup() override {}

  void loop() override {
    if (!_enabled) return;
    unsigned long now = millis();
    uint32_t interval = _showSeconds ? 1000 : 30000;
    if (now - _lastDraw < interval) return;
    _lastDraw = now;
    draw();
  }

  // ── Config ───────────────────────────────────────
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject("OledClock");
    top["enabled"]      = _enabled;
    top["showSeconds"]  = _showSeconds;
    top["hour24"]       = _hour24;
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
    if (top.containsKey("showSeconds"))  _showSeconds  = top["showSeconds"];
    if (top.containsKey("hour24"))       _hour24       = top["hour24"];
    if (top.containsKey("timeFontSize")) _timeFontSize = top["timeFontSize"];
    if (top.containsKey("showDate"))     _showDate     = top["showDate"];
    if (top.containsKey("dateFontSize")) _dateFontSize = top["dateFontSize"];
    if (top.containsKey("dateFormat"))   _dateFormat   = top["dateFormat"];

    // Strings → copie sécurisée
    if (top.containsKey("timePos"))
      strlcpy(_timePos, top["timePos"] | "tl", sizeof(_timePos));
    if (top.containsKey("datePos"))
      strlcpy(_datePos, top["datePos"] | "bl", sizeof(_datePos));

    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray info = user.createNestedArray("Horloge");
    time_t t = localTime;
    struct tm* ti = localtime(&t);
    if (ti->tm_year > 100) {
      char buf[20];
      sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
      info.add(buf);
    } else {
      info.add("NTP non synchro");
    }
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static OledClockUsermod oled_clock_mod;
REGISTER_USERMOD(oled_clock_mod);

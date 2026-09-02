// eAIS - AIS target list and plot for Xteink X3/X4 hardware.
//
// Sibling of eNMEA. Same hardware, same network/settings/power layers; where
// eNMEA answers "is this NMEA feed alive and correct", this one answers "what
// is out there". See README.md.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <InputManager.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PowerControl.h"
#include "ais/AisDecoder.h"
#include "ais/AisTypes.h"
#include "net/NmeaSource.h"
#include "nmea/NmeaSentence.h"
#include "settings/AppSettings.h"
#include "settings/ProvisioningPortal.h"
#include "ui/AisViews.h"
#include "ui/EinkCanvas.h"

namespace {

constexpr unsigned long REFRESH_MS = 2000;
// Targets move, so every frame is a full repaint. A deeper repaint less often
// still clears the ghosting that partial updates leave behind.
constexpr int FULL_REPAINT_EVERY_N = 15;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long PURGE_INTERVAL_MS = 30000;

constexpr unsigned long POWER_OFF_HOLD_MS = 2000;
constexpr unsigned long FORGET_HOLD_MS = 3000;
constexpr unsigned long HOLD_FEEDBACK_MS = 500;

bool connectToWifi(const NmeaProfile& profile);

EinkCanvas canvas;
AisViews views(canvas);
NmeaSource source;
ProvisioningPortal portal;
InputManager input;
BatteryMonitor battery;

AppSettings settings;
AisTargetTable targets;
OwnShip ownShip;
AisDecoder decoder;
ViewState view;

bool provisioningOnly = true;
int selectedIndex = -1;
unsigned long backHoldStartMs = 0;
bool powerHintShown = false;
bool backHintShown = false;
bool powerGestureArmed = false;
uint16_t batteryPct = 0;
bool batteryCharging = false;
bool batteryKnown = false;

// NMEA coordinate fields are "ddmm.mmmm" / "dddmm.mmmm": everything before the
// last two whole digits is degrees. Dividing by 100 and flooring isolates the
// minutes regardless of how many degree digits precede them.
double parseCoordinate(const char* field, const char* hemisphere) {
  if (field == nullptr || field[0] == '\0') return 0.0;
  const double raw = std::strtod(field, nullptr);
  const double degrees = static_cast<double>(static_cast<long>(raw / 100.0));
  double value = degrees + (raw - degrees * 100.0) / 60.0;
  if (hemisphere != nullptr && (hemisphere[0] == 'S' || hemisphere[0] == 'W')) value = -value;
  return value;
}

// Own-ship position from nav sentences, for the AIS units that send GPS data
// but not VDO. A VDO fix always wins: it is this vessel's own transponder
// output, which is what the rest of the fleet sees.
void handleNavSentence(const NmeaSentence& s, unsigned long nowMs) {
  if (ownShip.source == OwnShip::Source::Vdo) return;

  double lat = 0.0, lon = 0.0;
  bool got = false;
  if (std::strncmp(s.id, "RMC", 3) == 0 && s.fieldCount >= 7 && s.has(2) && s.field(2)[0] == 'A') {
    lat = parseCoordinate(s.field(3), s.field(4));
    lon = parseCoordinate(s.field(5), s.field(6));
    got = s.has(3) && s.has(5);
    if (s.has(7)) ownShip.sogKnots = std::strtof(s.field(7), nullptr);
    if (s.has(8)) ownShip.cogDeg = std::strtof(s.field(8), nullptr);
  } else if (std::strncmp(s.id, "GGA", 3) == 0 && s.fieldCount >= 7 && std::atoi(s.field(6)) > 0) {
    lat = parseCoordinate(s.field(2), s.field(3));
    lon = parseCoordinate(s.field(4), s.field(5));
    got = s.has(2) && s.has(4);
  } else if (std::strncmp(s.id, "GLL", 3) == 0 && s.fieldCount >= 7 && s.has(6) && s.field(6)[0] == 'A') {
    lat = parseCoordinate(s.field(1), s.field(2));
    lon = parseCoordinate(s.field(3), s.field(4));
    got = s.has(1) && s.has(3);
  }

  if (got) {
    ownShip.latDeg = lat;
    ownShip.lonDeg = lon;
    ownShip.hasPosition = true;
    ownShip.lastPositionMs = nowMs;
    ownShip.source = OwnShip::Source::NavSentence;
  }
}

// Called for every complete sentence the source produces.
void onSentence(char* line, void*) {
  const unsigned long now = millis();
  const NmeaSentence s = NmeaSentence::parse(line);
  if (!s.valid || !s.hasAddress) return;

  if (std::strncmp(s.id, "VDM", 3) == 0 || std::strncmp(s.id, "VDO", 3) == 0) {
    decoder.handle(s, targets, ownShip, now);
  } else {
    handleNavSentence(s, now);
  }
}

void formatBattery(char* out, size_t outLen) {
  const BatteryMonitor::Status s = battery.readStatus();
  if (s.supported && s.percentageKnown) {
    batteryPct = s.percentage;
    batteryKnown = true;
    if (s.chargingKnown) batteryCharging = s.charging;
  }
  if (!batteryKnown) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, outLen, "%s %u%%", batteryCharging ? "CHRG" : "BATT", static_cast<unsigned>(batteryPct));
}

void banner(const char* message) {
  canvas.fillRect(0, 24, canvas.width(), 17, false);
  canvas.drawText(8, 26, message, 2, true);
  canvas.present(EInkDisplay::FAST_REFRESH);
}

bool applyProfile(int index) {
  if (!settings.indexValid(index)) return false;
  const NmeaProfile& p = settings.profiles[index];
  Serial.printf("[eAIS] Applying profile %d '%s'\n", index + 1, p.name);

  source.end();
  // Targets belong to the unit that reported them; carrying them across a
  // switch would show vessels the new source has never mentioned.
  targets = AisTargetTable{};
  ownShip = OwnShip{};
  decoder = AisDecoder{};

  settings.activeIndex = static_cast<int8_t>(index);
  selectedIndex = index;
  saveAppSettings(settings);

  canvas.clear();
  canvas.drawText(24, 60, "SWITCHING...", 2, true);
  canvas.drawText(24, 100, p.name, 2, true);
  canvas.present(EInkDisplay::HALF_REFRESH);

  const bool joined = connectToWifi(p);
  if (joined) {
    source.begin(p);
    portal.beginOnStation();
  }
  provisioningOnly = false;
  return joined;
}

void forgetActiveProfile() {
  if (!settings.hasActive()) return;
  const int cleared = settings.activeIndex;
  Serial.printf("[eAIS] Forgetting profile %d '%s'\n", cleared + 1, settings.profiles[cleared].name);
  settings.profiles[cleared] = NmeaProfile{};
  const int next = settings.firstUsed();
  settings.activeIndex = static_cast<int8_t>(next);
  saveAppSettings(settings);
  if (next >= 0) {
    applyProfile(next);
    return;
  }
  ESP.restart();
}

void handleGestures() {
  input.update();

  if (!input.isPowerButtonPressed()) {
    powerGestureArmed = true;
    powerHintShown = false;
  } else if (powerGestureArmed) {
    const unsigned long held = input.getPowerButtonHeldTime();
    if (held >= POWER_OFF_HOLD_MS) powerOff(canvas);  // does not return
    if (held >= HOLD_FEEDBACK_MS && !powerHintShown) {
      powerHintShown = true;
      banner("KEEP HOLDING TO SHUT DOWN");
    }
  }

  if (input.isPressed(InputManager::BTN_BACK)) {
    if (backHoldStartMs == 0) backHoldStartMs = millis();
    const unsigned long held = millis() - backHoldStartMs;
    if (held >= FORGET_HOLD_MS) {
      backHoldStartMs = 0;
      backHintShown = false;
      forgetActiveProfile();
      return;
    }
    if (held >= HOLD_FEEDBACK_MS && !backHintShown) {
      backHintShown = true;
      banner("KEEP HOLDING TO FORGET THIS PROFILE");
    }
  } else {
    backHoldStartMs = 0;
    backHintShown = false;
  }

  if (provisioningOnly) return;

  // CONFIRM toggles between the list and the plot - the two ways of reading the
  // same targets, and the switch a user makes most often.
  if (input.wasPressed(InputManager::BTN_CONFIRM)) {
    view.mode = (view.mode == ViewMode::List) ? ViewMode::Plot : ViewMode::List;
    view.listScroll = 0;
  }

  // UP/DOWN mean different things per view, which is the point: in the list you
  // scroll, on the plot you change range. Both are the obvious vertical action.
  const bool up = input.wasPressed(InputManager::BTN_UP);
  const bool down = input.wasPressed(InputManager::BTN_DOWN);
  if (up || down) {
    if (view.mode == ViewMode::Plot) {
      view.rangeIndex += up ? 1 : -1;
      if (view.rangeIndex >= view.rangeCount()) view.rangeIndex = view.rangeCount() - 1;
      if (view.rangeIndex < 0) view.rangeIndex = 0;
    } else {
      view.listScroll += up ? -1 : 1;
      if (view.listScroll < 0) view.listScroll = 0;
      const int maxScroll = targets.count - views.listRows();
      if (view.listScroll > maxScroll) view.listScroll = maxScroll < 0 ? 0 : maxScroll;
    }
  }

  // LEFT/RIGHT step through profiles. Unlike eNMEA these apply immediately -
  // there is no separate commit button left, and switching source is cheap.
  if (settings.usedCount() > 1) {
    if (input.wasPressed(InputManager::BTN_LEFT) || input.wasPressed(InputManager::BTN_RIGHT)) {
      const int next = settings.nextUsed(selectedIndex, input.wasPressed(InputManager::BTN_LEFT) ? -1 : 1);
      if (next >= 0 && next != settings.activeIndex) applyProfile(next);
    }
  }
}

void drawSetupScreen(const char* headline) {
  canvas.clear();
  canvas.drawText(20, 40, headline, 3, true);
  canvas.drawText(20, 90, "CONNECT TO WIFI:", 1, true);
  canvas.drawText(20, 105, SETUP_AP_SSID, 2, true);
  canvas.drawText(20, 135, "THEN BROWSE TO 192.168.7.1", 1, true);
  canvas.drawText(20, canvas.height() - 14, "HOLD POWER 2S: SHUT DOWN", 1, true);
  canvas.present(EInkDisplay::HALF_REFRESH);
}

bool connectToWifi(const NmeaProfile& profile) {
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        Serial.printf("[eAIS] WiFi disconnect reason=%u\n", info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false, 1000);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  Serial.printf("[eAIS] Joining '%s'...\n", profile.ssid);
  WiFi.begin(profile.ssid, profile.password);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) delay(200);

  const wl_status_t status = WiFi.status();
  Serial.printf("[eAIS] WiFi.status() = %d\n", static_cast<int>(status));
  if (status == WL_CONNECTED) {
    Serial.printf("[eAIS] Connected. IP %s  gw %s  rssi %d dBm\n", WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  }
  return status == WL_CONNECTED;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Both must run before anything else touches a pin - see eNMEA's notes: they
  // assert the battery latch and clear GPIO holds that survive a reset.
  BoardConfig::holdPowerRails();
  BoardConfig::releaseSdRail();

  canvas.begin();
  canvas.clear();
  input.begin();

  Serial.printf("[eAIS] Board: %s, panel %ux%u\n", BoardConfig::ACTIVE.name,
                static_cast<unsigned>(canvas.width()), static_cast<unsigned>(canvas.height()));

  loadAppSettings(settings);
  selectedIndex = settings.activeIndex;

  if (!settings.hasActive()) {
    Serial.println("[eAIS] No usable profile - starting setup AP");
    drawSetupScreen("SETUP MODE");
    portal.beginAsAccessPoint();
    return;
  }

  const NmeaProfile& profile = settings.profiles[settings.activeIndex];
  canvas.drawText(20, 40, "CONNECTING...", 2, true);
  canvas.present(EInkDisplay::HALF_REFRESH);

  if (!connectToWifi(profile)) {
    drawSetupScreen("WIFI FAILED");
    portal.beginAsAccessPoint();
    return;
  }

  source.begin(profile);
  portal.beginOnStation();
  provisioningOnly = false;
}

void loop() {
  handleGestures();
  portal.poll();

  if (provisioningOnly) {
    delay(2);
    return;
  }

  static unsigned long lastDraw = 0;
  static unsigned long lastPurge = 0;
  static int frame = 0;

  source.poll(onSentence, nullptr);

  const unsigned long now = millis();
  if (now - lastPurge > PURGE_INTERVAL_MS) {
    lastPurge = now;
    targets.purgeStale(now);
  }
  if (now - lastDraw < REFRESH_MS) {
    delay(2);
    return;
  }
  lastDraw = now;

  char netLine[56];
  if (WiFi.status() == WL_CONNECTED) {
    std::snprintf(netLine, sizeof(netLine), "SETUP: %s OR AP", WiFi.localIP().toString().c_str());
  } else {
    std::snprintf(netLine, sizeof(netLine), "WIFI DOWN - SETUP AP %s", SETUP_AP_IP);
  }
  char battLine[16];
  formatBattery(battLine, sizeof(battLine));

  const NmeaProfile& profile = settings.hasActive() ? settings.profiles[settings.activeIndex] : settings.profiles[0];
  views.draw(targets, ownShip, profile, view, source.stateText(), netLine, battLine, now);

  ++frame;
  canvas.present(frame % FULL_REPAINT_EVERY_N == 0 ? EInkDisplay::HALF_REFRESH : EInkDisplay::FAST_REFRESH);
}

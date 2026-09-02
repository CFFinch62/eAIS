#pragma once

#include "ais/AisTypes.h"
#include "settings/AppSettings.h"
#include "ui/EinkCanvas.h"

// The two ways of looking at the same target table.
//
// LIST answers "what is out there and who are they" - the question when
// commissioning or checking an installation, and the one that still works when
// there is no own-ship position at all.
//
// PLOT answers "where are they relative to me", and needs an own position. On a
// 1-bit panel that refreshes every couple of seconds it is a situational
// sketch, not a radar: no trails, no CPA, no guard zones. Anything implying
// collision-avoidance accuracy would be dishonest at this refresh rate.
enum class ViewMode : uint8_t { List = 0, Plot };

// Plot range in nautical miles. Stepped rather than continuous so the button
// cycle is predictable and the ring labels stay legible.
constexpr float RANGE_SCALES_NM[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
constexpr int RANGE_SCALE_COUNT = sizeof(RANGE_SCALES_NM) / sizeof(RANGE_SCALES_NM[0]);

struct ViewState {
  ViewMode mode = ViewMode::List;
  int rangeIndex = 2;      // 2 nm
  int listScroll = 0;      // first row shown, for lists longer than the panel
  int rangeCount() const { return RANGE_SCALE_COUNT; }
  float rangeNm() const { return RANGE_SCALES_NM[rangeIndex]; }
};

class AisViews {
 public:
  explicit AisViews(EinkCanvas& canvas) : canvas_(canvas) {}

  // Full-frame redraw. Both views repaint everything rather than patching
  // regions: targets move, so there is no stable chrome to preserve, and the
  // shared EinkCanvas re-syncs the write buffer after every present anyway.
  void draw(const AisTargetTable& targets, const OwnShip& own, const NmeaProfile& profile, const ViewState& view,
            const char* sourceState, const char* netLine, const char* batteryText, unsigned long nowMs);

  // Rows the list can show at once - the caller needs this to clamp scrolling.
  int listRows() const;

 private:
  EinkCanvas& canvas_;

  void drawHeader(const NmeaProfile& profile, const OwnShip& own, const AisTargetTable& targets, const ViewState& view,
                  const char* sourceState, const char* netLine, const char* batteryText, unsigned long nowMs);
  void drawList(const AisTargetTable& targets, const OwnShip& own, const ViewState& view, unsigned long nowMs);
  void drawPlot(const AisTargetTable& targets, const OwnShip& own, const ViewState& view, unsigned long nowMs);
  void drawFooter();
  void drawTargetSymbol(int cx, int cy, float cogDeg, bool aging);
};

// Range and bearing from own ship to a target, in nautical miles and degrees
// true. Equirectangular rather than great-circle: at plot ranges the error is
// far below one pixel, and this runs for every target on every redraw.
void rangeBearing(double fromLat, double fromLon, double toLat, double toLon, float& rangeNm, float& bearingDeg);

#include "AisViews.h"

#include "Draw.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Text scales, matching eNMEA's reasoning: the 5x7 font has 1px strokes and is
// hard to read at scale 1 across a workshop. Everything read at a glance is
// scale 2; scale 1 is for reference text you walk up to.
constexpr int TEXT_BODY = 2;
constexpr int TEXT_FINE = 1;
constexpr int GLYPH_ADV_2 = 12;

constexpr int TITLE_Y = 6;
constexpr int STATUS_Y = 26;
constexpr int DIVIDER_Y = 46;
constexpr int CONTENT_TOP = 54;
constexpr int FOOTER_RESERVE = 22;

constexpr int LIST_X = 8;
constexpr int LIST_ROW_H = 24;

constexpr double NM_PER_DEG_LAT = 60.0;

int contentBottom(const EinkCanvas& c) { return c.height() - FOOTER_RESERVE; }

// Bearing degrees to screen direction. North is up, so bearing 0 is -y.
void bearingToXY(float bearingDeg, float distancePx, int& dx, int& dy) {
  const float rad = bearingDeg * 3.14159265358979f / 180.0f;
  dx = static_cast<int>(lroundf(distancePx * sinf(rad)));
  dy = static_cast<int>(-lroundf(distancePx * cosf(rad)));
}

}  // namespace

void rangeBearing(double fromLat, double fromLon, double toLat, double toLon, float& rangeNm, float& bearingDeg) {
  const double dLatNm = (toLat - fromLat) * NM_PER_DEG_LAT;
  // Longitude degrees shrink with latitude; without this a target due east
  // plots at the wrong distance everywhere except the equator.
  const double dLonNm = (toLon - fromLon) * NM_PER_DEG_LAT * cos(fromLat * 3.14159265358979 / 180.0);
  rangeNm = static_cast<float>(sqrt(dLatNm * dLatNm + dLonNm * dLonNm));
  double b = atan2(dLonNm, dLatNm) * 180.0 / 3.14159265358979;
  if (b < 0) b += 360.0;
  bearingDeg = static_cast<float>(b);
}

int AisViews::listRows() const {
  // One header row is consumed by the column titles.
  return (contentBottom(canvas_) - CONTENT_TOP) / LIST_ROW_H - 1;
}

void AisViews::drawHeader(const NmeaProfile& profile, const OwnShip& own, const AisTargetTable& targets,
                          const ViewState& view, const char* sourceState, const char* netLine,
                          const char* batteryText, unsigned long nowMs) {
  char title[64];
  if (profile.name[0] != '\0') {
    std::snprintf(title, sizeof(title), "eAIS - %s", profile.name);
  } else {
    std::snprintf(title, sizeof(title), "eAIS - AIS TARGET DISPLAY");
  }
  canvas_.drawText(8, TITLE_Y, title, TEXT_BODY, true);

  char right[64];
  if (view.mode == ViewMode::Plot) {
    std::snprintf(right, sizeof(right), "PLOT  %.1f NM", static_cast<double>(view.rangeNm()));
  } else {
    std::snprintf(right, sizeof(right), "LIST");
  }
  canvas_.drawText(canvas_.width() - canvas_.textWidth(right, TEXT_BODY) - 8, TITLE_Y, right, TEXT_BODY, true);

  // Status row: how many targets, where own position came from, link state.
  // "OWN: NONE" is information, not an error - plenty of AIS units are
  // configured to send target data only.
  char status[80];
  const char* ownSrc = own.source == OwnShip::Source::Vdo      ? "VDO"
                       : own.source == OwnShip::Source::NavSentence ? "GPS"
                                                                    : "NONE";
  std::snprintf(status, sizeof(status), "%s  TGTS %d/%d  OWN:%s", sourceState, targets.positionCount(nowMs),
                targets.liveCount(nowMs), ownSrc);
  canvas_.drawText(LIST_X, STATUS_Y, status, TEXT_BODY, true);

  int rightEdge = canvas_.width() - 8;
  if (batteryText != nullptr && batteryText[0] != '\0') {
    const int w = canvas_.textWidth(batteryText, TEXT_BODY);
    canvas_.drawText(rightEdge - w, STATUS_Y, batteryText, TEXT_BODY, true);
    rightEdge -= w + 12;
  }
  if (netLine != nullptr && netLine[0] != '\0') {
    canvas_.drawText(rightEdge - canvas_.textWidth(netLine, TEXT_FINE), STATUS_Y + 4, netLine, TEXT_FINE, true);
  }

  canvas_.drawHLine(0, DIVIDER_Y, canvas_.width(), true);
}

void AisViews::drawFooter() {
  canvas_.drawText(8, canvas_.height() - 16,
                   "CONFIRM: LIST/PLOT   UP/DOWN: SCROLL OR RANGE   LEFT/RIGHT: PROFILE   "
                   "HOLD POWER 2S: OFF",
                   TEXT_FINE, true);
}

void AisViews::drawList(const AisTargetTable& targets, const OwnShip& own, const ViewState& view,
                        unsigned long nowMs) {
  // Column layout. Name gets the most room because it is the only field a user
  // can match against something they can see out of the window.
  const int colName = LIST_X;
  const int colMmsi = LIST_X + 16 * GLYPH_ADV_2;
  const int colSog = colMmsi + 10 * GLYPH_ADV_2;
  const int colCog = colSog + 7 * GLYPH_ADV_2;
  const int colRange = colCog + 6 * GLYPH_ADV_2;

  canvas_.drawText(colName, CONTENT_TOP, "NAME", TEXT_BODY, true);
  canvas_.drawText(colMmsi, CONTENT_TOP, "MMSI", TEXT_BODY, true);
  canvas_.drawText(colSog, CONTENT_TOP, "SOG", TEXT_BODY, true);
  canvas_.drawText(colCog, CONTENT_TOP, "COG", TEXT_BODY, true);
  if (own.hasPosition) canvas_.drawText(colRange, CONTENT_TOP, "RNG BRG", TEXT_BODY, true);
  canvas_.drawHLine(LIST_X, CONTENT_TOP + 18, canvas_.width() - 2 * LIST_X, true);

  const int rows = listRows();
  int drawn = 0;
  for (int i = view.listScroll; i < targets.count && drawn < rows; ++i) {
    const AisTarget& t = targets.entries[i];
    if (t.isStale(nowMs)) continue;
    const int y = CONTENT_TOP + (drawn + 1) * LIST_ROW_H;

    char buf[24];
    // A vessel with no name yet is shown as its MMSI rather than left blank -
    // "we have heard it but not its name" is different from "nothing here".
    canvas_.drawText(colName, y, t.hasName() ? t.name : "(NO NAME)", TEXT_BODY, true);
    std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(t.mmsi));
    canvas_.drawText(colMmsi, y, buf, TEXT_BODY, true);

    if (t.hasPosition) {
      std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(t.sogKnots));
      canvas_.drawText(colSog, y, buf, TEXT_BODY, true);
      std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(t.cogDeg));
      canvas_.drawText(colCog, y, buf, TEXT_BODY, true);
      if (own.hasPosition) {
        float rng = 0.0f, brg = 0.0f;
        rangeBearing(own.latDeg, own.lonDeg, t.latDeg, t.lonDeg, rng, brg);
        std::snprintf(buf, sizeof(buf), "%.1f %.0f", static_cast<double>(rng), static_cast<double>(brg));
        canvas_.drawText(colRange, y, buf, TEXT_BODY, true);
      }
    } else {
      canvas_.drawText(colSog, y, "--", TEXT_BODY, true);
    }
    ++drawn;
  }

  if (drawn == 0) {
    canvas_.drawText(LIST_X, CONTENT_TOP + LIST_ROW_H * 2, "NO TARGETS HEARD YET", TEXT_BODY, true);
    canvas_.drawText(LIST_X, CONTENT_TOP + LIST_ROW_H * 3,
                     "CHECK THE SOURCE STATE ABOVE - LISTENING MEANS NOTHING HAS ARRIVED", TEXT_FINE, true);
  }
}

void AisViews::drawTargetSymbol(int cx, int cy, float cogDeg, bool aging) {
  // A triangle pointing along COG, which is the one thing a glance should
  // convey. Aging targets are drawn as an outline only: still there, position
  // no longer fresh.
  // Sized generously: on a 1-bit panel at arm's length a small outline reads as
  // a smudge, and the first version was too small to judge placement by eye.
  int nx = 0, ny = 0, lx = 0, ly = 0, rx = 0, ry = 0;
  bearingToXY(cogDeg, 16.0f, nx, ny);
  bearingToXY(cogDeg + 143.0f, 12.0f, lx, ly);
  bearingToXY(cogDeg - 143.0f, 12.0f, rx, ry);

  // Drawn twice, one pixel apart: e-ink has no anti-aliasing, and a
  // single-pixel outline vanishes against the range rings.
  for (int pass = 0; pass < 2; ++pass) {
    drawLine(canvas_, cx + nx + pass, cy + ny, cx + lx + pass, cy + ly, true);
    drawLine(canvas_, cx + nx + pass, cy + ny, cx + rx + pass, cy + ry, true);
    drawLine(canvas_, cx + lx + pass, cy + ly, cx + rx + pass, cy + ry, true);
  }
  // A fresh position gets a solid centre; an aging one stays hollow.
  if (!aging) canvas_.fillRect(cx - 3, cy - 3, 7, 7, true);
}

void AisViews::drawPlot(const AisTargetTable& targets, const OwnShip& own, const ViewState& view,
                        unsigned long nowMs) {
  const int top = CONTENT_TOP;
  const int bottom = contentBottom(canvas_);
  const int cx = canvas_.width() / 2;
  const int cy = (top + bottom) / 2;
  const int maxRadius = ((bottom - top) / 2) - 12;

  if (!own.hasPosition) {
    // Without an own position there is nothing to plot around. Say why, and
    // say what to do, rather than showing empty rings.
    canvas_.drawText(LIST_X, top + 40, "NO OWN POSITION - CANNOT PLOT", TEXT_BODY, true);
    canvas_.drawText(LIST_X, top + 70, "THE PLOT NEEDS THIS VESSEL'S POSITION, FROM EITHER", TEXT_FINE, true);
    canvas_.drawText(LIST_X, top + 85, "AN AIVDO SENTENCE OR GPS NAV DATA (RMC/GGA/GLL).", TEXT_FINE, true);
    canvas_.drawText(LIST_X, top + 100, "MANY AIS UNITS SEND TARGET DATA ONLY - CHECK THE UNIT'S", TEXT_FINE, true);
    canvas_.drawText(LIST_X, top + 115, "OUTPUT SETTINGS. THE TARGET LIST STILL WORKS.", TEXT_FINE, true);
    return;
  }

  // Range rings at half and full scale, labelled so the scale is never guessed.
  drawCircle(canvas_, cx, cy, maxRadius, true);
  drawCircle(canvas_, cx, cy, maxRadius / 2, true);
  char label[16];
  std::snprintf(label, sizeof(label), "%.1f", static_cast<double>(view.rangeNm()));
  canvas_.drawText(cx + 4, cy - maxRadius - 16, label, TEXT_FINE, true);
  std::snprintf(label, sizeof(label), "%.1f", static_cast<double>(view.rangeNm() / 2.0f));
  canvas_.drawText(cx + 4, cy - maxRadius / 2 - 16, label, TEXT_FINE, true);

  // Bearing scale. Without a reference on the ring there is no way to check by
  // eye whether a target sits where its bearing says - which is the whole point
  // of being able to trust the plot.
  for (int brg = 0; brg < 360; brg += 30) {
    int ox = 0, oy = 0, ix = 0, iy = 0;
    bearingToXY(static_cast<float>(brg), static_cast<float>(maxRadius), ox, oy);
    bearingToXY(static_cast<float>(brg), static_cast<float>(maxRadius - (brg % 90 == 0 ? 14 : 7)), ix, iy);
    drawLine(canvas_, cx + ix, cy + iy, cx + ox, cy + oy, true);
  }
  const char* cardinals[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    int lx = 0, ly = 0;
    bearingToXY(static_cast<float>(i * 90), static_cast<float>(maxRadius + 16), lx, ly);
    canvas_.drawText(cx + lx - 5, cy + ly - 7, cardinals[i], TEXT_BODY, true);
  }

  // Own ship: a cross at the centre, with a heading line when known.
  canvas_.drawHLine(cx - 6, cy, 13, true);
  canvas_.drawVLine(cx, cy - 6, 13, true);
  const float ownDir = own.headingDeg >= 0.0f ? own.headingDeg : own.cogDeg;
  int hx = 0, hy = 0;
  bearingToXY(ownDir, static_cast<float>(maxRadius / 3), hx, hy);
  drawLine(canvas_, cx, cy, cx + hx, cy + hy, true);

  const float scalePxPerNm = static_cast<float>(maxRadius) / view.rangeNm();
  int plotted = 0, offScale = 0;
  // Bearing/range labels help until the plot is busy, then they become noise.
  const bool uncrowded = targets.positionCount(nowMs) <= 8;
  for (int i = 0; i < targets.count; ++i) {
    const AisTarget& t = targets.entries[i];
    if (!t.hasPosition || t.isStale(nowMs)) continue;

    float rng = 0.0f, brg = 0.0f;
    rangeBearing(own.latDeg, own.lonDeg, t.latDeg, t.lonDeg, rng, brg);
    if (rng > view.rangeNm()) {
      ++offScale;
      continue;
    }
    int dx = 0, dy = 0;
    bearingToXY(brg, rng * scalePxPerNm, dx, dy);
    drawTargetSymbol(cx + dx, cy + dy, t.cogDeg, t.isAging(nowMs));

    // Label with the name when there is one, otherwise the MMSI's last digits -
    // enough to tell two symbols apart without crowding the plot.
    char tag[20];
    if (t.hasName()) {
      std::snprintf(tag, sizeof(tag), "%.10s", t.name);
    } else {
      std::snprintf(tag, sizeof(tag), "%lu", static_cast<unsigned long>(t.mmsi % 10000));
    }
    canvas_.drawText(cx + dx + 18, cy + dy - 10, tag, TEXT_FINE, true);
    // On an uncrowded plot, print each target's bearing and range beside it.
    // That is what makes placement verifiable: the number and the position must
    // agree, so a wrong projection shows up immediately instead of looking
    // plausible.
    if (uncrowded) {
      std::snprintf(tag, sizeof(tag), "%03.0f/%.2f", static_cast<double>(brg), static_cast<double>(rng));
      canvas_.drawText(cx + dx + 18, cy + dy + 2, tag, TEXT_FINE, true);
    }
    ++plotted;
  }

  // Targets outside the ring are counted, never silently dropped - "nothing on
  // screen" must not be confusable with "nothing out there".
  char summary[64];
  if (offScale > 0) {
    std::snprintf(summary, sizeof(summary), "%d SHOWN  %d BEYOND %.1f NM", plotted, offScale,
                  static_cast<double>(view.rangeNm()));
  } else {
    std::snprintf(summary, sizeof(summary), "%d SHOWN", plotted);
  }
  canvas_.drawText(LIST_X, bottom - 14, summary, TEXT_FINE, true);
}

void AisViews::draw(const AisTargetTable& targets, const OwnShip& own, const NmeaProfile& profile,
                    const ViewState& view, const char* sourceState, const char* netLine, const char* batteryText,
                    unsigned long nowMs) {
  canvas_.clear();
  drawHeader(profile, own, targets, view, sourceState, netLine, batteryText, nowMs);
  if (view.mode == ViewMode::Plot) {
    drawPlot(targets, own, view, nowMs);
  } else {
    drawList(targets, own, view, nowMs);
  }
  drawFooter();
}

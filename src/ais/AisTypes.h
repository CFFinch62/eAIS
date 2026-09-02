#pragma once

#include <cstdint>
#include <cstring>

// AIS target model.
//
// Deliberately flat and fixed-size: no heap, no strings, so the table can live
// as a global and be reasoned about at compile time. 128 targets is well past
// what a busy anchorage produces and costs about 10 KB.
constexpr int MAX_AIS_TARGETS = 128;

// How long a target stays in the list after its last report. AIS Class A
// transmits every 2-10s underway but only every 3 minutes at anchor, and Class
// B every 30s or 3 minutes; anything shorter than ~6 minutes would drop moored
// vessels that are still very much there.
constexpr unsigned long TARGET_STALE_MS = 360000;

// Reports older than this are drawn hollow rather than solid - present, but the
// position is no longer fresh enough to trust for close-quarters work.
constexpr unsigned long TARGET_AGING_MS = 60000;

enum class AisClass : uint8_t { Unknown = 0, A, B, Base, Aton };

struct AisTarget {
  uint32_t mmsi = 0;
  char name[21] = {0};      // AIS names are 20 six-bit chars, uppercase only
  char callsign[8] = {0};

  bool hasPosition = false;
  double latDeg = 0.0;      // + north
  double lonDeg = 0.0;      // + east
  float sogKnots = 0.0f;
  float cogDeg = 0.0f;      // course over ground, true
  float headingDeg = -1.0f; // true heading, -1 when not available (511 = n/a)
  uint8_t navStatus = 15;   // 15 = undefined
  AisClass cls = AisClass::Unknown;

  unsigned long lastSeenMs = 0;    // any message
  unsigned long lastPositionMs = 0;

  bool isStale(unsigned long nowMs) const { return nowMs - lastSeenMs > TARGET_STALE_MS; }
  bool isAging(unsigned long nowMs) const { return nowMs - lastPositionMs > TARGET_AGING_MS; }
  bool hasName() const { return name[0] != '\0'; }
};

// Own vessel, from VDO or from nav sentences depending on what the AIS unit is
// configured to output. Both paths are supported because the four models this
// is built for differ exactly there: some send target data only, some send
// VDM/VDO plus RMC/GGA/GLL/GSA.
struct OwnShip {
  bool hasPosition = false;
  double latDeg = 0.0;
  double lonDeg = 0.0;
  float sogKnots = 0.0f;
  float cogDeg = 0.0f;
  float headingDeg = -1.0f;
  uint32_t mmsi = 0;
  unsigned long lastPositionMs = 0;
  // Which source last supplied a fix - shown on screen, because "the plot is
  // centred on a position from my own transponder" and "...from a GPS sentence"
  // are different levels of confidence.
  enum class Source : uint8_t { None = 0, Vdo, NavSentence } source = Source::None;
};

// Fixed-capacity target store, keyed by MMSI.
//
// Full-table behaviour is deliberate: rather than refusing new targets, the
// stalest entry is replaced. A display that silently stops showing new vessels
// once full would be worse than one that forgets the vessel nobody has heard
// from in ten minutes.
struct AisTargetTable {
  AisTarget entries[MAX_AIS_TARGETS];
  int count = 0;
  uint32_t replacements = 0;  // how often the table has had to evict

  AisTarget* find(uint32_t mmsi) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].mmsi == mmsi) return &entries[i];
    }
    return nullptr;
  }

  AisTarget* findOrAdd(uint32_t mmsi, unsigned long nowMs) {
    if (mmsi == 0) return nullptr;
    if (AisTarget* existing = find(mmsi)) return existing;

    if (count < MAX_AIS_TARGETS) {
      AisTarget* t = &entries[count++];
      *t = AisTarget{};
      t->mmsi = mmsi;
      t->lastSeenMs = nowMs;
      return t;
    }

    // Full: evict whichever target has been silent longest.
    int oldest = 0;
    for (int i = 1; i < count; ++i) {
      if (entries[i].lastSeenMs < entries[oldest].lastSeenMs) oldest = i;
    }
    ++replacements;
    entries[oldest] = AisTarget{};
    entries[oldest].mmsi = mmsi;
    entries[oldest].lastSeenMs = nowMs;
    return &entries[oldest];
  }

  // Targets heard within TARGET_STALE_MS - what the display counts.
  int liveCount(unsigned long nowMs) const {
    int n = 0;
    for (int i = 0; i < count; ++i) {
      if (!entries[i].isStale(nowMs)) ++n;
    }
    return n;
  }

  int positionCount(unsigned long nowMs) const {
    int n = 0;
    for (int i = 0; i < count; ++i) {
      if (entries[i].hasPosition && !entries[i].isStale(nowMs)) ++n;
    }
    return n;
  }

  // Drops entries nobody has heard from in a long while, so the list does not
  // fill with vessels that left hours ago.
  void purgeStale(unsigned long nowMs) {
    int out = 0;
    for (int i = 0; i < count; ++i) {
      if (!entries[i].isStale(nowMs)) {
        if (out != i) entries[out] = entries[i];
        ++out;
      }
    }
    count = out;
  }
};

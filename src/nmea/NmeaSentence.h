#pragma once

#include <cstddef>
#include <cstdint>

#include "NmeaLineReader.h"

// Checksum validation and in-place field splitting for one NMEA sentence.
//
// eNMEA's NmeaParser does this too, but bolted to its own dashboard data model.
// eAIS needs the same framing with a different consumer, so this is the neutral
// half on its own: it hands back the talker, the sentence id and the fields,
// and has no opinion about what they mean.
struct NmeaSentence {
  static constexpr int MAX_FIELDS = 24;

  bool valid = false;          // checksum present and correct
  bool hasAddress = false;     // address field was present and >= 5 chars
  char talker[3] = {0};        // "AI", "GP", ...
  char id[4] = {0};            // "VDM", "RMC", ...
  char* fields[MAX_FIELDS] = {nullptr};
  int fieldCount = 0;

  const char* field(int i) const { return (i >= 0 && i < fieldCount) ? fields[i] : ""; }
  bool has(int i) const { return i >= 0 && i < fieldCount && fields[i][0] != '\0'; }

  // `line` must be mutable and start with '$' or '!' - NULs are written into it
  // while splitting. Values are only trustworthy when `valid` is true.
  static NmeaSentence parse(char* line);
};

#pragma once

#include <cstdint>

#include "AisTypes.h"
#include "nmea/NmeaSentence.h"

// Decodes AIVDM/AIVDO payloads into the target table.
//
// Supported messages, chosen for what a target display actually needs:
//   1, 2, 3   Class A position report - position, SOG, COG, heading, status
//   5         Class A static and voyage data - the vessel NAME (two fragments)
//   18        Class B position report
//   19        Class B extended position report - position AND name
//   24        Class B static data, parts A and B - name and callsign
//
// Everything else is counted and ignored. Notably absent: 21 (aids to
// navigation) and 4 (base stations), which would clutter a small plot more than
// they help; adding them is a branch each if that turns out to be wrong.
//
// The multi-fragment reassembly matters more than it looks: message 5 is where
// vessel names come from and it never fits in one sentence, so a decoder that
// only handles single-fragment messages shows a plot of anonymous MMSIs.
class AisDecoder {
 public:
  struct Result {
    bool handled = false;      // it was a VDM/VDO we understood
    bool positionUpdated = false;
    bool nameUpdated = false;
    bool awaitingFragments = false;  // valid fragment stored, message incomplete
    uint8_t messageType = 0;
    uint32_t mmsi = 0;
  };

  // `sentence` must be a checksum-valid VDM or VDO. `ownShip` is updated
  // instead of the target table when the sentence is a VDO (this vessel's own
  // transponder output) - a boat that plots itself as a target is a bug users
  // notice immediately.
  Result handle(const NmeaSentence& sentence, AisTargetTable& targets, OwnShip& ownShip, unsigned long nowMs);

  // Counters for the diagnostics line: this is a verification tool's sibling,
  // so "how many did I fail to decode" is worth showing rather than hiding.
  uint32_t decodedCount() const { return decoded_; }
  uint32_t unsupportedCount() const { return unsupported_; }
  uint32_t badFragmentCount() const { return badFragments_; }

 private:
  // Payloads are ASCII-armoured 6 bits per character. Message 5 is 424 bits =
  // 71 characters, so 128 covers every message type this decodes with room to
  // spare for a malformed one.
  static constexpr int MAX_PAYLOAD_CHARS = 128;
  // Concurrent multi-fragment messages in flight. Sequence ids run 0-9, but
  // more than a few interleaved on two channels would be unusual.
  static constexpr int MAX_ASSEMBLIES = 6;
  // A partial message older than this is abandoned. Fragments are transmitted
  // back-to-back; anything still waiting seconds later has lost a fragment.
  static constexpr unsigned long FRAGMENT_TIMEOUT_MS = 8000;

  struct Assembly {
    bool active = false;
    char seqId = 0;
    char channel = 0;
    int expected = 0;
    int received = 0;
    int length = 0;
    unsigned long startedMs = 0;
    char payload[MAX_PAYLOAD_CHARS + 1] = {0};
  };

  Assembly assemblies_[MAX_ASSEMBLIES];
  uint32_t decoded_ = 0;
  uint32_t unsupported_ = 0;
  uint32_t badFragments_ = 0;

  Assembly* findAssembly(char seqId, char channel);
  Assembly* claimAssembly(char seqId, char channel, int expected, unsigned long nowMs);
  Result decodePayload(const char* payload, int bitCount, bool isOwnShip, AisTargetTable& targets, OwnShip& ownShip,
                       unsigned long nowMs);
};

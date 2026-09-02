#include "AisDecoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// AIS payloads are "six-bit ASCII": subtract 48, and subtract a further 8 when
// the result exceeds 40, to recover the original six bits.
uint8_t sixBit(char c) {
  uint8_t v = static_cast<uint8_t>(c) - 48;
  if (v > 40) v -= 8;
  return static_cast<uint8_t>(v & 0x3F);
}

// Unsigned big-endian field. `available` guards against reading past a short
// payload - a truncated message must decode to nothing rather than to garbage
// that looks like a position.
bool bitsUnsigned(const char* payload, int available, int start, int len, uint32_t& out) {
  if (start < 0 || len <= 0 || len > 32 || start + len > available) return false;
  uint32_t v = 0;
  for (int i = 0; i < len; ++i) {
    const int bit = start + i;
    const uint8_t six = sixBit(payload[bit / 6]);
    v = (v << 1) | ((six >> (5 - (bit % 6))) & 1u);
  }
  out = v;
  return true;
}

// Two's-complement field. Latitude and longitude are 27 and 28 bits, so the
// sign bit is nowhere near a byte boundary and must be extended by hand.
bool bitsSigned(const char* payload, int available, int start, int len, int32_t& out) {
  uint32_t raw = 0;
  if (!bitsUnsigned(payload, available, start, len, raw)) return false;
  const uint32_t signBit = 1u << (len - 1);
  out = (raw & signBit) ? static_cast<int32_t>(raw | ~(signBit * 2 - 1)) : static_cast<int32_t>(raw);
  return true;
}

// Six-bit ASCII text: 0-31 map to '@'..'_', 32-63 to ' '..'?'. Trailing '@' and
// spaces are padding and are trimmed.
void bitsText(const char* payload, int available, int start, int chars, char* out, size_t outSize) {
  out[0] = '\0';
  size_t written = 0;
  for (int i = 0; i < chars && written + 1 < outSize; ++i) {
    uint32_t v = 0;
    if (!bitsUnsigned(payload, available, start + i * 6, 6, v)) break;
    out[written++] = v < 32 ? static_cast<char>('@' + v) : static_cast<char>(' ' + (v - 32));
  }
  out[written] = '\0';
  while (written > 0 && (out[written - 1] == '@' || out[written - 1] == ' ')) {
    out[--written] = '\0';
  }
}

constexpr double COORD_SCALE = 600000.0;  // 1/10000 minute, per the AIS spec

bool plausibleLat(double d) { return d >= -90.0 && d <= 90.0; }
bool plausibleLon(double d) { return d >= -180.0 && d <= 180.0; }

}  // namespace

AisDecoder::Assembly* AisDecoder::findAssembly(char seqId, char channel) {
  for (Assembly& a : assemblies_) {
    if (a.active && a.seqId == seqId && a.channel == channel) return &a;
  }
  return nullptr;
}

AisDecoder::Assembly* AisDecoder::claimAssembly(char seqId, char channel, int expected, unsigned long nowMs) {
  // Reuse a matching slot, then a free one, then the oldest - a lost fragment
  // must not permanently consume a slot.
  Assembly* slot = findAssembly(seqId, channel);
  if (slot == nullptr) {
    for (Assembly& a : assemblies_) {
      if (!a.active || nowMs - a.startedMs > FRAGMENT_TIMEOUT_MS) { slot = &a; break; }
    }
  }
  if (slot == nullptr) {
    slot = &assemblies_[0];
    for (Assembly& a : assemblies_) {
      if (a.startedMs < slot->startedMs) slot = &a;
    }
  }
  *slot = Assembly{};
  slot->active = true;
  slot->seqId = seqId;
  slot->channel = channel;
  slot->expected = expected;
  slot->startedMs = nowMs;
  return slot;
}

AisDecoder::Result AisDecoder::handle(const NmeaSentence& sentence, AisTargetTable& targets, OwnShip& ownShip,
                                      unsigned long nowMs) {
  Result r;
  // fields: 0=!AIVDM 1=fragCount 2=fragNum 3=seqId 4=channel 5=payload 6=fillBits
  if (!sentence.valid || sentence.fieldCount < 7) return r;

  const bool isOwnShip = std::strncmp(sentence.id, "VDO", 3) == 0;
  if (!isOwnShip && std::strncmp(sentence.id, "VDM", 3) != 0) return r;

  const int fragCount = sentence.field(1)[0] ? std::atoi(sentence.field(1)) : 1;
  const int fragNum = sentence.field(2)[0] ? std::atoi(sentence.field(2)) : 1;
  const char seqId = sentence.field(3)[0] ? sentence.field(3)[0] : '-';
  const char channel = sentence.field(4)[0] ? sentence.field(4)[0] : 'A';
  const char* payload = sentence.field(5);
  const int fillBits = sentence.field(6)[0] ? std::atoi(sentence.field(6)) : 0;
  const int payloadLen = static_cast<int>(std::strlen(payload));

  if (fragCount < 1 || fragNum < 1 || fragNum > fragCount || payloadLen == 0 || payloadLen > MAX_PAYLOAD_CHARS) {
    ++badFragments_;
    return r;
  }

  if (fragCount == 1) {
    return decodePayload(payload, payloadLen * 6 - fillBits, isOwnShip, targets, ownShip, nowMs);
  }

  // Multi-fragment. Fragment 1 starts a new assembly; later fragments must
  // arrive in order against a live one, or the whole message is discarded -
  // stitching a fragment onto the wrong message would produce a confident,
  // wrong result, which is worse than dropping it.
  Assembly* slot = (fragNum == 1) ? claimAssembly(seqId, channel, fragCount, nowMs) : findAssembly(seqId, channel);
  if (slot == nullptr || slot->expected != fragCount || slot->received != fragNum - 1 ||
      slot->length + payloadLen > MAX_PAYLOAD_CHARS || nowMs - slot->startedMs > FRAGMENT_TIMEOUT_MS) {
    if (slot != nullptr) slot->active = false;
    ++badFragments_;
    return r;
  }

  std::memcpy(slot->payload + slot->length, payload, static_cast<size_t>(payloadLen));
  slot->length += payloadLen;
  slot->payload[slot->length] = '\0';
  slot->received = fragNum;

  if (fragNum < fragCount) {
    r.handled = true;
    r.awaitingFragments = true;
    return r;
  }

  slot->active = false;
  return decodePayload(slot->payload, slot->length * 6 - fillBits, isOwnShip, targets, ownShip, nowMs);
}

AisDecoder::Result AisDecoder::decodePayload(const char* payload, int bits, bool isOwnShip, AisTargetTable& targets,
                                             OwnShip& ownShip, unsigned long nowMs) {
  Result r;
  uint32_t type = 0, mmsi = 0;
  if (!bitsUnsigned(payload, bits, 0, 6, type) || !bitsUnsigned(payload, bits, 8, 30, mmsi)) {
    ++badFragments_;
    return r;
  }
  r.messageType = static_cast<uint8_t>(type);
  r.mmsi = mmsi;

  // Position fields sit at different offsets for Class A (1/2/3) and Class B
  // (18/19), which is the only structural difference that matters here.
  int sogAt = -1, lonAt = -1, latAt = -1, cogAt = -1, hdgAt = -1, nameAt = -1, nameChars = 20;
  AisClass cls = AisClass::Unknown;

  switch (type) {
    case 1: case 2: case 3:
      sogAt = 50; lonAt = 61; latAt = 89; cogAt = 116; hdgAt = 128;
      cls = AisClass::A;
      break;
    case 18:
      sogAt = 46; lonAt = 57; latAt = 85; cogAt = 112; hdgAt = 124;
      cls = AisClass::B;
      break;
    case 19:  // extended Class B: same position block as 18, plus a name
      sogAt = 46; lonAt = 57; latAt = 85; cogAt = 112; hdgAt = 124; nameAt = 143;
      cls = AisClass::B;
      break;
    case 5:   // Class A static and voyage data - this is where names come from
      nameAt = 112; cls = AisClass::A;
      break;
    case 24:  // Class B static, part A carries the name
      cls = AisClass::B;
      break;
    default:
      ++unsupported_;
      r.handled = true;
      return r;
  }

  r.handled = true;
  ++decoded_;

  // Own-ship reports update the vessel, never the target list.
  if (isOwnShip) {
    ownShip.mmsi = mmsi;
    if (latAt >= 0) {
      int32_t rawLat = 0, rawLon = 0;
      if (bitsSigned(payload, bits, latAt, 27, rawLat) && bitsSigned(payload, bits, lonAt, 28, rawLon)) {
        const double lat = rawLat / COORD_SCALE, lon = rawLon / COORD_SCALE;
        if (plausibleLat(lat) && plausibleLon(lon)) {
          ownShip.latDeg = lat;
          ownShip.lonDeg = lon;
          ownShip.hasPosition = true;
          ownShip.lastPositionMs = nowMs;
          ownShip.source = OwnShip::Source::Vdo;
          r.positionUpdated = true;
        }
      }
      uint32_t sog = 0, cog = 0, hdg = 0;
      if (bitsUnsigned(payload, bits, sogAt, 10, sog) && sog != 1023) ownShip.sogKnots = sog / 10.0f;
      if (bitsUnsigned(payload, bits, cogAt, 12, cog) && cog != 3600) ownShip.cogDeg = cog / 10.0f;
      if (bitsUnsigned(payload, bits, hdgAt, 9, hdg)) ownShip.headingDeg = (hdg == 511) ? -1.0f : hdg;
    }
    return r;
  }

  AisTarget* t = targets.findOrAdd(mmsi, nowMs);
  if (t == nullptr) return r;
  t->lastSeenMs = nowMs;
  if (t->cls == AisClass::Unknown) t->cls = cls;

  if (latAt >= 0) {
    int32_t rawLat = 0, rawLon = 0;
    if (bitsSigned(payload, bits, latAt, 27, rawLat) && bitsSigned(payload, bits, lonAt, 28, rawLon)) {
      const double lat = rawLat / COORD_SCALE, lon = rawLon / COORD_SCALE;
      // 91/181 are the spec's "not available" values and decode to exactly
      // those; anything outside the valid range is discarded rather than
      // plotted at the edge of the world.
      if (plausibleLat(lat) && plausibleLon(lon)) {
        t->latDeg = lat;
        t->lonDeg = lon;
        t->hasPosition = true;
        t->lastPositionMs = nowMs;
        r.positionUpdated = true;
      }
    }
    uint32_t sog = 0, cog = 0, hdg = 0, status = 0;
    if (bitsUnsigned(payload, bits, sogAt, 10, sog) && sog != 1023) t->sogKnots = sog / 10.0f;
    if (bitsUnsigned(payload, bits, cogAt, 12, cog) && cog != 3600) t->cogDeg = cog / 10.0f;
    if (bitsUnsigned(payload, bits, hdgAt, 9, hdg)) t->headingDeg = (hdg == 511) ? -1.0f : hdg;
    if ((type == 1 || type == 2 || type == 3) && bitsUnsigned(payload, bits, 38, 4, status)) {
      t->navStatus = static_cast<uint8_t>(status);
    }
  }

  // Type 24 splits its static data across two messages; only part A has the
  // name, and part B is where the callsign lives.
  if (type == 24) {
    uint32_t part = 0;
    if (bitsUnsigned(payload, bits, 38, 2, part)) {
      if (part == 0) {
        nameAt = 40;
        nameChars = 20;
      } else {
        char callsign[8];
        bitsText(payload, bits, 90, 7, callsign, sizeof(callsign));
        if (callsign[0] != '\0') std::snprintf(t->callsign, sizeof(t->callsign), "%s", callsign);
      }
    }
  }

  if (nameAt >= 0) {
    char name[21];
    bitsText(payload, bits, nameAt, nameChars, name, sizeof(name));
    if (name[0] != '\0') {
      std::snprintf(t->name, sizeof(t->name), "%s", name);
      r.nameUpdated = true;
    }
    if (type == 5) {
      char callsign[8];
      bitsText(payload, bits, 70, 7, callsign, sizeof(callsign));
      if (callsign[0] != '\0') std::snprintf(t->callsign, sizeof(t->callsign), "%s", callsign);
    }
  }

  return r;
}

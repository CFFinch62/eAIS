#include "NmeaSentence.h"

#include <cstring>

namespace {

bool hexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') { out = static_cast<uint8_t>(c - '0'); return true; }
  if (c >= 'A' && c <= 'F') { out = static_cast<uint8_t>(c - 'A' + 10); return true; }
  if (c >= 'a' && c <= 'f') { out = static_cast<uint8_t>(c - 'a' + 10); return true; }
  return false;
}

}  // namespace

NmeaSentence NmeaSentence::parse(char* line) {
  NmeaSentence s;
  if (line == nullptr || (line[0] != '$' && line[0] != '!')) return s;

  char* body = line + 1;
  char* star = std::strchr(body, '*');
  if (star != nullptr) {
    uint8_t computed = 0;
    for (const char* p = body; p < star; ++p) computed ^= static_cast<uint8_t>(*p);
    uint8_t hi = 0, lo = 0;
    if (star[1] != '\0' && star[2] != '\0' && hexNibble(star[1], hi) && hexNibble(star[2], lo)) {
      s.valid = computed == static_cast<uint8_t>((hi << 4) | lo);
    }
    *star = '\0';  // terminate before splitting so the checksum isn't a field
  }

  s.fields[s.fieldCount++] = body;
  for (char* p = body; *p != '\0' && s.fieldCount < MAX_FIELDS; ++p) {
    if (*p == ',') {
      *p = '\0';
      s.fields[s.fieldCount++] = p + 1;
    }
  }

  const size_t addrLen = std::strlen(s.fields[0]);
  if (addrLen >= 5) {
    s.hasAddress = true;
    s.talker[0] = s.fields[0][0];
    s.talker[1] = s.fields[0][1];
    s.id[0] = s.fields[0][addrLen - 3];
    s.id[1] = s.fields[0][addrLen - 2];
    s.id[2] = s.fields[0][addrLen - 1];
  }
  return s;
}

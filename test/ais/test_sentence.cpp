#include <cstring>

#include "test_support.h"

// Checksum and field splitting. Shared shape with eNMEA's parser, but this one
// has no opinion about meaning, so the tests are about framing only.

void runSentenceTests() {
  beginSection("NmeaSentence - valid AIS sentence");
  {
    char buf[NMEA_MAX_SENTENCE_LEN + 1];
    std::snprintf(buf, sizeof(buf), "%s", "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5C");
    const NmeaSentence s = NmeaSentence::parse(buf);
    CHECK(s.valid);
    CHECK(s.hasAddress);
    CHECK_STR(s.talker, "AI");
    CHECK_STR(s.id, "VDM");
    CHECK(s.fieldCount == 7);
    CHECK_STR(s.field(1), "1");
    CHECK_STR(s.field(4), "B");
    CHECK_STR(s.field(6), "0");
    // The checksum must not survive into the last field.
    CHECK(std::strchr(s.field(6), '*') == nullptr);
  }

  beginSection("NmeaSentence - empty fields are preserved positionally");
  {
    // The AIS sequence-id field is routinely empty, and dropping it would shift
    // every field after it - putting the payload where the channel belongs.
    char buf[NMEA_MAX_SENTENCE_LEN + 1];
    std::snprintf(buf, sizeof(buf), "%s", "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C");
    const NmeaSentence s = NmeaSentence::parse(buf);
    CHECK(s.fieldCount == 7);
    CHECK_STR(s.field(3), "");
    CHECK(!s.has(3));
    CHECK_STR(s.field(4), "B");
    CHECK(s.has(5));
  }

  beginSection("NmeaSentence - rejects what it should");
  {
    char buf[NMEA_MAX_SENTENCE_LEN + 1];
    std::snprintf(buf, sizeof(buf), "%s", "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5D");
    CHECK(!NmeaSentence::parse(buf).valid);  // one bit off

    std::snprintf(buf, sizeof(buf), "%s", "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0");
    const NmeaSentence none = NmeaSentence::parse(buf);
    CHECK(!none.valid);  // absent is not the same as correct
    CHECK(none.hasAddress);

    std::snprintf(buf, sizeof(buf), "%s", "rubbish");
    CHECK(!NmeaSentence::parse(buf).hasAddress);
  }

  beginSection("NmeaSentence - nav sentences still parse (own-ship fallback)");
  {
    // Some of the AIS units this targets send RMC/GGA alongside VDM, and that
    // is the only own-ship position available when they do not send VDO.
    char buf[NMEA_MAX_SENTENCE_LEN + 1];
    std::snprintf(buf, sizeof(buf), "%s",
                  "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A");
    const NmeaSentence s = NmeaSentence::parse(buf);
    CHECK(s.valid);
    CHECK_STR(s.talker, "GP");
    CHECK_STR(s.id, "RMC");
    CHECK_STR(s.field(2), "A");
  }
}

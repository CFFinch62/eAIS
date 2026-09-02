#include "test_support.h"

// AIS payload decoding.
//
// Expected values come from an independent decoder written from the AIS
// specification (see scripts/ais_vectors.py), never from this code. Where a
// message needed particular contents - a known vessel name, a multi-fragment
// type 5 - the sentence was synthesised so the answer is known exactly rather
// than inferred from a sample of unknown provenance.

namespace {

// Real, widely-published Class A position report. Independently decoded as:
// type 1, MMSI 366053209, nav status 3, SOG 0.0, COG 219.3, heading 1,
// lat 37.80211833 N, lon -122.34161833 E.
constexpr const char* VDM_TYPE1 = "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5C";

// Synthesised: MMSI 235009802, name "TEST VESSEL ONE", callsign "TSTVSL".
constexpr const char* VDM_TYPE5_F1 =
    "!AIVDM,2,1,0,A,53P7o2P2;HNMA=AI<h1@E=B1HE=<Dj0tpD00001600000000000000000000,0*34";
constexpr const char* VDM_TYPE5_F2 = "!AIVDM,2,2,0,A,00000000000,0*26";

// Synthesised Class B: MMSI 338123456 at 37.5 N, -122.5 E, 12.3 kn, COG 87.6.
constexpr const char* VDM_TYPE18 = "!AIVDM,1,1,,A,B52MJh00Nmkkm@5GDb0nkwP00000,0*74";
// Synthesised type 24 part A for the same MMSI: name "LITTLE BOAT".
constexpr const char* VDM_TYPE24A = "!AIVDM,1,1,,A,H52MJh0hUA@hF08t5@0000000000,0*2D";
// Synthesised own-ship report: MMSI 366999123 at 37.9 N, -122.4 E.
constexpr const char* VDO_TYPE1 = "!AIVDO,1,1,,A,15MwnDhP10G?d`0Ect8=Nbn00000,0*60";

}  // namespace

void runDecoderTests() {
  beginSection("Type 1 - Class A position report");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    const AisDecoder::Result r = feed(d, VDM_TYPE1, targets, own, 100000);
    CHECK(r.handled);
    CHECK(r.positionUpdated);
    CHECK(r.messageType == 1);
    CHECK(r.mmsi == 366053209u);
    CHECK(targets.count == 1);

    const AisTarget* t = targets.find(366053209u);
    CHECK(t != nullptr);
    if (t != nullptr) {
      CHECK(t->hasPosition);
      // Sign matters more than precision here: a longitude that loses its sign
      // puts a San Francisco vessel in the Mediterranean, which looks entirely
      // plausible on a plot with no coastline.
      CHECK_NEAR(t->latDeg, 37.80211833, 1e-6);
      CHECK_NEAR(t->lonDeg, -122.34161833, 1e-6);
      CHECK_NEAR(t->sogKnots, 0.0, 1e-3);
      CHECK_NEAR(t->cogDeg, 219.3, 1e-3);
      CHECK(t->navStatus == 3);
      CHECK(t->cls == AisClass::A);
      CHECK(!t->hasName());  // a position report carries no name
    }
    // Own ship must be untouched by another vessel's report.
    CHECK(!own.hasPosition);
  }

  beginSection("Type 5 - multi-fragment, this is where names come from");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;

    // Fragment 1 alone must produce no target: acting on half a message is how
    // a decoder invents vessels.
    const AisDecoder::Result r1 = feed(d, VDM_TYPE5_F1, targets, own, 100000);
    CHECK(r1.handled);
    CHECK(r1.awaitingFragments);
    CHECK(!r1.nameUpdated);
    CHECK(targets.count == 0);

    const AisDecoder::Result r2 = feed(d, VDM_TYPE5_F2, targets, own, 100100);
    CHECK(r2.handled);
    CHECK(!r2.awaitingFragments);
    CHECK(r2.messageType == 5);
    CHECK(r2.nameUpdated);
    CHECK(targets.count == 1);

    const AisTarget* t = targets.find(235009802u);
    CHECK(t != nullptr);
    if (t != nullptr) {
      CHECK_STR(t->name, "TEST VESSEL ONE");
      CHECK_STR(t->callsign, "TSTVSL");
      CHECK(!t->hasPosition);  // type 5 is static data only
    }
  }

  beginSection("Type 5 - a lost fragment is dropped, not stitched");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    feed(d, VDM_TYPE5_F1, targets, own, 100000);
    // Second fragment of a *different* message arriving on the same sequence
    // slot. Concatenating it would decode confident nonsense.
    const AisDecoder::Result bad = feed(d, "!AIVDM,3,3,0,A,00000000000,0*26", targets, own, 100100);
    CHECK(!bad.handled);
    CHECK(targets.count == 0);
    CHECK(d.badFragmentCount() > 0);
  }
  {
    // A fragment arriving long after its partner is abandoned rather than
    // joined to whatever came before.
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    feed(d, VDM_TYPE5_F1, targets, own, 100000);
    feed(d, VDM_TYPE5_F2, targets, own, 100000 + 60000);
    CHECK(targets.count == 0);
  }

  beginSection("Type 18 / 24 - Class B position and name");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    const AisDecoder::Result pos = feed(d, VDM_TYPE18, targets, own, 100000);
    CHECK(pos.handled);
    CHECK(pos.positionUpdated);
    CHECK(pos.messageType == 18);

    const AisTarget* t = targets.find(338123456u);
    CHECK(t != nullptr);
    if (t != nullptr) {
      CHECK(t->cls == AisClass::B);
      CHECK_NEAR(t->latDeg, 37.5, 1e-6);
      CHECK_NEAR(t->lonDeg, -122.5, 1e-6);
      CHECK_NEAR(t->sogKnots, 12.3, 1e-3);
      CHECK_NEAR(t->cogDeg, 87.6, 1e-3);
      // 511 means "not available" and must not be plotted as heading 511.
      CHECK(t->headingDeg < 0.0f);
    }

    // The name arrives separately, in a type 24, and must merge into the same
    // target rather than creating a second one.
    const AisDecoder::Result name = feed(d, VDM_TYPE24A, targets, own, 100200);
    CHECK(name.handled);
    CHECK(name.nameUpdated);
    CHECK(targets.count == 1);
    const AisTarget* merged = targets.find(338123456u);
    CHECK(merged != nullptr);
    if (merged != nullptr) {
      CHECK_STR(merged->name, "LITTLE BOAT");
      CHECK(merged->hasPosition);  // the earlier position survives the merge
      CHECK_NEAR(merged->latDeg, 37.5, 1e-6);
    }
  }

  beginSection("VDO - own ship, never a target");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    const AisDecoder::Result r = feed(d, VDO_TYPE1, targets, own, 100000);
    CHECK(r.handled);
    // A vessel that plots itself as a target is the bug users notice first.
    CHECK(targets.count == 0);
    CHECK(own.hasPosition);
    CHECK(own.mmsi == 366999123u);
    CHECK_NEAR(own.latDeg, 37.9, 1e-5);
    CHECK_NEAR(own.lonDeg, -122.4, 1e-5);
    CHECK_NEAR(own.sogKnots, 6.4, 1e-3);
    CHECK_NEAR(own.cogDeg, 345.0, 1e-3);
    CHECK_NEAR(own.headingDeg, 347.0, 1e-3);
    CHECK(own.source == OwnShip::Source::Vdo);
  }

  beginSection("Malformed input decodes to nothing");
  {
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    // Bad checksum: the payload may be corrupt, so nothing in it is trustworthy.
    CHECK(!feed(d, "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5D", targets, own, 1000).handled);
    // Truncated payload: reading fields past the end must fail, not return
    // whatever happens to follow in memory.
    feed(d, "!AIVDM,1,1,,B,15M6,0*5A", targets, own, 1000);
    // Not an AIS sentence at all.
    CHECK(!feed(d, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", targets, own, 1000).handled);
    CHECK(targets.count == 0);
  }
  {
    // An unsupported but well-formed message is counted, not treated as an
    // error - knowing the feed carries type 21 aids to navigation is useful
    // even though they are not plotted.
    AisDecoder d;
    AisTargetTable targets;
    OwnShip own;
    const AisDecoder::Result r = feed(d, "!AIVDM,1,1,,A,E52MJh00000000000000000000000,0*3B", targets, own, 1000);
    CHECK(r.handled);
    CHECK(d.unsupportedCount() == 1);
    CHECK(targets.count == 0);
  }
}

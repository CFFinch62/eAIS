#include "test_support.h"

// Target table behaviour. Pure data handling, and the place where a display
// quietly stops telling the truth if the edges are wrong.

void runTargetTableTests() {
  beginSection("Target table - add, find, dedupe");
  {
    AisTargetTable table;
    CHECK(table.findOrAdd(0, 1000) == nullptr);  // MMSI 0 is not a vessel
    CHECK(table.count == 0);

    AisTarget* a = table.findOrAdd(111111111u, 1000);
    CHECK(a != nullptr);
    CHECK(table.count == 1);
    // A second report from the same vessel updates it rather than adding a row.
    CHECK(table.findOrAdd(111111111u, 2000) == a);
    CHECK(table.count == 1);
  }

  beginSection("Target table - staleness windows");
  {
    AisTargetTable table;
    AisTarget* t = table.findOrAdd(222222222u, 100000);
    t->lastPositionMs = 100000;
    CHECK(!t->isStale(100000));
    CHECK(!t->isAging(100000));
    // Aging first: still listed, but the position is no longer fresh.
    CHECK(t->isAging(100000 + TARGET_AGING_MS + 1));
    CHECK(!t->isStale(100000 + TARGET_AGING_MS + 1));
    // Then stale. The boundary is inclusive, so check both sides of it - a
    // Class B at anchor reports only every 3 minutes and must not vanish.
    CHECK(!t->isStale(100000 + TARGET_STALE_MS));
    CHECK(t->isStale(100000 + TARGET_STALE_MS + 1));
  }

  beginSection("Target table - counts reflect what is really there");
  {
    AisTargetTable table;
    table.findOrAdd(1u, 100000)->hasPosition = true;
    table.findOrAdd(2u, 100000);                      // heard, but no position yet
    table.findOrAdd(3u, 100000)->hasPosition = true;
    CHECK(table.liveCount(100000) == 3);
    // The plot can only draw the ones with positions; the list shows all three.
    CHECK(table.positionCount(100000) == 2);
    CHECK(table.liveCount(100000 + TARGET_STALE_MS + 1) == 0);
  }

  beginSection("Target table - purge drops only the stale");
  {
    AisTargetTable table;
    table.findOrAdd(1u, 0);
    table.findOrAdd(2u, 300000);
    table.findOrAdd(3u, 300000);
    table.purgeStale(300000 + TARGET_STALE_MS - 1);
    CHECK(table.count == 2);
    CHECK(table.find(1u) == nullptr);
    CHECK(table.find(2u) != nullptr);
    CHECK(table.find(3u) != nullptr);
  }

  beginSection("Target table - a full table evicts the stalest, not the newest");
  {
    // Refusing new targets once full would make a busy anchorage hide exactly
    // the vessel that just appeared. Evicting whoever has been silent longest
    // keeps the display honest about what is nearby now.
    AisTargetTable table;
    for (int i = 0; i < MAX_AIS_TARGETS; ++i) {
      table.findOrAdd(static_cast<uint32_t>(1000 + i), static_cast<unsigned long>(10000 + i));
    }
    CHECK(table.count == MAX_AIS_TARGETS);
    CHECK(table.replacements == 0);

    AisTarget* fresh = table.findOrAdd(999999u, 500000);
    CHECK(fresh != nullptr);
    CHECK(table.count == MAX_AIS_TARGETS);
    CHECK(table.replacements == 1);
    CHECK(table.find(999999u) != nullptr);
    CHECK(table.find(1000u) == nullptr);   // the oldest went
    CHECK(table.find(1001u) != nullptr);   // the next oldest did not
  }
}

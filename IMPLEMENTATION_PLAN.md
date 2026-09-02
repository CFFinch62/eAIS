# eAIS Implementation Plan

Read this and `README.md` before touching code. Written for whoever picks this
up cold, including me.

## State

Scaffolded and building; **nothing has run on hardware yet.** The decoder is
covered by 114 host-side tests; the views have never been seen on a panel.

- `pio run -e x3` builds clean (16.2% RAM, 17.2% flash)
- `test/run_tests.sh` passes — sentence framing, AIS decoding, target table
- `scripts/check_shared.py` reports 17 files identical to eNMEA's

## What exists

| Area | State |
| --- | --- |
| AIS decode: types 1/2/3, 5, 18/19, 24 | Done, tested |
| Multi-fragment reassembly (type 5 names) | Done, tested |
| Target table, staleness, eviction | Done, tested |
| Own ship from VDO, fallback to RMC/GGA/GLL | Written, untested on real data |
| LIST view | Written, never rendered |
| PLOT view: rings, own ship, symbols, range | Written, never rendered |
| Network / settings / power / canvas | Copied from eNMEA, hardware-proven there |
| Test server with a moving fleet | Done, output validated |

## Do this first, in order

1. **Flash it and look at the screen.** Everything in `ui/` is unverified. Run
   `scripts/ais_test_server.py`, point a profile at it, and check the list is
   legible and the plot's geometry is right — targets where the bearings say
   they should be, symbols pointing along COG.

2. **Watch for e-ink ghosting on the plot.** This is the biggest open risk and
   the reason the plot might need rethinking. eNMEA's dashboard redraws numbers
   in fixed boxes; here symbols *move across* the panel, and fast partial
   refresh leaves trails. `FULL_REPAINT_EVERY_N` is set to 15 (30 s) as a
   guess. If trails are objectionable, the options are a full refresh every
   frame (which flashes), or drawing only within a cleared plot area. Decide
   with your eyes, not from here.

3. **Check the column layout against real names.** AIS names are up to 20
   characters; the list allots 16 at scale 2. Real traffic will show whether
   truncation is acceptable or the layout needs to change.

## Known gaps, deliberate

- **No CPA/TCPA, no trails, no guard zones.** See README — not honest at this
  refresh rate.
- **Types 4 (base stations) and 21 (aids to navigation) are counted, not
  plotted.** They would crowd a small screen. `unsupportedCount()` is exposed
  so a decision can be based on what real feeds actually carry.
- **North-up only.** Course-up needs own heading to be reliable, and adds a
  rotation to every symbol.
- **The plot ignores targets beyond range** but says how many (`N BEYOND x NM`),
  so an empty plot is never confusable with an empty sea.

## Notes worth not rediscovering

- **Space is value 32 in AIS six-bit ASCII, not 0.** An earlier version of
  `scripts/ais_vectors.py` got this wrong, and the resulting test vectors
  encoded spaces as `@`. The C++ decoder was right and the test was wrong —
  which is the correct way round, but only because the vectors were generated
  independently. Keep it that way.
- **`.replace("AIVDO", "AIVDM")` on a finished sentence corrupts it.** The body
  changes and the checksum does not. `ais_vectors.type1()` takes a `kind`
  argument for this reason.
- **Fragment 1 of a type 5 must produce nothing.** Acting on half a message is
  how a decoder invents vessels; the tests assert it.
- **VDO is own ship and must never become a target.** A boat that plots itself
  is the bug users notice first.

## Shared code

Copied from eNMEA, not linked — see README. The rule: fix in eNMEA first, then
re-copy, and let `check_shared.py` catch what was forgotten. If a third 'e' app
appears, that is the moment to extract a real shared core rather than maintain
three copies.

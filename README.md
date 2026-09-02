# eAIS

AIS target list and plot for Xteink X3/X4 hardware (ESP32-C3; 792x528 e-ink on
the X3, 800x480 on the X4), fed over Wi-Fi from an AIS receiver or transponder.

Sibling of [eNMEA](../eNMEA). Where that answers *"is this NMEA feed alive and
correct?"*, eAIS answers *"what is out there?"* — a target list of
name/MMSI/speed/course, and a north-up plot of where they are relative to you.

## What it does

- Connects to an AIS unit's Wi-Fi output over UDP or TCP, using the same
  8-profile settings system as eNMEA (bench work means switching between units
  constantly).
- Decodes AIS messages **1, 2, 3** (Class A position), **5** (Class A static —
  the vessel name, spread across two sentences), **18/19** (Class B position)
  and **24** (Class B name).
- Tracks up to 128 targets with name, MMSI, position, SOG, COG and heading.
- **LIST** view: every target heard, with range and bearing when own position
  is known. Works with no own position at all.
- **PLOT** view: north-up, own ship centred, range rings at 0.5–16 nm.
- Own-ship position from **VDO**, falling back to **RMC/GGA/GLL** — because
  these units can be configured either way.

## What it is not

Not a collision-avoidance display. A 1-bit panel refreshing every two seconds
is a situational sketch: no trails, no CPA/TCPA, no guard zones. Anything
implying that accuracy at this refresh rate would be dishonest.

## Build, flash, test

```sh
git clone https://github.com/Free-Ink/freeink-sdk.git   # next to platformio.ini
git -C freeink-sdk checkout fad70f28a982c978737410e535a4f7276ce28c19
pio run -e x3 -t upload
test/run_tests.sh          # host-side decoder tests, ~1 second, no hardware
```

Exercise it without an AIS unit — a fleet of moving targets, names arriving
later than positions, and an own-ship report:

```sh
python3 scripts/ais_test_server.py                # TCP
python3 scripts/ais_test_server.py --proto udp    # UDP broadcast
```

## Buttons

| Button | Does |
| --- | --- |
| **CONFIRM** | Switch between LIST and PLOT |
| **UP / DOWN** | Scroll the list, or change plot range |
| **LEFT / RIGHT** | Step through saved source profiles |
| **BACK** (hold 3 s) | Forget the profile in use |
| **POWER** (hold 2 s) | Shut down |

## Shared code with eNMEA

The network, settings, power and display layers are **copied** from eNMEA, each
file carrying a header naming its origin. Those files hold fixes that were
expensive to find on hardware — e-ink double buffering, an lwIP socket leak on
TCP retry, a SoftAP subnet that collided with marine gateways. Fix them in
eNMEA first, then re-copy.

```sh
python3 scripts/check_shared.py           # what has drifted
python3 scripts/check_shared.py --diff    # and how
```

Two files diverge on purpose and are excluded from that check: `NmeaSource`
(hands sentences to a callback rather than parsing into eNMEA's dashboard
model) and `ui/Draw.*` (plot primitives eNMEA has no use for).

## Licence

MIT — see `LICENSE`.

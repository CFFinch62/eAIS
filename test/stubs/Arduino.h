// SHARED WITH eNMEA - copied from eNMEA/test/stubs/Arduino.h at f2c1fb5.
//
// Edit the eNMEA copy first when fixing something that affects both, then
// re-copy. `scripts/check_shared.py` reports when the two have drifted.
// These files carry hardware-found fixes (e-ink double buffering, the lwIP
// socket leak on TCP retry, the SoftAP subnet collision with marine
// gateways) that are expensive to rediscover - do not casually diverge.
#pragma once

// Host-side stand-in for the Arduino core header, so src/nmea/*.cpp compiles
// with a plain g++ and no ESP32 toolchain.
//
// NmeaParser.cpp is the only file under test that includes <Arduino.h>, and
// the only thing it uses from it is millis() - it timestamps each field as it
// lands. Everything else in the parser is standard C++. That is precisely why
// the parser is testable on the host at all, and it is worth keeping that way:
// if a future change pulls String, Serial or anything else Arduino-shaped into
// NmeaParser.cpp or NmeaLineReader.cpp, this stub stops being enough and the
// cheapest tests in the project stop working.
//
// The definition lives in test_support.cpp and returns g_fakeMillis, which
// tests set directly so ages and staleness are deterministic.

unsigned long millis();

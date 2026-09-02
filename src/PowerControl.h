// SHARED WITH eNMEA - copied from eNMEA/src/PowerControl.h at f2c1fb5.
//
// Edit the eNMEA copy first when fixing something that affects both, then
// re-copy. `scripts/check_shared.py` reports when the two have drifted.
// These files carry hardware-found fixes (e-ink double buffering, the lwIP
// socket leak on TCP retry, the SoftAP subnet collision with marine
// gateways) that are expensive to rediscover - do not casually diverge.
#pragma once

class EinkCanvas;

// Software power-off: leaves a "POWERED OFF" image on the panel (e-ink holds
// it with no power), puts the panel controller to sleep, releases the battery
// latch and enters deep sleep armed to wake on the power button.
//
// Does not return - the chip resets when the user presses power again, so wake
// comes back through setup() as a normal cold boot.
[[noreturn]] void powerOff(EinkCanvas& canvas);

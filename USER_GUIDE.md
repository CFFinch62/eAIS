# eAIS User Guide

**Xteink X3 · AIS target list and plot over Wi-Fi**

eAIS turns an Xteink X3 into a small AIS display: a list of the vessels around
you, and a plot of where they are. It connects to an AIS receiver or
transponder over Wi-Fi — no chartplotter, no laptop, no phone app.

It is a **situational display, not a collision-avoidance instrument.** A 1-bit
screen that refreshes every two seconds is a sketch of what's around you. There
are no trails, no CPA/TCPA, no guard zones, and nothing here should be used to
decide whether you are standing into danger.

---

## What you need

- The Xteink X3, charged (or on USB)
- A 2.4 GHz Wi-Fi network — the ESP32-C3 has **no 5 GHz radio**
- An AIS unit that outputs over Wi-Fi, either as its own access point or onto
  your boat's network

---

## First-time setup

The device has no keyboard. You configure it from a phone, through a Wi-Fi
access point it hosts itself.

1. **Power on.** The screen shows `SETUP MODE` and the device starts an open
   network called **`eAIS-Setup`**.
2. **Join `eAIS-Setup`** from your phone. Ignore the "no internet" warning.
3. **Browse to `http://192.168.7.1/`**
4. **Fill in a profile** — see the table below.
5. **Press "Save & use now".** The device restarts and connects.

| Field | What to enter |
| --- | --- |
| **Name** | Whatever you'll recognise: `AIS-100 BENCH`, `HELM`, `KC-2W` |
| **Wi-Fi SSID** | Your AIS unit's access point, or the boat's network |
| **Wi-Fi Password** | Blank for an open network — most units are open |
| **Protocol** | `TCP` if the unit is a server; `UDP` if it broadcasts |
| **NMEA Source Host** | TCP only: the unit's IP, often `192.168.4.1` |
| **Port** | Whatever the unit documents; `10110` is common |

> **The setup page stays available the whole time**, both on your network and
> over `eAIS-Setup`. You are never locked out by a wrong setting.

---

## Profiles — for more than one unit

eAIS stores **eight** complete configurations and switches between them from
the device's buttons. Set them up once, then **LEFT/RIGHT** steps through them
and reconnects — no phone needed.

Switching clears the target list. Carrying one unit's vessels over to the next
would show traffic the new unit has never reported.

---

## The list

![The eAIS list view on an Xteink X3, showing five vessels with name, MMSI,
speed, course, range and bearing.](docs/images/list-view.jpg)

Every vessel heard, most recent first. Press **CONFIRM** to switch to the plot.

| Column | Means |
| --- | --- |
| **NAME** | The vessel's name. `(NO NAME)` until its name arrives — see below |
| **MMSI** | Its unique nine-digit identity |
| **SOG** | Speed over ground, knots |
| **COG** | Course over ground, degrees true |
| **RNG BRG** | Range in nautical miles and bearing in degrees, from you. Only shown when your own position is known |

The top line reads `TGTS 5/5` — vessels with a **position** out of vessels
**heard**. Those differ when a unit has been heard but hasn't reported where it
is yet.

### Why names appear late

Vessels transmit their **position** every few seconds but their **name** only
every few minutes. A target showing `(NO NAME)` with an MMSI is normal and
correct — the name fills in when the vessel next broadcasts it. On a cold start
expect a couple of minutes before the list is fully named.

---

## The plot

![The eAIS plot view: range rings with N/E/S/W bearing marks, own vessel at the
centre, and three target symbols labelled with name, bearing and
range.](docs/images/plot-view.jpg)

North-up, your vessel at the centre. **UP/DOWN** changes the range;
**CONFIRM** returns to the list.

- **Range rings** at half and full scale, labelled in nautical miles
- **Bearing marks** every 30°, longer at N/E/S/W
- **Targets** point along their course. A **solid** centre means a fresh
  position; **hollow** means it hasn't updated in a minute
- Each target is labelled with its name and, when the plot isn't busy, its
  **bearing/range** — so you can check the picture against the numbers
- The bottom line reports anything outside the ring: `3 SHOWN 2 BEYOND 2.0 NM`.
  **An empty-looking plot never means an empty sea** — widen the range

### "NO OWN POSITION — CANNOT PLOT"

The plot needs to know where *you* are. That comes from your own transponder
(an AIVDO sentence) or from GPS data (RMC/GGA/GLL). Many AIS receivers are
configured to send target data only, in which case there is nothing to centre
the plot on.

**The list still works.** Check your unit's output settings if you want the
plot. The top line shows the source: `OWN:VDO`, `OWN:GPS` or `OWN:NONE`.

---

## Buttons

| Button | Does |
| --- | --- |
| **CONFIRM** | Switch between list and plot |
| **UP / DOWN** | Scroll the list, or change the plot range |
| **LEFT / RIGHT** | Step through saved profiles |
| **BACK** (hold 3 s) | Forget the profile in use |
| **POWER** (hold 2 s) | Shut down; press once to start again |

---

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `LISTENING` and no targets | You chose UDP but the unit expects TCP. Change the protocol on the setup page. |
| `FAILED` | Cannot reach the address entered. Check the unit is powered, the Host is right *for this network*, and both are on the same subnet. |
| `CONNECTED` then `NO DATA` | The link is fine and the unit stopped sending. Check the unit. |
| Won't join the network | The device is **2.4 GHz only**. A 5 GHz-only network, or a router steering devices to 5 GHz under a shared name, cannot work. |
| Targets appear with no names | Normal — positions arrive every few seconds, names every few minutes. |
| Plot is empty, list is not | The targets are beyond the range ring. The bottom line says how many; press UP. |
| `NO OWN POSITION` | Your unit isn't sending its own position. See above. |
| Faint marks left behind | E-ink ghosting from moving symbols. It clears on the deeper repaint every 30 seconds. |
| Settings vanished after an update | Expected when installing from the web page, which rewrites the storage area. A USB `pio run -t upload` preserves them. |

For anything deeper, connect USB and watch the serial log at 115200 baud — it
reports the Wi-Fi join, every connection attempt, and why one failed.

---

## Limits worth knowing

- **Decodes AIS types 1/2/3, 5, 18/19 and 24** — Class A and Class B position
  and identity. Base stations (type 4) and aids to navigation (type 21) are
  counted but not plotted; they would crowd a small screen.
- **128 targets.** Beyond that the vessel heard from longest ago is dropped, so
  the display always reflects what is nearby now.
- **Targets disappear after 6 minutes** of silence. Class B at anchor reports
  only every 3 minutes, so a shorter window would drop moored vessels.
- **North-up only.** No course-up.
- **Range and bearing are computed flat**, not great-circle. At plot ranges the
  difference is far below one pixel.
- **The setup page is at 192.168.7.1**, not the usual 192.168.4.1 — many AIS
  units use that address for their own access point, and sharing it would stop
  the device reaching the unit at all.

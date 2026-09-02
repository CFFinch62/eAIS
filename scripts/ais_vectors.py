#!/usr/bin/env python3
"""Generates the AIS test sentences used by test/ais/test_decoder.cpp.

The expected values in those tests come from here, not from the C++ decoder, so
the two can disagree. They did, once: an earlier version of six_bit_text()
mapped ASCII 32-63 to value-32, encoding a space as 0. That round-trips to '@'
under a correct decoder, which is exactly what the C++ produced - the decoder
was right and the vector was wrong.

In AIS six-bit ASCII:
    value 0..31  -> '@' 'A'..'Z' '[' '\\' ']' '^' '_'     (ASCII - 64)
    value 32..63 -> ' ' '!' .. '?'                        (ASCII unchanged)

Run it to regenerate the sentences if a test needs different contents.
"""


def enc6(v):
    return chr(v + 48) if v < 40 else chr(v + 56)


def pack(bits):
    while len(bits) % 6:
        bits += "0"
    return "".join(enc6(int(bits[i:i + 6], 2)) for i in range(0, len(bits), 6))


def checksum(body):
    c = 0
    for ch in body:
        c ^= ord(ch)
    return f"{c:02X}"


def sentence(body):
    return f"!{body}*{checksum(body)}"


def six_bit_text(s, chars):
    """ASCII to AIS six-bit values. Space is 32, not 0 - see the module note."""
    s = s.upper()[:chars].ljust(chars, "@")
    out = ""
    for ch in s:
        a = ord(ch)
        v = a - 64 if 64 <= a <= 95 else (a if 32 <= a <= 63 else 0)
        out += f"{v:06b}"
    return out


def fragments(payload, kind="AIVDM", seq="0", channel="A", per=60):
    parts = [payload[i:i + per] for i in range(0, len(payload), per)]
    n = len(parts)
    return [sentence(f"{kind},{n},{k + 1},{seq if n > 1 else ''},{channel},{p},0")
            for k, p in enumerate(parts)]


def type5(mmsi, name, callsign):
    b = f"{5:06b}" + "00" + f"{mmsi:030b}" + f"{0:02b}" + f"{9134567:030b}"
    b += six_bit_text(callsign, 7) + six_bit_text(name, 20) + f"{70:08b}"
    return fragments(pack(b + "0" * (424 - len(b))))


def type18(mmsi, lat, lon, sog, cog):
    b = f"{18:06b}" + "00" + f"{mmsi:030b}" + "0" * 8
    b += f"{int(round(sog * 10)):010b}" + "0"
    b += f"{int(round(lon * 600000)) & 0xFFFFFFF:028b}"
    b += f"{int(round(lat * 600000)) & 0x7FFFFFF:027b}"
    b += f"{int(round(cog * 10)):012b}" + f"{511:09b}"
    return fragments(pack(b + "0" * (168 - len(b))))[0]


def type24a(mmsi, name):
    b = f"{24:06b}" + "00" + f"{mmsi:030b}" + f"{0:02b}" + six_bit_text(name, 20)
    return fragments(pack(b + "0" * (168 - len(b))))[0]


def type1(mmsi, lat, lon, sog, cog, hdg, kind="AIVDM"):
    """Class A position report. `kind` picks AIVDM (another vessel) or AIVDO
    (own ship) - it must be passed here rather than string-substituted into a
    finished sentence, which would leave the checksum describing the old body."""
    b = f"{1:06b}" + "00" + f"{mmsi:030b}" + f"{0:04b}" + f"{128:08b}"
    b += f"{int(round(sog * 10)):010b}" + "0"
    b += f"{int(round(lon * 600000)) & 0xFFFFFFF:028b}"
    b += f"{int(round(lat * 600000)) & 0x7FFFFFF:027b}"
    b += f"{int(round(cog * 10)):012b}" + f"{hdg:09b}"
    return fragments(pack(b + "0" * (168 - len(b))), kind=kind)[0]


if __name__ == "__main__":
    print("Type 5 - MMSI 235009802, name 'TEST VESSEL ONE', callsign 'TSTVSL'")
    for s in type5(235009802, "TEST VESSEL ONE", "TSTVSL"):
        print("  " + s)
    print("\nType 18 - MMSI 338123456, 37.5 N, -122.5 E, 12.3 kn, COG 87.6, heading n/a")
    print("  " + type18(338123456, 37.5, -122.5, 12.3, 87.6))
    print("\nType 24 part A - MMSI 338123456, name 'LITTLE BOAT'")
    print("  " + type24a(338123456, "LITTLE BOAT"))
    print("\nType 1 as VDO - MMSI 366999123, 37.9 N, -122.4 E, 6.4 kn, COG 345, hdg 347")
    print("  " + type1(366999123, 37.9, -122.4, 6.4, 345.0, 347, kind="AIVDO"))

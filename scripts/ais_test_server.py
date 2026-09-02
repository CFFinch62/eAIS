#!/usr/bin/env python3
"""Synthetic AIS feed for exercising eAIS without hardware.

Emits a small fleet of moving targets, their names (type 5 / type 24, so the
multi-fragment path gets used), and an own-ship position, so both the list and
the plot have something real to show.

    python3 scripts/ais_test_server.py                # TCP, device dials in
    python3 scripts/ais_test_server.py --proto udp    # UDP broadcast

Sentence construction lives in ais_vectors.py, which is also what the unit
tests use - one encoder, so a bug in it shows up in both places rather than
making the tests agree with a broken server.
"""
import argparse
import math
import socket
import time

import ais_vectors as av

OWN_LAT, OWN_LON = 47.6062, -122.3321

# name, mmsi, bearing from own ship, range nm, course, speed
FLEET = [
    ("PACIFIC TRADER", 366053209, 30.0, 1.2, 210.0, 12.5),
    ("HARBOR QUEEN", 366999712, 110.0, 2.4, 315.0, 8.0),
    ("SEA SPRITE", 244660564, 200.0, 0.7, 45.0, 4.2),
    ("NORTHERN STAR", 235009802, 285.0, 3.6, 90.0, 16.8),
    ("LITTLE BOAT", 338123456, 340.0, 0.4, 180.0, 3.1),
]


def offset(lat, lon, bearing_deg, range_nm):
    b = math.radians(bearing_deg)
    dlat = range_nm * math.cos(b) / 60.0
    dlon = range_nm * math.sin(b) / (60.0 * math.cos(math.radians(lat)))
    return lat + dlat, lon + dlon


def batch(tick):
    lines = []
    # Own ship, so the plot has a centre. Alternates with nav sentences would be
    # more realistic, but VDO is what a properly configured unit sends.
    lines.append(av.type1(367000001, OWN_LAT, OWN_LON, 6.2, 45.0, 47, kind="AIVDO") + "\r\n")

    for i, (name, mmsi, brg, rng, cog, sog) in enumerate(FLEET):
        # Drift the targets so successive redraws visibly differ.
        drift = tick * sog / 3600.0 * 2.0
        lat, lon = offset(OWN_LAT, OWN_LON, brg + tick * 0.4, max(0.1, rng - drift % rng))
        if mmsi == 338123456:  # the Class B unit
            lines.append(av.type18(mmsi, lat, lon, sog, cog) + "\r\n")
            if tick % 10 == i:
                lines.append(av.type24a(mmsi, name) + "\r\n")
        else:
            lines.append(av.type1(mmsi, lat, lon, sog, cog, int(cog)) + "\r\n")
            # Names arrive far less often than positions, exactly as they do on
            # the water - a display that only shows named targets shows nothing
            # for the first few minutes.
            if tick % 12 == i:
                for f in av.type5(mmsi, name, "TST%03d" % i):
                    lines.append(f + "\r\n")
    return "".join(lines).encode("ascii")


def serve_tcp(args):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.port))
    srv.listen(1)
    print(f"[ais-test-server] TCP on {args.bind}:{args.port} - {len(FLEET)} targets + own ship")
    while True:
        print("[ais-test-server] waiting for a connection...")
        conn, addr = srv.accept()
        print(f"[ais-test-server] client connected: {addr}")
        tick = 0
        try:
            while True:
                conn.sendall(batch(tick))
                tick += 1
                time.sleep(2.0)
        except OSError as exc:
            print(f"[ais-test-server] client gone ({exc}) - waiting for reconnect")
        finally:
            conn.close()


def serve_udp(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    target = args.udp_target or "255.255.255.255"
    print(f"[ais-test-server] UDP to {target}:{args.port}")
    tick = 0
    while True:
        for line in batch(tick).splitlines(keepends=True):
            try:
                sock.sendto(line, (target, args.port))
            except OSError as exc:
                print(f"[ais-test-server] send failed ({exc}) - continuing")
        tick += 1
        time.sleep(2.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", choices=("tcp", "udp"), default="tcp")
    ap.add_argument("--port", type=int, default=10110)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--udp-target", default=None)
    args = ap.parse_args()
    serve_tcp(args) if args.proto == "tcp" else serve_udp(args)


if __name__ == "__main__":
    main()

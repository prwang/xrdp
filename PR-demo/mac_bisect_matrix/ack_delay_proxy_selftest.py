#!/usr/bin/env python3
"""ack_delay_proxy_selftest.py -- prove the delay line before spending a
session on it. BACKLOG #79 layer 1, step 0.

The escalation ladder says the cheapest instrument that can answer the
question, first. Before the proxy is put in front of a real arm, three
things about it must be true, and all three are answerable on loopback
in a few seconds:

  1. it applies the delay it was asked for, in the client->server
     direction, and only there;
  2. at D = 0 it is transparent -- the added RTT is noise next to the
     22 ms frame period being measured;
  3. it does not throttle the server->client direction, which carries
     the 4K video and must stay far above the ~50-100 MB/s a flood
     session pushes. A proxy that caps that would confound every leg.

A failure here is a harness fault and stops the sweep.

Usage: ack_delay_proxy_selftest.py [path-to-proxy]
"""
import os
import socket
import statistics as st
import struct
import subprocess
import sys
import threading
import time

PROXY = sys.argv[1] if len(sys.argv) > 1 else \
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "ack_delay_proxy")
SRV_PORT = 45501
PXY_PORT = 45502
BULK_MB = 200
# What the server->client direction must sustain. The measured payload
# of the run this proxy will sit in front of (i78_x017_pumpsplit,
# 3840x2400 textflood, oracle client) is 8.70 GB over 56.8 s = 153 MB/s.
# A ratio against loopback's ~10 GB/s is not the right test -- any proxy
# costs two copies -- so the criterion is an absolute floor with a wide
# margin over what the session actually pushes.
FLOOR_MBPS = 1000.0


def server(sock, stop):
    """echo 8-byte pings; on 'B' send BULK_MB of zeros as fast as it can"""
    while not stop.is_set():
        try:
            c, _ = sock.accept()
        except OSError:
            return
        threading.Thread(target=serve_one, args=(c,), daemon=True).start()


def serve_one(c):
    """messages are 8 bytes: b'P' + 7-byte counter, or b'B' + 7 zeros.

    The tag byte is FIRST and the counter is behind it on purpose. The
    first version packed the counter alone ('<Q'), so ping number 66
    serialised its low byte as 0x42 = 'B' and turned itself into a bulk
    request: the client then read 200 MB of zeros 8 bytes at a time and
    reported an RTT of 0.011 ms for a 10 ms delay line that was working
    perfectly. A self-test can lie about the thing it is testing.
    """
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    buf = b""
    blob = b"\0" * (256 * 1024)
    try:
        while True:
            d = c.recv(65536)
            if not d:
                break
            buf += d
            while len(buf) >= 8:
                msg, buf = buf[:8], buf[8:]
                if msg[:1] == b"B":
                    for _ in range((BULK_MB * 1024 * 1024) // len(blob)):
                        c.sendall(blob)
                    c.sendall(b"E" * 8)
                else:
                    c.sendall(msg)
    except OSError:
        pass
    finally:
        c.close()


def ping_rtt(port, n=200):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    out = []
    for i in range(n):
        t0 = time.perf_counter()
        s.sendall(b"P" + struct.pack("<Q", i)[:7])
        got = b""
        while len(got) < 8:
            got += s.recv(8 - len(got))
        out.append((time.perf_counter() - t0) * 1000.0)
        time.sleep(0.002)
    s.close()
    return out


def bulk_mbps(port):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.sendall(b"B" + b"\0" * 7)
    want = BULK_MB * 1024 * 1024
    got = 0
    t0 = time.perf_counter()
    while got < want:
        d = s.recv(1 << 20)
        if not d:
            break
        got += len(d)
    el = time.perf_counter() - t0
    s.close()
    return got / el / 1e6


def start_proxy(delay):
    errf = open(f"/tmp/ack_proxy_selftest_d{delay}.err", "w+")
    p = subprocess.Popen([PROXY, "-l", str(PXY_PORT), "-r", str(SRV_PORT),
                          "-d", str(delay)],
                         stderr=errf, stdout=subprocess.DEVNULL)
    p.errpath = errf.name
    time.sleep(0.3)
    return p


def main():
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", SRV_PORT))
    ls.listen(8)
    stop = threading.Event()
    threading.Thread(target=server, args=(ls, stop), daemon=True).start()

    print("== direct (no proxy) ==")
    d_rtt = ping_rtt(SRV_PORT)
    d_bulk = bulk_mbps(SRV_PORT)
    print(f"  rtt p50 {st.median(d_rtt):.3f} ms  p90 "
          f"{sorted(d_rtt)[180]:.3f} ms   bulk {d_bulk:.0f} MB/s")

    fails = []
    for D in (0, 10, 20, 40):
        p = start_proxy(D)
        rtt = ping_rtt(PXY_PORT)
        bulk = bulk_mbps(PXY_PORT)
        p.terminate()
        p.wait(timeout=5)
        err = open(p.errpath).read()
        p50 = st.median(rtt)
        add = p50 - st.median(d_rtt)
        print(f"== D={D} ==")
        print(f"  rtt p50 {p50:.3f} ms (direct + {add:.3f})  "
              f"p90 {sorted(rtt)[180]:.3f}   bulk {bulk:.0f} MB/s")
        for ln in err.strip().splitlines():
            if "applied delay" in ln or "close after" in ln:
                print("   ", ln.strip())
        if abs(add - D) > 1.0:
            fails.append(f"D={D}: applied {add:.2f} ms of RTT, wanted {D}")
        if bulk < FLOOR_MBPS:
            fails.append(f"D={D}: server->client {bulk:.0f} MB/s is below "
                         f"the {FLOOR_MBPS:.0f} MB/s floor (the session "
                         f"pushes 153 MB/s); direct is {d_bulk:.0f}")
    stop.set()
    ls.close()
    if fails:
        print("\nSELFTEST RED:")
        for f in fails:
            print("  -", f)
        return 1
    print("\nSELFTEST GREEN: delay applies in one direction only, "
          "D=0 is transparent, video direction not throttled")
    return 0


if __name__ == "__main__":
    sys.exit(main())

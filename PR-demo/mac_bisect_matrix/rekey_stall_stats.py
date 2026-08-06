#!/usr/bin/env python3
"""How long does a #48 re-key actually stall the session?

BACKLOG #48 was accepted on the owner's terms: "I'd rather let the
session glitch for ~690 ms every hour than let xrdp have undefined
behaviour every hour." 690 ms was an ESTIMATE carried over from the
encoder-respawn cost measured elsewhere. This measures the real thing
from the server's own log, so the trade the owner agreed to is stated
in numbers rather than in a guess.

The interval measured is:

  "aux_ltr_chain frame_num N reached the re-key threshold" (encoder
      thread notices the counter)
    -> "aux_ltr_chain re-key: surface 0 rebuilt WxH" (main thread has
      torn the surface down, recreated it, and is about to ship the
      full-surface IDR)

which is the server-side cost of the boundary. Client decode of the
fresh IDR is on top of it and is not visible here.

Known artifact, reported rather than hidden: xrdp's logger occasionally
emits a triple whose timestamps run BACKWARD (observed 2026-07-29:
02:07:44.607 -> .659 -> .268). Those events are counted and reported
separately instead of being averaged in as negative durations.

Usage: rekey_stall_stats.py <xrdp.log> [HH:MM:SS-lo HH:MM:SS-hi]
"""
import re
import sys

STAMP = re.compile(r'\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d\d\d)')
REQ = 'reached the re-key threshold'
DONE = 're-key: surface'


def ms_of(line):
    m = STAMP.match(line)
    if not m:
        return None
    return (int(m.group(4)) * 3600000 + int(m.group(5)) * 60000
            + int(m.group(6)) * 1000 + int(m.group(7)))


def to_ms(hms):
    h, m, s = (int(x) for x in hms.split(':'))
    return h * 3600000 + m * 60000 + s * 1000


def main():
    if len(sys.argv) < 2:
        print('usage: rekey_stall_stats.py <xrdp.log> [lo hi]')
        return 2
    lo = to_ms(sys.argv[2]) if len(sys.argv) > 3 else 0
    hi = to_ms(sys.argv[3]) if len(sys.argv) > 3 else 24 * 3600000

    events = []
    for line in open(sys.argv[1]):
        stamp = ms_of(line)
        if stamp is None or not lo <= stamp <= hi:
            continue
        if REQ in line:
            events.append(('REQ', stamp))
        elif DONE in line and 'rebuilt' in line:
            events.append(('DONE', stamp))

    gaps = []
    inverted = []
    pending = None
    for kind, stamp in events:
        if kind == 'REQ':
            pending = stamp
        elif pending is not None:
            delta = stamp - pending
            (gaps if delta >= 0 else inverted).append(delta)
            pending = None

    total = len(gaps) + len(inverted)
    print('re-key boundaries in window : %d' % total)
    print('usable measurements         : %d' % len(gaps))
    print('log-timestamp inversions    : %d (excluded)' % len(inverted))
    if gaps:
        gaps.sort()
        print('server-side stall (ms)      : %s' % gaps)
        print('min %d  median %d  max %d  mean %.0f'
              % (gaps[0], gaps[len(gaps) // 2], gaps[-1],
                 sum(gaps) / float(len(gaps))))
    return 0


if __name__ == '__main__':
    sys.exit(main())

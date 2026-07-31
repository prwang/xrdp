#!/usr/bin/env python3
"""i55_analyze.py — partition the m=1 AVC444 cycle from i55_h1h2_uprobe.sh
(v2) events and localize the capture/encode serializer on the T4.

Usage: i55_analyze.py <extracted i55_<tag> dir> [<dir2> ...]

The true cycle order at m=1 (verified against the raw condB stream,
2026-07-31 — frames are strictly serial, overlap 0/205 established by the
07-31 gfx_trace measurement):

  xrecv(ack N-1) -> caprect_N -> pack..pack_ret (Xorg)
    -> [msg62 -> worker] pset spin ... collect (children encoded)
    -> pegfx/fstart .. wire .. fend (EGFX assembly)
    -> last wire -> aemit_N (module ack) -> xrecv -> (timer) -> caprect_N+1

Every leg is paired GLOBALLY (first event of the successor stream at or
after the predecessor), never by cycle window — cycle-window pairing
produced 265/268 negative pairings on the first attempt because client
acks belong to the frame before. A closure check (legs vs period) is
printed so a broken pairing is visible, not summarized over.

Scheduler delay (H2): per-thread run_delay delta over the window from two
schedstat reads; a starved thread shows a large runnable share."""
import sys, os, re, bisect, collections

EV = re.compile(r'^\s*(?P<comm>\S+)\s+(?P<pid>\d+)/(?P<tid>\d+)\s+'
                r'(?P<ts>\d+\.\d+):\s+(?P<grp>probe_\w+):(?P<name>\w+):')


def load_events(d):
    evs = collections.defaultdict(list)
    with open(os.path.join(d, 'events.txt')) as f:
        for line in f:
            m = EV.match(line)
            if m:
                evs[m['name']].append(float(m['ts']))
    for v in evs.values():
        v.sort()
    return evs


def stats(v):
    if not v:
        return 'n=0'
    s = sorted(v)
    n = len(s)
    return ('n=%-4d mean=%6.1f p50=%6.1f p90=%6.1f max=%6.1f'
            % (n, sum(s) / n, s[n // 2], s[int(n * .9)], s[-1]))


def chain(prev, nxt, cap_ms=1000.0):
    """For each ts in prev, delta to first nxt >= ts (ms). Drops pairs
    whose successor is beyond cap_ms (window edge)."""
    out = []
    for t in prev:
        i = bisect.bisect_left(nxt, t)
        if i < len(nxt) and (nxt[i] - t) * 1000.0 <= cap_ms:
            out.append((nxt[i] - t) * 1000.0)
    return out


def mean(v):
    return sum(v) / len(v) if v else 0.0


def schedstat(d):
    def read(name):
        m = {}
        with open(os.path.join(d, name)) as f:
            for line in f:
                p = line.split()
                if len(p) >= 6:
                    m[int(p[1])] = (p[2], int(p[3]), int(p[4]))
        return m
    b, a = read('schedstat_before.txt'), read('schedstat_after.txt')
    rows = []
    for tid, (comm, run, wait) in a.items():
        if tid in b:
            rows.append((comm, tid, (run - b[tid][1]) / 1e6,
                         (wait - b[tid][2]) / 1e6))
    rows.sort(key=lambda r: -r[3])
    return rows


def main():
    for d in sys.argv[1:]:
        print('=' * 74)
        print('== %s' % d)
        with open(os.path.join(d, 'env.txt')) as f:
            print(f.read().rstrip())
        ev = load_events(d)
        n_frames = len(ev.get('caprect', []))
        span = 0.0
        allts = sorted(t for v in ev.values() for t in v)
        if len(allts) > 1:
            span = allts[-1] - allts[0]
        print('-- events over %.1f s: %s' % (span, ' '.join(
            '%s:%d' % (k, len(v)) for k, v in sorted(ev.items()))))

        legs = [
            ('capture+pack   caprect->pack_ret', 'caprect',
             'pack_ret__return'),
            ('handoff+encode pack_ret->collect', 'pack_ret__return',
             'collect'),
            ('emit prep      collect->fstart', 'collect', 'fstart'),
            ('EGFX assembly  fstart->fend', 'fstart', 'fend'),
            ('send tail      fend->aemit', 'fend', 'aemit'),
            ('ack transit    aemit->xrecv (H1)', 'aemit', 'xrecv'),
            ('re-arm         xrecv(post-ack)->caprect', None, None),
            ('client leg     fend->cack (context)', 'fend', 'cack'),
        ]
        results = {}
        for title, a, b in legs:
            if a is None:
                continue
            results[title] = chain(ev.get(a, []), ev.get(b, []))
        # re-arm: from the xrecv that FOLLOWS each aemit to the next caprect
        post_ack_recv = []
        xr = ev.get('xrecv', [])
        for t in ev.get('aemit', []):
            i = bisect.bisect_left(xr, t)
            if i < len(xr):
                post_ack_recv.append(xr[i])
        results['re-arm         xrecv(post-ack)->caprect'] = chain(
            post_ack_recv, ev.get('caprect', []))

        print('-- cycle legs (ms):')
        serial_sum = 0.0
        for title, _, _ in legs:
            v = results.get(title, [])
            print('   %-38s %s' % (title, stats(v)))
            if 'context' not in title and 'H1' not in title:
                serial_sum += mean(v)
        serial_sum += mean(results['ack transit    aemit->xrecv (H1)'])
        periods = [(b - a) * 1000.0 for a, b in
                   zip(ev.get('caprect', []), ev.get('caprect', [])[1:])]
        print('   %-38s %s' % ('period         caprect->caprect',
                               stats(periods)))
        print('-- closure: serial legs sum %.1f ms vs period mean %.1f ms '
              '(gap %.1f ms unattributed)'
              % (serial_sum, mean(periods), mean(periods) - serial_sum))

        # worker poll behaviour inside the encode wait
        pset = ev.get('pset', [])
        if pset and n_frames:
            print('-- pump_set polls: %d total = %.1f per frame'
                  % (len(pset), len(pset) / n_frames))
        cb = len(ev.get('cbfire', []))
        print('-- timer fires: %d, captures: %d (fires refused: %d)'
              % (cb, n_frames, cb - n_frames))

        print('-- scheduler delay over the window (H2; top 10 by '
              'runnable_ms):')
        print('   %-16s %8s %12s %12s %8s' % (
            'comm', 'tid', 'ran_ms', 'runnable_ms', 'starv%'))
        for comm, tid, run, wait in schedstat(d)[:10]:
            tot = run + wait
            print('   %-16s %8d %12.1f %12.1f %7.1f%%' % (
                comm, tid, run, wait, 100 * wait / tot if tot else 0))


if __name__ == '__main__':
    main()

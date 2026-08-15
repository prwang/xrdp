#!/usr/bin/env python3
"""BACKLOG #92 / PRD FR-H264-9 -- read an i92_sparse_aux_ab.sh capture.

WHAT THE EXPERIMENT IS. One arm, two configurations of one gfx.toml,
interleaved off/on/off/on:

  OFF   chroma_refresh_ms = 0     the AVC444 aux (chroma) view goes with
                                  every frame -- the behaviour that
                                  shipped before #92
  ON    chroma_refresh_ms = 1000  chroma is sent when the screen settles
        chroma_idle_ms   = 100    (100 ms quiet) and at least once a
                                  second whatever the screen is doing;
                                  every other frame ships the luma view
                                  alone

Same image, same producer, same payload, same geometry, same hour. The
interleave is there because an unchanged arm on this host has drifted
36.8 -> 41.8 ms across one day (BACKLOG #88).

THE MECHANISM CHECK COMES BEFORE THE RATE, and this script prints it
first and refuses to compute a ratio if it fails. A knob that was set
but produced no change in its own telemetry has not been tested (the
project's quality gate 2, learned from an arm where the concurrency a
change existed to create never appeared and the rate was reported
anyway).

THE RECORDS THIS READS, with their printed field names:

  auxdue  monitor=monitor index
          due=THE DECISION: 1 = the chroma view went with this frame,
            0 = luma only
          since_aux_ms=ms since this monitor last carried chroma
          since_previous_ms=ms since its previous frame
          refresh_ms=chroma_refresh_ms as the running binary holds it, so a leg
            can be shown to have had the config it claims
  egress  ONE PER FRAME handed to the transport. a=the frame's id,
          pending_kib=bytes queued in the transport, client=the last frame the
          client had acknowledged. This -- not 'send' -- is the frame
          delivery rate, and it reproduces e_gate_run.sh's own
          "send-to-send gap" line to within 0.2 ms, which is how the two
          instruments are checked against each other.
  send    one per network WRITE of encoded bytes, of which there are
          several per frame. Counted here only because the count itself
          moves: a luma-only frame carries one PDU instead of two.
  pump_beg/pump_end
          the encoder worker's wait for the ffmpeg children to finish
          encoding a frame -- the segment a second encode is paid in.

Usage: i92_sparse_aux_analyze.py <capture-dir>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from perf_trace_records import read_records


def load(path):
    """Read named records; every payload field keeps its printed name."""
    return [(r["mono_ns"], r["event"], r) for r in read_records(path)
            if r["event"] != "clock_base"]


def leg_records(legdir):
    """Every ring in the leg, merged and sorted by the monotonic clock.
    The encoder worker and the main thread write separate rings."""
    recs = []
    perf = os.path.join(legdir, 'perf')
    if not os.path.isdir(perf):
        return recs
    for name in sorted(os.listdir(perf)):
        recs.extend(load(os.path.join(perf, name)))
    recs.sort(key=lambda r: r[0])
    return recs


def pct(xs, p):
    if not xs:
        return float('nan')
    s = sorted(xs)
    i = int(round((len(s) - 1) * p / 100.0))
    return s[i]


def mean(xs):
    return sum(xs) / float(len(xs)) if xs else float('nan')


def analyse_leg(legdir):
    recs = leg_records(legdir)
    out = {'dir': os.path.basename(legdir), 'n_records': len(recs)}

    body = os.path.join(legdir, 'window.txt')
    if os.path.exists(body):
        for line in open(body):
            k, _, v = line.strip().partition(' ')
            out[k] = v

    aux = [r for r in recs if r[1] == 'auxdue']
    out['auxdue_records'] = len(aux)
    out['chroma_frames'] = sum(1 for r in aux if r[2]['due'] == 1)
    out['luma_only_frames'] = sum(1 for r in aux if r[2]['due'] == 0)
    out['refresh_ms_on_wire'] = sorted({r[2]['refresh_ms'] for r in aux})

    # the GUARANTEE, measured rather than assumed: the wall-clock gap
    # between consecutive frames that carried chroma, per monitor
    gaps = []
    last = {}
    for t, _, fields in aux:
        if fields['due'] != 1:
            continue
        mon = fields['monitor']
        if mon in last:
            gaps.append((t - last[mon]) / 1e6)
        last[mon] = t
    out['chroma_gap_ms_mean'] = mean(gaps)
    out['chroma_gap_ms_p50'] = pct(gaps, 50)
    out['chroma_gap_ms_p99'] = pct(gaps, 99)
    out['chroma_gap_ms_max'] = max(gaps) if gaps else float('nan')
    out['chroma_gaps'] = len(gaps)

    # THE DELIVERY RATE. One egress record per frame handed to the
    # transport -- not one per network write, of which there are
    # several per frame. Using 'send' here would silently measure PDUs
    # per second and would move for the trivial reason that a luma-only
    # frame carries one PDU where a full frame carries two.
    egress = [r for r in recs if r[1] == 'egress']
    out['frames'] = len(egress)
    iv = [(b[0] - a[0]) / 1e6 for a, b in zip(egress, egress[1:])]
    out['frame_ms_mean'] = mean(iv)
    out['frame_ms_p50'] = pct(iv, 50)
    out['frame_ms_p90'] = pct(iv, 90)
    out['frame_ms_p99'] = pct(iv, 99)
    if egress:
        out['span_s'] = (egress[-1][0] - egress[0][0]) / 1e9
    out['writes'] = sum(1 for r in recs if r[1] == 'send')
    out['writes_per_frame'] = (out['writes'] / float(out['frames'])
                               if out['frames'] else float('nan'))

    # the frame period as the ENCODER sees it: pump_beg to pump_beg
    pb = [r for r in recs if r[1] == 'pump_beg']
    piv = [(b[0] - a[0]) / 1e6 for a, b in zip(pb, pb[1:])]
    out['pump_cycles'] = len(pb)
    out['period_ms_mean'] = mean(piv)
    out['period_ms_p50'] = pct(piv, 50)

    # how long the pump itself took -- the wait for the ffmpeg children.
    # Paired by ORDER within one thread's ring, which is safe here
    # because pump_beg/pump_end bracket one another on the worker and
    # cannot interleave.
    pumps = [r for r in recs if r[1] in ('pump_beg', 'pump_end')]
    dur = []
    open_t = None
    for t, n, _ in pumps:
        if n == 'pump_beg':
            open_t = t
        elif open_t is not None:
            dur.append((t - open_t) / 1e6)
            open_t = None
    out['pump_ms_mean'] = mean(dur)
    out['pump_ms_p50'] = pct(dur, 50)

    # WHY THE PERIOD DOES OR DOES NOT MOVE. A child cannot start
    # encoding until its whole raw picture has arrived, so its encode
    # window is [feedend, outfirst] -- the same construction BACKLOG
    # #91 used. Field b of both records is (self->leaf == NULL): 0 is
    # the MAIN child (it owns the aux child as its leaf), 1 is the AUX
    # child. Windows are paired by (child, sequence) -- by IDENTITY,
    # never by time window.
    #
    # If the two windows overlap, the aux encode was hidden behind the
    # main one and deleting it frees nothing from the critical path,
    # however many bytes it saved.
    fed = {}
    for r in [x for x in recs if x[1] == 'feedend']:
        fed.setdefault((r[2]['main'], r[2]['sequence']), r[0])
    win = {}
    for r in [x for x in recs if x[1] == 'outfirst']:
        k = (r[2]['main'], r[2]['sequence'])
        if k in fed and k not in win:
            win[k] = (fed[k], r[0])
    wmain = {k[1]: v for k, v in win.items() if k[0] == 0}
    waux = {k[1]: v for k, v in win.items() if k[0] == 1}
    ov = []
    after = []
    for seq, (a0, a1) in waux.items():
        if seq in wmain:
            m0, m1 = wmain[seq]
            ov.append(max(0, min(a1, m1) - max(a0, m0)) / 1e6)
            after.append((m1 - min(a1, m1)) / 1e6)
    out['main_encode_ms'] = mean([(b - a) / 1e6 for a, b in wmain.values()])
    out['aux_encode_ms'] = mean([(b - a) / 1e6 for a, b in waux.values()])
    out['aux_windows'] = len(waux)
    out['overlap_ms'] = mean(ov)
    out['main_after_aux_ms'] = mean(after)
    out['overlap_pct'] = (100.0 * out['overlap_ms'] / out['aux_encode_ms']
                          if out['aux_encode_ms'] else float('nan'))

    # WHERE THE WHOLE CYCLE GOES, from the MAIN child's point of view.
    # Asked 2026-08-08: "is the main encoder capable of accepting faster
    # and waiting less?" -- i.e. would letting xrdp run further ahead
    # (more frames in flight) fill idle time and raise the rate? That is
    # only answerable if the cycle is fully accounted for, so these four
    # segments are built to SUM to it, and the residual is printed.
    #
    #   feed     pump_beg -> feedend: the raw picture going into the
    #            child's 1 MiB pipe. xrdp's side is vmsplice -- page
    #            references, near-free -- so the elapsed time here is
    #            the CHILD's read() copying, not xrdp waiting.
    #   encode   feedend -> outfirst: the child holds a whole picture
    #            and produces the first encoded byte.
    #   drain    outfirst -> pump_end: xrdp reading the rest of it.
    #   between  pump_end -> next pump_beg: collect, the LTR rewrite,
    #            emit, slot release.
    order = sorted(wmain)
    feed = []
    enc = []
    drain = []
    between = []
    cyc = []
    pbeg = [r[0] for r in recs if r[1] == 'pump_beg']
    pend = [r[0] for r in recs if r[1] == 'pump_end']
    ncyc = min(len(pbeg), len(pend), len(order))
    for i in range(ncyc):
        a0, a1 = wmain[order[i]]
        feed.append((a0 - pbeg[i]) / 1e6)
        enc.append((a1 - a0) / 1e6)
        drain.append((pend[i] - a1) / 1e6)
        if i + 1 < ncyc:
            between.append((pbeg[i + 1] - pend[i]) / 1e6)
            cyc.append((pbeg[i + 1] - pbeg[i]) / 1e6)
    out['seg_feed_ms'] = mean(feed)
    out['seg_encode_ms'] = mean(enc)
    out['seg_drain_ms'] = mean(drain)
    out['seg_between_ms'] = mean(between)
    out['seg_cycle_ms'] = mean(cyc)
    out['seg_residual_ms'] = (out['seg_cycle_ms'] - out['seg_feed_ms']
                              - out['seg_encode_ms'] - out['seg_drain_ms']
                              - out['seg_between_ms'])

    # DID THE WORKER EVER HAVE NOTHING TO ENCODE? If it did, the
    # producer is the limit and nothing xrdp does downstream matters.
    # Reported as a COUNT above a threshold rather than a mean: the
    # session-start wait is seconds long and would swamp any average.
    waits = []
    open_t = None
    for t, n, _ in [(t, n, f) for t, n, f in recs
                    if n in ('wait_beg', 'wait_end')]:
        if n == 'wait_beg':
            open_t = t
        elif open_t is not None:
            waits.append((t - open_t) / 1e6)
            open_t = None
    out['waits'] = len(waits)
    out['waits_over_1ms'] = sum(1 for w in waits if w > 1.0)
    out['wait_p50_ms'] = pct(waits, 50)
    out['wait_p99_ms'] = pct(waits, 99)

    # WHICH TERM OF THE CREDIT ACTUALLY BINDS. The producer captures
    # only when xrdp grants it a frame id, and that credit is
    #     min(frame_id_consumed, frame_id_server + 1, frame_id_client + C)
    # -- the children have absorbed it / one frame of pipeline inventory
    # / the end-to-end wire window. Asking which of the three is the
    # minimum is how "would turning a flow-control knob raise the rate?"
    # is answered without turning one. The ackslot record carries all
    # three plus C, so this is read off the wire, not inferred.
    #
    # Ties are counted as ties, deliberately: if two terms are equal,
    # relaxing either ALONE changes nothing, and reporting one of them
    # as "the binder" would point at a knob that cannot move anything.
    ack = [r for r in recs if r[1] == 'ack'
           and r[2].get('class') == 'ACK_TRACE'
           and r[2].get('kind') == 'slot']
    bind = {}
    head = {}
    for _, _, fields in ack:
        server = fields['egress']
        consumed = fields['absorbed']
        client = fields['client']
        wwin = fields['window']
        terms = {'absorbed': consumed,
                 'inventory': server + 1,
                 'wire': client + wwin}
        lo = min(terms.values())
        key = '+'.join(sorted(k for k, v in terms.items() if v == lo))
        bind[key] = bind.get(key, 0) + 1
        h = (client + wwin) - (server + 1)
        head[h] = head.get(h, 0) + 1
    out['acks'] = len(ack)
    out['credit_binder'] = dict(sorted(bind.items(),
                                       key=lambda kv: -kv[1]))
    out['wire_headroom'] = dict(sorted(head.items()))
    out['wire_window'] = sorted({fields['window']
                                 for _, _, fields in ack})

    # how many children the pump armed. 2 per monitor normally; 1 for a
    # monitor whose chroma was skipped. This is the skip visible from
    # the OTHER side of the mechanism.
    pe = [r for r in recs if r[1] == 'pump_end']
    armed = {}
    for _, _, fields in pe:
        armed[fields['kids_armed']] = armed.get(fields['kids_armed'], 0) + 1
    out['kids_armed_histogram'] = dict(sorted(armed.items()))

    # THE BYTES. The oracle client saves every encoded payload it
    # receives, so the dump size is what the server actually put on the
    # wire over the leg. Divided by the frame count it is the quantity
    # the comparison is about -- a whole-leg total would move with the
    # frame count as well as with the treatment.
    dump = os.path.join(legdir, 'oracle_dump_bytes.txt')
    if os.path.exists(dump):
        txt = open(dump).read().split()
        for tok in txt:
            if tok.isdigit():
                out['oracle_dump_bytes'] = int(tok)
                break
    if out.get('oracle_dump_bytes') and out.get('frames'):
        out['bytes_per_frame'] = (out['oracle_dump_bytes']
                                  / float(out['frames']))
    return out


def fmt(x, nd=3):
    try:
        if x != x:            # NaN
            return '-'
        return '%.*f' % (nd, x)
    except (TypeError, ValueError):
        return str(x)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cap = sys.argv[1]
    legs = sorted(d for d in os.listdir(cap) if d.startswith('leg_'))
    if not legs:
        sys.exit('no legs in %s' % cap)
    res = [analyse_leg(os.path.join(cap, d)) for d in legs]

    print('=== capture: %s' % cap)
    print()

    # ---- gate 2 FIRST: did the intervention change its own mechanism?
    print('=== DID THE FEATURE APPLY? (this decides whether any rate '
          'below means anything) ===')
    print()
    print('  Each row is one leg. "chroma frames" is how many frames '
          'carried the')
    print('  aux (chroma) view; "luma only" is how many shipped the luma '
          'view alone.')
    print('  "refresh_ms on the wire" is chroma_refresh_ms read out of '
          'the running')
    print('  binary\'s own trace records, not out of the config file.')
    print()
    hdr = ('  %-6s %-4s %9s %9s %9s %10s'
           % ('leg', 'cfg', 'chroma', 'luma-only', 'share', 'refresh_ms'))
    print(hdr)
    print('  ' + '-' * (len(hdr) - 2))
    bad = []
    red = []
    for r in res:
        cfg = 'ON' if 'on' in r.get('body', '') else 'OFF'
        tot = r['chroma_frames'] + r['luma_only_frames']
        share = (100.0 * r['chroma_frames'] / tot) if tot else float('nan')
        print('  %-6s %-4s %9d %9d %8s%% %10s'
              % (r['dir'].replace('leg_', ''), cfg, r['chroma_frames'],
                 r['luma_only_frames'], fmt(share, 1),
                 ','.join(str(x) for x in r['refresh_ms_on_wire'])
                 or 'no records'))
        r['cfg'] = cfg
        if cfg == 'ON' and r['luma_only_frames'] == 0:
            bad.append('%s: ON leg skipped NOTHING -- the feature did '
                       'not apply' % r['dir'])
        if cfg == 'OFF' and r['luma_only_frames'] != 0:
            bad.append('%s: OFF leg skipped %d frames -- the wrong '
                       'config was live'
                       % (r['dir'], r['luma_only_frames']))
    print()

    # ---- the guarantee, which is the user-facing requirement
    print('=== THE GUARANTEE: how long did chroma ever go missing? ===')
    print()
    print('  The requirement is "chroma detail is restored at least '
          'every')
    print('  chroma_refresh_ms, whatever the screen is doing". These are '
          'the')
    print('  wall-clock gaps between consecutive frames that carried '
          'chroma.')
    print('  A max above the configured bound is a RED result whatever '
          'the rate says.')
    print()
    print('  %-6s %-4s %8s %9s %9s %9s %9s'
          % ('leg', 'cfg', 'gaps', 'mean ms', 'p50', 'p99', 'MAX'))
    print('  ' + '-' * 60)
    for r in res:
        print('  %-6s %-4s %8d %9s %9s %9s %9s'
              % (r['dir'].replace('leg_', ''), r['cfg'], r['chroma_gaps'],
                 fmt(r['chroma_gap_ms_mean'], 1),
                 fmt(r['chroma_gap_ms_p50'], 1),
                 fmt(r['chroma_gap_ms_p99'], 1),
                 fmt(r['chroma_gap_ms_max'], 1)))
        bound = max(r['refresh_ms_on_wire'] or [0])
        if r['cfg'] == 'ON' and bound > 0 \
                and r['chroma_gap_ms_max'] == r['chroma_gap_ms_max'] \
                and r['chroma_gap_ms_max'] > bound:
            red.append('%s: chroma went missing for %.1f ms against a '
                       '%d ms guarantee -- %.1f ms over, which is about '
                       'one frame interval (%s ms), because the decision '
                       'exists only AT a frame and the first frame at or '
                       'after the bound is the earliest chroma can go'
                       % (r['dir'], r['chroma_gap_ms_max'], bound,
                          r['chroma_gap_ms_max'] - bound,
                          fmt(r['frame_ms_mean'], 1)))
    print()

    print('=== HOW MANY ENCODER CHILDREN THE PUMP ARMED PER CYCLE ===')
    print()
    print('  Two children per damaged monitor is the normal case (luma '
          'and chroma);')
    print('  one means that cycle skipped chroma and did not wait for '
          'that child.')
    print('  This is the same skip seen from the other side of the '
          'mechanism.')
    print()
    for r in res:
        print('  %-6s %-4s %s' % (r['dir'].replace('leg_', ''), r['cfg'],
                                  r['kids_armed_histogram']))
    print()

    if bad:
        print('=== MECHANISM CHECK FAILED -- NO RATE IS REPORTED ===')
        print()
        print('  The feature did not do what it exists to do on at '
              'least one leg,')
        print('  so nothing downstream of it can be attributed to it.')
        for b in bad:
            print('  * ' + b)
        return 1

    if red:
        # A DIFFERENT CLASS OF PROBLEM, and the distinction is
        # deliberate. "The feature did not apply" makes every number
        # below meaningless and suppresses them. "The feature applied
        # and then missed its own bound" is a real defect in the
        # feature, but the rate it produced is still a true measurement
        # of what it did -- so it is printed LOUDLY and FIRST, and the
        # numbers follow.
        print('=== RED: THE GUARANTEE WAS EXCEEDED ===')
        print()
        for b in red:
            print('  * ' + b)
        print()
        print('  The rate below is still reported: it is what this '
              'build actually did.')
        print('  It is not evidence that the bound is met.')
        print()

    print('=== THE RATE ===')
    print()
    print('  "frame interval" is the wall-clock gap between consecutive '
          'FRAMES')
    print('  handed to the transport (one egress record each) -- the '
          'same quantity')
    print('  e_gate_run.sh prints as its send-to-send gap, and it '
          'agrees with it')
    print('  to within 0.2 ms. "period" is pump_beg to pump_beg, one '
          'encoder worker')
    print('  cycle. "pump" is the part of that cycle spent waiting for '
          'the ffmpeg')
    print('  children to finish encoding -- the segment a second encode '
          'is paid in.')
    print('  "writes/frame" is how many network writes one frame took: '
          'a luma-only')
    print('  frame carries one PDU where a full frame carries two.')
    print()
    print('  %-4s %-4s %7s %8s %8s %8s %8s %9s %8s %7s'
          % ('leg', 'cfg', 'frames', 'mean ms', 'p50', 'p90', 'p99',
             'period', 'pump', 'wr/fr'))
    print('  ' + '-' * 82)
    for r in res:
        print('  %-4s %-4s %7d %8s %8s %8s %8s %9s %8s %7s'
              % (r['dir'].replace('leg_', ''), r['cfg'], r['frames'],
                 fmt(r['frame_ms_mean']), fmt(r['frame_ms_p50']),
                 fmt(r['frame_ms_p90']), fmt(r['frame_ms_p99']),
                 fmt(r['period_ms_mean']), fmt(r['pump_ms_mean']),
                 fmt(r['writes_per_frame'], 2)))
    print()

    off = [r for r in res if r['cfg'] == 'OFF']
    on = [r for r in res if r['cfg'] == 'ON']
    if not (off and on):
        return 0

    def avg(rs, k):
        vals = [r[k] for r in rs if r.get(k) is not None]
        return mean(vals)

    o = avg(off, 'frame_ms_mean')
    n = avg(on, 'frame_ms_mean')
    print('=== WHAT IT BOUGHT ===')
    print()

    # A CONDITION WHOSE OWN TWO LEGS DISAGREE HAS NO MEAN WORTH TAKING.
    # Added 2026-08-08 after this script computed "1.343x" from a
    # control condition whose legs were 17.9 and 27.7 ms -- a 55 %
    # spread. The ratio was an artefact of one sick leg and would have
    # been reported as the treatment's effect. The interleave exists to
    # BRACKET drift; when the bracket is this wide it has caught
    # something, and averaging across it hides exactly what it caught.
    spread_limit = 15.0
    disagree = []
    for name, rs in (('control', off), ('treatment', on)):
        vals = [r['frame_ms_mean'] for r in rs]
        if len(vals) > 1 and min(vals) > 0:
            spread = 100.0 * (max(vals) - min(vals)) / min(vals)
            if spread > spread_limit:
                disagree.append('%s: legs %s ms differ by %.0f %%'
                                % (name,
                                   ' and '.join(fmt(v, 1) for v in vals),
                                   spread))
    if disagree:
        print('  *** NO RATIO IS REPORTED. A condition\'s own two legs '
              'disagree by more')
        print('  *** than %.0f %%, so its mean describes neither of '
              'them:' % spread_limit)
        for dline in disagree:
            print('  ***   ' + dline)
        print()
        print('  The per-leg numbers are in the table above and the '
              'cycle decomposition')
        print('  below; read the disagreeing legs there and find out '
              'what happened to')
        print('  the slow one before comparing anything.')
        print()
        print('  Per-leg frame interval: control %s | treatment %s'
              % (' / '.join(fmt(r['frame_ms_mean']) for r in off),
                 ' / '.join(fmt(r['frame_ms_mean']) for r in on)))
        print()
        bo2 = avg(off, 'bytes_per_frame')
        bn2 = avg(on, 'bytes_per_frame')
        if bo2 == bo2 and bn2 == bn2:
            print('  Bytes per frame are reported anyway, because they '
                  'are a property of')
            print('  what was ENCODED rather than of how fast the host '
                  'was: control %s MB'
                  % fmt(bo2 / 1e6))
            print('  against treatment %s MB, a %s %% reduction; the '
                  'per-leg spread there is'
                  % (fmt(bn2 / 1e6), fmt(100.0 * (1.0 - bn2 / bo2), 1)))
            print('  control %s | treatment %s.'
                  % (' / '.join(fmt(r['bytes_per_frame'] / 1e6)
                                for r in off),
                     ' / '.join(fmt(r['bytes_per_frame'] / 1e6)
                                for r in on)))
        return 0

    print('  Every figure is the mean of that condition\'s TWO '
          'interleaved legs, so')
    print('  host drift across the sitting is bracketed rather than '
          'folded in.')
    print()
    print('  frame interval')
    print('    chroma every frame (control) : %s ms  (%s frames/s)'
          % (fmt(o), fmt(1000.0 / o, 1)))
    print('    chroma only when settled     : %s ms  (%s frames/s)'
          % (fmt(n), fmt(1000.0 / n, 1)))
    print('    ratio                        : %sx' % fmt(o / n))
    print('    per-leg spread: control %s | treatment %s'
          % (' / '.join(fmt(r['frame_ms_mean']) for r in off),
             ' / '.join(fmt(r['frame_ms_mean']) for r in on)))
    print()
    po = avg(off, 'pump_ms_mean')
    pn = avg(on, 'pump_ms_mean')
    print('  wait for the ffmpeg children to encode a frame')
    print('    control %s ms -> treatment %s ms  (%sx)'
          % (fmt(po), fmt(pn), fmt(po / pn) if pn else '-'))
    print()
    print('  WHY -- the two encodes overlap, so the chroma one was '
          'never on the')
    print('  critical path. A child cannot start until its whole raw '
          'picture has')
    print('  arrived, so its encode window is [feedend, outfirst]; '
          'these are those')
    print('  windows, paired by child and sequence number.')
    print()
    print('    %-4s %-4s %11s %11s %11s %9s'
          % ('leg', 'cfg', 'luma enc', 'chroma enc', 'overlap',
             'luma on'))
    print('    ' + '-' * 60)
    for r in res:
        print('    %-4s %-4s %8s ms %8s ms %6s ms %4s%% %6s ms'
              % (r['dir'].replace('leg_', ''), r['cfg'],
                 fmt(r['main_encode_ms'], 2), fmt(r['aux_encode_ms'], 2),
                 fmt(r['overlap_ms'], 2), fmt(r['overlap_pct'], 1),
                 fmt(r['main_after_aux_ms'], 2)))
    print()
    print('    "overlap" is how much of the chroma encode ran at the '
          'same time as')
    print('    the luma one; "luma on" is how long the luma encode ran '
          'after the')
    print('    chroma one had finished -- which is all that deleting '
          'the chroma')
    print('    encode could ever have saved from the wait.')
    print()
    print('=== WHERE THE WHOLE CYCLE GOES, and whether there is idle to '
          'reclaim ===')
    print()
    print('  The four segments are built to SUM to the cycle, and the '
          'residual is')
    print('  printed so a decomposition that does not close cannot be '
          'read as one.')
    print('    feed     the raw picture going into the child\'s 1 MiB '
          'pipe. xrdp\'s')
    print('             side is vmsplice -- page references, near-free '
          '-- so this')
    print('             elapsed time is the CHILD copying it in, not '
          'xrdp waiting.')
    print('    encode   the child holds a whole picture and produces '
          'the first')
    print('             encoded byte.')
    print('    drain    xrdp reading the rest of the encoded frame.')
    print('    between  collect, the reference rewrite, emit, slot '
          'release.')
    print()
    print('  %-4s %-4s %8s %8s %8s %9s %8s %8s %9s'
          % ('leg', 'cfg', 'feed', 'encode', 'drain', 'between', 'sum',
             'cycle', 'residual'))
    print('  ' + '-' * 76)
    for r in res:
        tot = (r['seg_feed_ms'] + r['seg_encode_ms'] + r['seg_drain_ms']
               + r['seg_between_ms'])
        print('  %-4s %-4s %8s %8s %8s %9s %8s %8s %9s'
              % (r['dir'].replace('leg_', ''), r['cfg'],
                 fmt(r['seg_feed_ms']), fmt(r['seg_encode_ms']),
                 fmt(r['seg_drain_ms']), fmt(r['seg_between_ms']),
                 fmt(tot), fmt(r['seg_cycle_ms']),
                 fmt(r['seg_residual_ms'])))
    print()
    print('  Times the encoder worker had NOTHING TO ENCODE. If this is '
          'not ~zero,')
    print('  the producer is the limit and nothing downstream of it '
          'matters. Counted')
    print('  above a threshold rather than averaged: the wait before the '
          'payload')
    print('  starts drawing is seconds long and would swamp any mean.')
    print()
    for r in res:
        print('    %-4s %-4s %d waits, %d of them over 1 ms; p50 %s ms, '
              'p99 %s ms'
              % (r['dir'].replace('leg_', ''), r['cfg'], r['waits'],
                 r['waits_over_1ms'], fmt(r['wait_p50_ms'], 4),
                 fmt(r['wait_p99_ms'], 4)))
    print()
    fl = [max(r['seg_feed_ms'], r['seg_encode_ms']) + r['seg_drain_ms']
          + r['seg_between_ms'] for r in res]
    cy = [r['seg_cycle_ms'] for r in res]
    print('  WHICH FLOW-CONTROL TERM ACTUALLY HOLDS THE PRODUCER BACK')
    print()
    print('  The producer captures only when xrdp grants it a frame id, '
          'and that')
    print('  credit is the smallest of three things: the frame the '
          'encoder children')
    print('  have absorbed, one past the last frame that left for the '
          'transport')
    print('  ("one frame of pipeline inventory"), and the last frame the '
          'client')
    print('  acknowledged plus the configured wire window C. Ties are '
          'counted AS')
    print('  ties: if two terms are equal, relaxing either one alone '
          'moves nothing.')
    print()
    for r in res:
        print('    %-4s %-4s C=%s  %s'
              % (r['dir'].replace('leg_', ''), r['cfg'],
                 ','.join(str(x) for x in r['wire_window']),
                 r['credit_binder']))
    print()
    print('    headroom the wire window had above the binding value '
          '(frames):')
    for r in res:
        print('      %-4s %-4s %s' % (r['dir'].replace('leg_', ''),
                                      r['cfg'], r['wire_headroom']))
    print()
    print('  ARITHMETIC, NOT A MEASUREMENT: if the feed of the next '
          'picture ran')
    print('  entirely concurrently with the encode of this one, the '
          'cycle floor')
    print('  would be max(feed, encode) + drain + between = %s ms '
          'against the'
          % ' / '.join(fmt(x, 1) for x in fl))
    print('  measured %s ms. That is the whole prize available to any '
          'amount of'
          % ' / '.join(fmt(x, 1) for x in cy))
    print('  pipelining, and it is bounded below by the encode alone.')
    print()

    bo = avg(off, 'bytes_per_frame')
    bn = avg(on, 'bytes_per_frame')
    if bo == bo and bn == bn:
        print('  BYTES PER FRAME on the wire (the oracle dump, which '
              'saves every')
        print('  encoded payload the client received, divided by the '
              'frames in the leg)')
        print('    chroma every frame (control) : %s MB/frame'
              % fmt(bo / 1e6))
        print('    chroma only when settled     : %s MB/frame'
              % fmt(bn / 1e6))
        print('    reduction                    : %s %%'
              % fmt(100.0 * (1.0 - bn / bo), 1))
        print('    per-leg spread: control %s | treatment %s'
              % (' / '.join(fmt(r['bytes_per_frame'] / 1e6) for r in off),
                 ' / '.join(fmt(r['bytes_per_frame'] / 1e6) for r in on)))
    return 0


if __name__ == '__main__':
    sys.exit(main())

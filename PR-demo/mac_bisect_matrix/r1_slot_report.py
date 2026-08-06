#!/usr/bin/env python3
"""Turn a captured R1SLOT log into the recon-gate R1 verdict.

Input: the lines produced by the recon xorgxrdp build, one per AVC444
send (see r1_slot_recon.sh for why they exist):

    R1SLOT mon 0 shmem_offset 0 rect_id 41 rect_id_ack 40 max_outstanding 2

FR-CAPTURE-8 gives the capture loop two shmem slots per monitor so a
monitor's frame N+1 can be captured while frame N is still being
encoded. The slot is chosen globally as (rect_id + 1) & 1 and rect_id
advances once per SEND, so what a monitor actually gets depends on how
many monitors send in a pass. R1 asks whether that leaves the mechanism
working per monitor at monitorCount = 2. Three things decide it, and all
three are measured here rather than argued:

  1. Does a monitor reuse the same slot on consecutive sends? Split by
     pass shape: a FULL pass (rect_id gap == number of monitors seen)
     means every monitor sent; a PARTIAL pass means it did not.
  2. Did a monitor ever use its second slot at all?
  3. How often did a monitor hold TWO OUTSTANDING frames in TWO
     DIFFERENT slots — the event the two-slot design exists to allow?
     That needs the previous send of the same monitor to be both in a
     different slot AND still unacked (rect_id_ack is cumulative, so
     "unacked" is prev_rect_id > rect_id_ack at the later send).

Everything reported is a property of THIS run at THIS monitor count.
Nothing is claimed about odd monitor counts; they were not run.
"""
import collections
import re
import sys

REC = re.compile(
    r"R1SLOT mon (\d+) shmem_offset (\d+) rect_id (\d+) "
    r"rect_id_ack (-?\d+) max_outstanding (\d+)")


def main(path):
    rows = []
    with open(path, errors="replace") as fp:
        for line in fp:
            m = REC.search(line)
            if m:
                rows.append(tuple(int(g) for g in m.groups()))
    if not rows:
        print("RED: no parsable R1SLOT records")
        return 1

    by_mon = collections.OrderedDict()
    for mon, off, rid, ack, _mx in rows:
        by_mon.setdefault(mon, []).append((off, rid, ack))
    mons = sorted(by_mon)

    print("sends            : %d" % len(rows))
    print("monitors seen    : %s" % ", ".join(str(m) for m in mons))
    print("max_outstanding  : %s"
          % ", ".join(sorted({str(r[4]) for r in rows})))
    print("rect_id range    : %d .. %d" % (rows[0][2], rows[-1][2]))
    depth = collections.Counter(rid - ack for _, _, rid, ack, _ in rows)
    print("outstanding depth at send: %s" % dict(sorted(depth.items())))
    print()

    if len(mons) < 2:
        print("RED: only %d monitor(s) sent — this run is NOT the m >= 2 case "
              "R1 is about. Re-run with content that damages every monitor."
              % len(mons))
        return 1

    m = len(mons)
    full_same = full_diff = part_same = part_diff = 0
    pipelined = 0
    for mon in mons:
        seq = by_mon[mon]
        offs = [o for o, _, _ in seq]
        distinct = sorted(set(offs))
        f_same = f_diff = p_same = p_diff = 0
        mon_pipe = 0
        for (o0, r0, _a0), (o1, r1, a1) in zip(seq, seq[1:]):
            same = (o0 == o1)
            if r1 - r0 == m:
                f_same, f_diff = f_same + same, f_diff + (not same)
            else:
                p_same, p_diff = p_same + same, p_diff + (not same)
            if not same and r0 > a1:
                mon_pipe += 1
        full_same += f_same
        full_diff += f_diff
        part_same += p_same
        part_diff += p_diff
        pipelined += mon_pipe
        print("monitor %d: %5d sends, slots used %s" % (mon, len(seq),
                                                        distinct))
        print("           full passes  (gap %d): same slot %4d, changed %4d"
              % (m, f_same, f_diff))
        print("           partial pass (gap != %d): same slot %4d, changed %4d"
              % (m, p_same, p_diff))
        print("           two outstanding frames in two slots: %d" % mon_pipe)
    print()

    print("ACROSS ALL MONITORS")
    print("  full-pass consecutive sends : %d, of which slot changed %d"
          % (full_same + full_diff, full_diff))
    print("  partial-pass               : %d, of which slot changed %d"
          % (part_same + part_diff, part_diff))
    print("  per-monitor two-slot pipelining events: %d of %d sends (%.2f%%)"
          % (pipelined, len(rows), 100.0 * pipelined / len(rows)))
    print()

    single_slot = [mon for mon in mons
                   if len({o for o, _, _ in by_mon[mon]}) == 1]
    if full_diff == 0 and pipelined * 100 < len(rows):
        print("R1 VERDICT: CONFIRMED — at monitorCount = %d the global "
              "rect_id parity pins each monitor to one slot on EVERY "
              "full pass (%d/%d, zero exceptions); the only slot changes "
              "come from partial passes, where a monitor sends without "
              "the other(s). FR-CAPTURE-8's two-slot pipelining fired for "
              "a monitor %d time(s) in %d sends (%.2f%%) — it is inert per "
              "monitor at this monitor count, and the overlap the session "
              "does get is cross-monitor. Monitors that never left one "
              "slot: %s. Step 6c (per-monitor slot index) may proceed on "
              "this basis (BACKLOG #45 recon gate R1)."
              % (m, full_same, full_same + full_diff, pipelined, len(rows),
                 100.0 * pipelined / len(rows),
                 ", ".join(str(x) for x in single_slot) or "none"))
        return 0
    if full_diff > 0 and pipelined * 10 > len(rows):
        print("R1 VERDICT: REFUTED — monitors change slots on full passes "
              "(%d of %d) and per-monitor two-slot pipelining fired %d "
              "times in %d sends. The source-derived pinning argument does "
              "not hold at monitorCount = %d; the measurement wins. "
              "Re-examine the rationale for step 6c before writing code "
              "(BACKLOG #45 R1)."
              % (full_diff, full_same + full_diff, pipelined, len(rows), m))
        return 0
    print("R1 VERDICT: PARTIAL — full-pass slot changes %d/%d and %d "
          "pipelining events in %d sends fit neither a clean confirmation "
          "nor a refutation. Report the per-monitor rows above and find "
          "what distinguishes them before step 6c (BACKLOG #45 R1)."
          % (full_diff, full_same + full_diff, pipelined, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "-"))

#!/usr/bin/env python3
"""i61g_sched_sampler.py -- why is a thread not making progress?

BACKLOG #61g Step 1.  During a gate run the oracle client stops draining
the RDP socket for 50-150 ms at a time, and #61f showed those pauses echo
back into the server's capture clock.  Three different things look the
same from outside the process:

  (a) the kernel wanted to run the thread but no CPU was free
      -> schedstat field 2 (run-delay) grows while the thread is R
  (b) the thread was running and simply took that long
      -> schedstat field 1 (time on CPU) grows, state R
  (c) the thread was asleep -- waiting on a socket, a lock, a page, an
      ioctl -- and the pause is internal to the client
      -> neither counter grows; state is S (sleep) or D (uninterruptible)

/proc/<tid>/schedstat carries exactly those two nanosecond counters plus
the number of timeslices, and /proc/<tid>/stat carries the state letter,
so sampling both at 100 Hz separates (a) from (b) from (c) without a
tracer, without root inside the pod, and without touching any binary.

Container processes are ordinary host processes in the host's /proc, and
k3s does not use a time namespace, so this samples the pod's xrdp and
session Xorg from the host on the SAME CLOCK_MONOTONIC that xrdp's
ACK_TRACE `us=` timestamps use.  That is what lets the samples be joined
to the frame chain afterwards with no clock fitting.

Usage:
  i61g_sched_sampler.py --secs 60 --out sched.csv \\
      client=/opt/freerdp-vaapi/bin/xfreerdp xrdp=/usr/sbin/xrdp

Each target is `label=pattern`; the pattern is matched against the full
command line (pgrep -f).  Threads are re-enumerated once a second, so a
thread that starts mid-run is picked up.

Output CSV, one row per thread per sample:
  us,label,pid,tid,comm,state,cpu_ns,wait_ns,slices
`us` is CLOCK_MONOTONIC microseconds.  The counters are cumulative --
take differences between consecutive samples of the same tid.
"""
import argparse
import os
import subprocess
import sys
import time


def resolve(pattern):
    """pids for one target spec.

    `@cgroup:<substr>` selects every process whose /proc/<pid>/cgroup
    names <substr> -- that is how a pod's processes are picked out from
    the host, where a bare `pgrep -f /usr/sbin/xrdp` would match every
    arm in the fleet at once.  Anything else is a pgrep -f pattern.
    """
    if pattern.startswith("@cgroup:"):
        want = pattern[len("@cgroup:"):]
        pids = []
        for e in os.listdir("/proc"):
            if not e.isdigit():
                continue
            cg = read("/proc/%s/cgroup" % e)
            if cg and want in cg:
                pids.append(int(e))
        return pids
    try:
        out = subprocess.run(["pgrep", "-f", pattern], capture_output=True,
                             text=True, timeout=10).stdout
    except (subprocess.SubprocessError, OSError):
        return []
    # NEVER sample the sampler. The target patterns are this process's own
    # arguments, so `pgrep -f <pattern>` matches it -- which on the first
    # run (2026-08-01) put the sampler's own 36%-of-a-core into the
    # client's CPU total and would have been read as client work.
    me = os.getpid()
    return [int(x) for x in out.split() if int(x) != me]


def threads(pid):
    try:
        return os.listdir("/proc/%d/task" % pid)
    except OSError:
        return []


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except (OSError, IOError):
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=float, default=60.0)
    ap.add_argument("--hz", type=float, default=100.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("targets", nargs="+", help="label=pgrep-pattern")
    a = ap.parse_args()

    spec = []
    for t in a.targets:
        if "=" not in t:
            sys.exit("target %r is not label=pattern" % t)
        label, pat = t.split("=", 1)
        pids = resolve(pat)
        if not pids:
            sys.stderr.write("i61g_sched_sampler: no process matches %r "
                             "for label %s -- that target is MISSING from "
                             "this run, do not read its absence as a "
                             "measurement\n" % (pat, label))
        spec.append((label, pat, pids))
    sys.stderr.write("i61g_sched_sampler: %s\n"
                     % "; ".join("%s=%s" % (l, p) for l, _, p in spec))

    period = 1.0 / a.hz
    end = time.monotonic() + a.secs
    next_enum = 0.0
    allt = []          # every thread of every target, refreshed at 1 Hz
    tids = []          # the ACTIVE subset, sampled at --hz
    lastcpu = {}
    comm = {}
    rows = []
    while True:
        now = time.monotonic()
        if now >= end:
            break
        if now >= next_enum:
            next_enum = now + 1.0
            # re-resolve every second: a session process the run itself
            # creates (the pod's Xorg, an ffmpeg child) must not be
            # missing from the sample just because it did not exist when
            # the sampler started.
            #
            # Then sample at --hz only the threads that BURNED CPU in the
            # last second. The oracle client carries ~170 threads of which
            # three do the work; sampling all of them at 100 Hz cost 36 %
            # of a core on the first run and moved the rate it was
            # measuring. An idle thread is re-checked once a second, so it
            # rejoins the fast set within 1 s of waking up -- state a
            # thread only enters at a discontinuity, never mid-pause.
            allt = []
            for label, pat, pids in spec:
                pids[:] = resolve(pat)
                for pid in pids:
                    for tid in threads(pid):
                        allt.append((label, pid, int(tid)))
            tids = []
            for label, pid, tid in allt:
                ss = read("/proc/%d/task/%d/schedstat" % (pid, tid))
                if ss is None:
                    continue
                cpu = int(ss.split()[0])
                if cpu != lastcpu.get(tid):
                    tids.append((label, pid, tid))
                lastcpu[tid] = cpu
        us = int(time.clock_gettime(time.CLOCK_MONOTONIC) * 1e6)
        for label, pid, tid in tids:
            ss = read("/proc/%d/task/%d/schedstat" % (pid, tid))
            if ss is None:
                continue
            f = ss.split()
            if len(f) < 3:
                continue
            if tid not in comm:
                st = read("/proc/%d/task/%d/comm" % (pid, tid))
                comm[tid] = (st or "?").strip()
            stat = read("/proc/%d/task/%d/stat" % (pid, tid))
            state = "?"
            if stat:
                # comm is parenthesised and may contain spaces
                state = stat[stat.rfind(")") + 2:stat.rfind(")") + 3]
            rows.append("%d,%s,%d,%d,%s,%s,%s,%s,%s"
                        % (us, label, pid, tid, comm[tid], state,
                           f[0], f[1], f[2]))
        slack = period - (time.monotonic() - now)
        if slack > 0:
            time.sleep(slack)

    with open(a.out, "w") as f:
        f.write("us,label,pid,tid,comm,state,cpu_ns,wait_ns,slices\n")
        f.write("\n".join(rows))
        f.write("\n")
    sys.stderr.write("i61g_sched_sampler: %d rows -> %s\n"
                     % (len(rows), a.out))


main()

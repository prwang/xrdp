#!/usr/bin/env python3
"""
Offline reproduction of the stock-ffmpeg persistent-pipe latency that shaped
the AVC444 runner design (see BACKLOG.md "CRITICAL FINDING" and PRD sections
8.6/11). This is a developer diagnostic, NOT a unit test -- it is not wired
into `make check`; run it by hand against a real ffmpeg.

What it demonstrates
--------------------
xrdp feeds the ffmpeg child one raw NV12 picture at a time over a persistent
pipe (fd 3) and reads NUT/H.264 from stdout, WITHOUT ever closing the input
during a session. Against that usage, stock ffmpeg:

  (1) emits ZERO output until several input pictures have been buffered
      (the "first output after N inputs" latency), and
  (2) never flushes the final buffered picture(s) until the input hits EOF.

This script measures both, for the production command and for a sweep of
candidate low-latency flag combinations, so the numbers are reproducible on
any box rather than asserted from memory.

Usage
-----
    ./repro_ffmpeg_latency.py [--ffmpeg /usr/bin/ffmpeg] [--width W --height H]
                              [--frames N] [--only NAME]

Exit status is 0 if it ran; the finding is the printed table, not a pass/fail.

Method
------
For each config we spawn ffmpeg, then loop: write exactly one raw frame to the
input pipe, flush, and poll stdout for up to a short quiescence window. We
record the input-frame index at which the first byte of output appears
("first_out_after"), and the total output bytes seen while the input is still
open. Then we close the input (EOF) and drain the remainder, recording how
many bytes only appeared post-EOF ("tail_after_eof"). A config that streams
with no latency shows first_out_after == 1 and tail_after_eof == 0.
"""

import argparse
import os
import select
import subprocess
import sys
import time


def build_cmd(ffmpeg, width, height, extra_in, extra_enc):
    """Production-shaped command with per-config extra input/encoder flags."""
    cmd = [ffmpeg, "-hide_banner", "-nostats", "-loglevel", "error"]
    cmd += extra_in
    cmd += ["-f", "rawvideo", "-pix_fmt", "nv12",
            "-s", "%dx%d" % (width, height), "-r", "60", "-i", "pipe:3"]
    cmd += ["-c:v", "libx264", "-crf", "18", "-g", "240"]
    cmd += extra_enc
    cmd += ["-flush_packets", "1", "-f", "nut", "pipe:1"]
    return cmd


# NV12 frame size for the given dims (Y plane + interleaved half-res UV).
def frame_size(width, height):
    return width * height + width * (height // 2)


def make_frame(width, height, idx):
    """A distinguishable NV12 frame; content varies per index."""
    y = bytes(((i + idx * 37) & 0xff) for i in range(width * height))
    uv = bytes((0x80 for _ in range(width * (height // 2))))
    return y + uv


def drain(fd, quiescence_s, hard_deadline_s):
    """Read all currently-available bytes from fd until it stays quiet for
    quiescence_s or hard_deadline_s elapses. Returns bytes read."""
    out = bytearray()
    last = time.monotonic()
    while True:
        now = time.monotonic()
        if now - last >= quiescence_s:
            break
        if now >= hard_deadline_s:
            break
        r, _, _ = select.select([fd], [], [], quiescence_s)
        if not r:
            continue
        try:
            chunk = os.read(fd, 1 << 16)
        except BlockingIOError:
            continue
        if chunk:
            out += chunk
            last = time.monotonic()
    return bytes(out)


def run_config(name, ffmpeg, width, height, frames, extra_in, extra_enc):
    fsz = frame_size(width, height)
    # input on fd 3 (a dedicated pipe), NUT on stdout, stderr passthrough
    in_r, in_w = os.pipe()
    cmd = build_cmd(ffmpeg, width, height, extra_in, extra_enc)
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            pass_fds=(in_r,),
            # remap child fd 3 -> read end of our input pipe
            preexec_fn=(lambda: os.dup2(in_r, 3)))
    except FileNotFoundError:
        return {"name": name, "error": "ffmpeg not found"}
    os.close(in_r)
    out_fd = proc.stdout.fileno()
    os.set_blocking(out_fd, False)

    first_out_after = None
    bytes_before_eof = 0
    per_frame = []
    for i in range(frames):
        os.write(in_w, make_frame(width, height, i))
        got = drain(out_fd, quiescence_s=0.15,
                    hard_deadline_s=time.monotonic() + 1.0)
        per_frame.append(len(got))
        bytes_before_eof += len(got)
        if got and first_out_after is None:
            first_out_after = i + 1

    # EOF: close input, drain the tail the persistent pipe withheld
    os.close(in_w)
    tail = drain(out_fd, quiescence_s=0.3,
                 hard_deadline_s=time.monotonic() + 5.0)
    tail_after_eof = len(tail)

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    stderr = proc.stderr.read().decode("utf-8", "replace").strip()
    proc.stdout.close()
    proc.stderr.close()

    return {
        "name": name,
        "first_out_after": first_out_after,
        "bytes_before_eof": bytes_before_eof,
        "tail_after_eof": tail_after_eof,
        "per_frame": per_frame,
        "frame_size": fsz,
        "stderr": stderr,
    }


# Candidate configurations. Each targets INPUT-side buffering, ENCODER-side
# delay, or both. The runner ships the "production" row; the others document
# why the obvious low-latency knobs were rejected (they break or corrupt).
CONFIGS = [
    # name,                 extra_in,                     extra_enc
    ("production",          [],                           []),
    ("tune_zerolatency",    [],                           ["-tune", "zerolatency"]),
    ("x264_nodelay",        [],
     ["-x264-params", "sync-lookahead=0:rc-lookahead=0:bframes=0:"
      "sliced-threads=0"]),
    ("fflags_nobuffer",     ["-fflags", "nobuffer"],      []),
    ("probesize_32",        ["-probesize", "32"],         []),
    ("analyzeduration_0",   ["-analyzeduration", "0"],    []),
    ("avioflags_direct",    ["-avioflags", "direct"],     []),
    ("max_delay_0",         ["-max_delay", "0"],          ["-flags", "low_delay"]),
    ("combined_input",      ["-avioflags", "direct", "-analyzeduration", "0",
                             "-probesize", "1000000"],    ["-tune", "zerolatency"]),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ffmpeg", default=os.environ.get(
        "XRDP_TEST_FFMPEG_PATH", "/usr/bin/ffmpeg"))
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--frames", type=int, default=6)
    ap.add_argument("--only", default=None,
                    help="run only the named config")
    args = ap.parse_args()

    if not (os.path.isabs(args.ffmpeg) and os.path.exists(args.ffmpeg)):
        print("ffmpeg not found at %r; pass --ffmpeg or set "
              "XRDP_TEST_FFMPEG_PATH" % args.ffmpeg, file=sys.stderr)
        return 2

    print("ffmpeg: %s" % args.ffmpeg)
    print("frame: %dx%d NV12 (%d bytes), submitting %d frames one at a time\n"
          % (args.width, args.height,
             frame_size(args.width, args.height), args.frames))
    hdr = ("%-18s %14s %14s %14s   %s"
           % ("config", "first_out@", "bytes<EOF", "tail@EOF", "note"))
    print(hdr)
    print("-" * len(hdr))
    for name, extra_in, extra_enc in CONFIGS:
        if args.only and name != args.only:
            continue
        r = run_config(name, args.ffmpeg, args.width, args.height,
                       args.frames, extra_in, extra_enc)
        if r.get("error"):
            print("%-18s  %s" % (name, r["error"]))
            continue
        note = ""
        if r["first_out_after"] is None:
            note = "NO OUTPUT while input open"
        elif r["first_out_after"] > 1:
            note = "latency: %d frames buffered first" % (
                r["first_out_after"] - 1)
        else:
            note = "streams immediately"
        if r["stderr"]:
            firstline = r["stderr"].splitlines()[0][:40]
            note += " | stderr: %s" % firstline
        fo = "none" if r["first_out_after"] is None else r["first_out_after"]
        print("%-18s %14s %14d %14d   %s"
              % (name, str(fo), r["bytes_before_eof"],
                 r["tail_after_eof"], note))
        if args.only:
            print("   per-frame output bytes (index->bytes while input open): %s"
                  % r["per_frame"])
    print("\nInterpretation:")
    print("  first_out@ = input-frame index at which the 1st output byte "
          "appeared (1 == no latency).")
    print("  tail@EOF   = output bytes that only appeared AFTER closing the "
          "input (0 == nothing withheld).")
    print("  A config that both streams at frame 1 AND has tail@EOF 0 would "
          "let the synchronous")
    print("  write-pair/read-pair model work; the runner is pipelined "
          "because none of them do.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# #125B T4 sparse-chroma by wire-window matrix

## Verdict

PASS. Retain sparse chroma as an opt-in bandwidth/quality tradeoff. It reduced
the exact video-command bytes per delivered frame by 42.1% at a one-frame
wire window and 41.7% at a two-frame window. It also reduced the measured
encoder-pump-to-transport/credit latency by about one third. Dense remains the
quality-preserving default because sparse mode has the already documented
4:2:0/4:4:4 transition limitation under recurrent damage.

Do not claim the observed 34.8--35.4% frame-rate increase as server-ceiling
throughput. Every sparse repetition had a producer margin of only 0.96--0.98x
and 67--73 encoder waits longer than 1 ms for producer work. The payload, not
the server pipeline alone, limited those arms.

The two-frame window was genuinely exercised: its four traces contain 4, 13,
20 and 26 credit-frontier advances which a one-frame window could not admit.
Despite that, widening the window changed throughput by -0.3% dense and -0.7%
sparse, and bytes per frame by -0.2% dense and +0.7% sparse. The evidence does
not justify changing the shipped one-frame default. The T4 is deliberately
left on sparse/window 2 for the owner's requested interactive follow-up.

## Exact run

The eight 20-second client lifetimes ran in `A B C D D C B A` order. Each used
one 3840x2400 monitor with modeline
`592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync`, the oracle
client, AVC444v2, auxiliary LTR, eager slot acknowledgement and full-screen
textflood. Visual autostarts were disabled during the matrix.

| condition | wire window | chroma refresh/idle |
|---|---:|---:|
| A | 1 | 0/0 ms (dense) |
| B | 1 | 1000/100 ms (sparse) |
| C | 2 | 0/0 ms (dense) |
| D | 2 | 1000/100 ms (sparse) |

The generated profiles have one common-content hash after removing only the
three treatment assignments. Their individual SHA-256 values are:

| profile | SHA-256 |
|---|---|
| dense/window 1 | `770a41f630b94ab081e25c5b733749a4ec0c5c8090d3760cf634595c087527ef` |
| sparse/window 1 | `080d1644b109b15a31e4a9e8d40aaca8757327094906b98fcd34ce2cff183714` |
| dense/window 2 | `b0585234306461194e5b11a9b8ab9422b9cd3e18ac0fa9f07a168b847fe54a5c` |
| sparse/window 2 | `437aa8302706ea4a259943487f48f59eca40cb234bddb9f80594a9a136093ded` |

Server: Tesla T4, driver 580.173.02, Ubuntu kernel 7.0.0-1009-aws and ffmpeg
8.0.1. xrdp was the clean package
`0.10.80+git20260824141733.4cf5063e05d3`, package SHA-256
`cb3e419242107e693e77c405ef5cb88ff4c6ecfe5da14e940a10bafa3a501660`.
xorgxrdp remained
`1:0.10.80+git20260822114312.c190343ff28a`. The xrdp commit adds only the
compile-time `video_cmd` trace identity needed for exact accounting; the four
runtime treatments are otherwise the same functional source.

The retained matrix ran after the GPU had been warmed, with persistence mode
still disabled. The first discarded cold probe took 8.513 seconds, while its
immediate warm repeat took 0.708 seconds; the cold result exceeded xrdp's
four-second hardware probe deadline. NVIDIA persistence mode was therefore
enabled after the matrix, as already required by the T4 deployment record, and
was enabled for the final two-resolution smoke test and interactive handoff.
The matrix-only systemd override which set the perf, GFX and acknowledgement
trace variables was then removed from service activation and archived under
the target's root-owned profile directory. The mandatory two-resolution smoke
was rerun and passed in that uninstrumented runtime state.

## Distributions

Intervals are between terminal transport-egress records of successive frame
identities. Throughput is their reciprocal over the measured span.

| leg | profile | frames | mean ms | p50 | p90 | p99 | frame/s | MB/video frame |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| A1 | dense W1 | 179 | 84.72 | 79.32 | 111.00 | 171.50 | 11.80 | 3.720 |
| B1 | sparse W1 | 241 | 62.73 | 54.21 | 93.83 | 165.53 | 15.94 | 2.155 |
| C1 | dense W2 | 173 | 87.97 | 83.45 | 112.32 | 152.65 | 11.37 | 3.730 |
| D1 | sparse W2 | 239 | 63.46 | 53.63 | 94.48 | 163.90 | 15.76 | 2.182 |
| D2 | sparse W2 | 239 | 63.54 | 55.24 | 92.07 | 171.73 | 15.74 | 2.163 |
| C2 | dense W2 | 182 | 83.38 | 80.07 | 109.53 | 148.63 | 11.99 | 3.717 |
| B2 | sparse W1 | 240 | 63.36 | 55.68 | 93.52 | 145.33 | 15.78 | 2.161 |
| A2 | dense W1 | 177 | 85.96 | 82.49 | 108.39 | 134.85 | 11.63 | 3.738 |

The within-profile mean-interval spreads are 1.5% (dense W1), 1.0% (sparse
W1), 5.5% (dense W2) and 0.1% (sparse W2), all below the predeclared 15%
limit. The p99 values are retained rather than collapsed into the mean; every
profile has a long tail.

## Exact command and byte closure

`video_cmd` is emitted at command construction with the echoed frame identity,
view 1/2 and exact command bytes. Pairing is by that identity, never by a time
window. One view-1 command is a main-only frame; view 1 followed by view 2 is a
paired frame.

| leg | main-only commands | paired commands | main-only bytes | paired bytes | total bytes | edge commands |
|---|---:|---:|---:|---:|---:|---:|
| A1 | 0 | 358 | 0 | 665,926,003 | 665,926,003 | 2 |
| B1 | 218 | 46 | 434,954,462 | 84,473,017 | 519,427,479 | 2 |
| C1 | 0 | 346 | 0 | 645,207,799 | 645,207,799 | 0 |
| D1 | 212 | 54 | 416,550,963 | 105,044,647 | 521,595,610 | 0 |
| D2 | 215 | 48 | 429,671,913 | 87,193,023 | 516,864,936 | 0 |
| C2 | 0 | 364 | 0 | 676,492,550 | 676,492,550 | 2 |
| B2 | 216 | 48 | 433,460,728 | 85,194,372 | 518,655,100 | 0 |
| A2 | 0 | 354 | 0 | 661,560,583 | 661,560,583 | 2 |

Every in-scope command and byte is classified and both sums close exactly.
An edge command has an explicit identity whose command was timestamped before
the measurement cutoff but whose terminal egress was after it. It is reported
but excluded from both in-window sums; no time-based reassignment is made.
Every sparse leg independently contains both omitted and restored auxiliary
work, and no chroma omission reaches or exceeds the configured 1000 ms bound.

## Latency decomposition

The table reports means in milliseconds. Feed ends after the main child's
complete raw picture enters its pipe. Wait-output ends at that same child and
sequence's first encoded byte. Drain ends when the shared child pump finishes.
Finish ends after the same frame identity reaches transport and releases slot
credit.

| leg | feed | wait for output | drain | finish transport/credit | full |
|---|---:|---:|---:|---:|---:|
| A1 | 10.12 | 54.66 | 12.71 | 37.67 | 115.16 |
| B1 | 5.90 | 46.85 | 3.78 | 21.75 | 78.27 |
| C1 | 12.04 | 58.33 | 11.42 | 37.64 | 119.43 |
| D1 | 6.45 | 46.09 | 3.03 | 21.80 | 77.38 |
| D2 | 6.44 | 46.06 | 3.19 | 21.23 | 76.92 |
| C2 | 10.15 | 52.61 | 13.64 | 37.65 | 114.05 |
| B2 | 6.57 | 45.68 | 3.35 | 22.31 | 77.91 |
| A2 | 10.74 | 55.71 | 13.35 | 38.16 | 117.96 |

There are zero negative segments. Every row closes to the full interval with
maximum absolute floating-point residual at or below
`1.14e-13` ms. The measured sparse reduction is distributed across feed,
encoded-output wait, drain and transport/credit finish; it is not a relabelled
single stage.

## Scientific checks and limitations

* Counts, command bytes, stage sums and rates were recomputed from the raw
  schema-1 records in `raw-distributions.json`; all close.
* The intervention applied: sparse traces show both decisions and window-2
  traces reach states impossible at window 1.
* The run obeys the dense default and time-only sparse policy in PRD slices
  141/142. No logger, sampler or sidecar was active.
* There is no apples-to-apples retained regression baseline: the older T4
  result used two monitors, codeflood and a different duration. No ratio to
  that series is made.
* The oracle acknowledges before decode. This is numerical server-path
  characterization, not real-client rendering evidence; Scope A supplies the
  separate Windows/macOS visual result.

## Red shakedowns retained as history

No number from the four preceding attempts is used. Three A1 attempts aborted
because the new SSH runner wrote a malformed payload marker or could not list
the root-only trace directory. The first also hit a cold NVENC probe timeout:
a direct first frame took 8.513 s and the immediate warm repeat 0.708 s. The
first complete eight-leg run then failed exact byte closure because the old
transport-fragment trace did not carry the current frame identity. That gap
caused the compile-time `video_cmd` event and the complete matrix was rerun.
The unqualified raw directories were removed after this record was written.

## Replay

From `/work`, with the same packages already installed and no Ubuntu X11
session active, first keep the NVIDIA driver resident so a cold first login
does not fail the four-second encoder probe:

```sh
ssh ubuntu@3.235.10.89 sudo nvidia-smi -pm 1
I125B_HOST=ubuntu@3.235.10.89 \
  bash PR-demo/mac_bisect_matrix/i125b_t4_matrix.sh 20
```

The command generates the four complete profiles, proves that only the three
treatment assignments differ, runs the fixed order, analyzes raw traces, and
leaves the server on sparse/window 2 with benchmark payloads disarmed.

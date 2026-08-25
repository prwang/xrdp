# #142 final local numerical replay

## Verdict

PASS. This is the retained clean-room `A B C D D C B A` replay: eight exact
20-second one-monitor 3840x2400 oracle-client legs using full-screen
textflood. All four deployed profiles were independently wire-certified
before timing. Apart from `wire_window`, `chroma_refresh_ms` and
`chroma_idle_ms`, their common-content SHA-256 is
`5f58ec6937fa2b21abe9e7a6441c0c779db7d8ee6737e4575f9371ccf7382502`.

Sparse chroma raised this end-to-end textflood workload's delivered-frame
rate by 8.3% at window 1 and 9.0% at window 2. It reduced exact classified
video-command bytes per delivered frame by 49.3% and 49.4%, respectively,
and reduced mean encoder-pump-start through terminal transport and slot-credit
completion from 32.6--33.2 ms to 18.8--19.5 ms.

These are producer-paced workload results, not unconstrained server ceilings.
Every leg's producer margin was 1.00x: the payload's own frame interval and
the delivered-frame interval were equal. The comparison is still valid
because payload, geometry, client and duration are identical, but this run
cannot establish whether pipeline stages would overlap with damage always
pending.

Window 2 changed delivered-frame rate by only +0.3% dense and +0.8% sparse,
and changed bytes per frame by +0.0% and -0.3%. It therefore supplies no
performance reason to change the shipped window-1 default in this local
one-monitor workload.

## Exact distributions

Throughput is terminal-frame count divided by the complete recorded
measurement window. The interval percentiles describe gaps between active
frames and are not used as the rate denominator.

| leg | profile | frames | frame/s | p50 ms | p90 ms | p99 ms | MB/frame | mean full latency ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| A1 | dense W1 | 547 | 27.35 | 36.70 | 41.49 | 44.61 | 2.500 | 32.57 |
| B1 | sparse W1 | 590 | 29.50 | 33.66 | 38.15 | 51.97 | 1.269 | 18.91 |
| C1 | dense W2 | 552 | 27.60 | 36.10 | 41.21 | 46.14 | 2.501 | 32.68 |
| D1 | sparse W2 | 594 | 29.70 | 33.57 | 37.86 | 55.60 | 1.267 | 19.27 |
| D2 | sparse W2 | 599 | 29.95 | 33.26 | 38.09 | 50.40 | 1.262 | 19.45 |
| C2 | dense W2 | 543 | 27.15 | 36.27 | 41.55 | 48.04 | 2.501 | 32.79 |
| B2 | sparse W1 | 593 | 29.65 | 33.52 | 37.43 | 56.09 | 1.268 | 18.80 |
| A2 | dense W1 | 545 | 27.25 | 36.68 | 40.62 | 45.32 | 2.500 | 33.16 |

## Mechanism and accounting checks

Sparse legs independently contained both auxiliary omission and restoration;
no omission reached the 1000 ms bound. Window-2 traces contained 221 and 205
dense, and 20 and 18 sparse, credit-frontier advances impossible at window 1,
so the wider-window treatment applied even though its throughput effect was
small.

Every in-scope video command and byte was classified by explicit frame
identity. Three legs have one terminal frame whose command was constructed
before the measurement cutoff; those boundary frames are named and excluded
from byte-per-frame rather than time-paired. There were no commands with the
opposite boundary direction. All eight exact-window rates close to their frame
counts, all terminal identities are unique, every latency decomposition has
zero negative segments, and the maximum absolute residual is
`7.11e-15` ms.

The first execution of this capture stopped red because the analyzer treated
the expected command-before-window boundary as malformed. The raw schema
already contained both identities, so the correction made both edge
directions explicit and reanalyzed these same bytes; no production mechanism,
instrument, treatment or measurement window changed.

## Preceding red attempts

The sibling captures ending `042713Z` through `053542Z` remain red and supply
no timing result. They record, in order, topology-mismatched certification,
an invalid LTR probe configuration, bounded session-logout and rollout races,
an unavailable privileged collector, missing or unsafe trace schema fields,
cold-start contamination, and finally an active-span denominator that failed
count/rate closure. The retained runner waits for 60 payload frames from a
newly cleared stamp file, stops polling before measurement, and divides the
terminal count by the exact window.

Raw distributions are in `raw-distributions.json`; `analysis.txt` is the
human-readable replay, and each `leg_*` directory retains its exact profile,
window, trace and producer stamps.

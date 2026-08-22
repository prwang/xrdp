# #122 -- one-active/one-idle monitor characterization

Recorded 2026-08-17.

## Outcome

The claimed one-active-monitor regression did **not** reproduce. With one
unchanged 2560x1440 monitor beside an idle 3840x2400 monitor, the active
surface completed a picture every 16.149 ms. The same surface completed a
picture every 26.768 ms while both monitors changed. One-active is therefore
0.603x the two-active period, or 1.658x its per-surface rate. The quarantined
old #94 hypothesis said the one-active case was slower; it is withdrawn.

This is a decision result only. It does not add a changed-pixel heuristic or
a product requirement. The existing per-monitor dirty-region behavior is
sufficient for the measured case.

## Paired conditions

Both 20-second arms used the same sitting, oracle client, package pair,
configuration, image and exact modelines:

* xrdp source `edfd0e5c80a6` plus the uncommitted #120 trace lifecycle;
* xorgxrdp source `10fa3aa23033` with the development `ACK_TRACE cap`
  producer logger removed;
* image
  `edfd0e5c80a6d.xx10fa3aa23033d-tf.r2.p11d2c5a7`;
* `gfx.toml` SHA-256 prefix `adb69e6e518902f7`;
* 2560x1440 mode
  `312.25 2560 2752 3024 3488 1440 1443 1448 1493 -hsync +vsync`;
* 3840x2400 mode
  `592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync`;
* `SESSION_KIND=textflood_strip`, compile-time-enabled `common/perf_trace`,
  and the save-only oracle client.

The only arm variable was `TEXTFLOOD_MONITOR`: `all` for the two-active
control and `0` for the selected 2560x1440 monitor. The payload's own header
closed that intervention independently:

* control: `target=all origin=0,0 6400x2400`;
* selected: `target=monitor-0 origin=0,0 2560x1440`.

The retained captures are:

* `i122_all_monitors_final_x036_20260817_s20`;
* `i122_monitor0_final_x037_20260817_s20`.

## Result by monitor identity

Intervals below are differences between consecutive `GFX_TRACE dmg` events
on the same surface, using the event's monotonic-microsecond field. They are
paired by surface identity, not by a time window. Percentiles use the same
nearest-rank indexing as the gate report.

| condition / surface | events | gaps | mean ms | p50 | p90 | p99 |
|---|---:|---:|---:|---:|---:|---:|
| both active / 2560x1440 surface 0 | 629 | 628 | 26.768 | 24.805 | 44.917 | 53.418 |
| both active / 3840x2400 surface 1 | 628 | 627 | 26.594 | 23.054 | 36.845 | 42.769 |
| monitor 0 selected / surface 0 | 1043 | 1042 | 16.149 | 16.052 | 16.979 | 18.394 |
| monitor 0 selected / idle surface 1 | 1 | 0 | n/a | n/a | n/a | n/a |

The count closure is exact: 629 + 628 = 1257 two-active damage and encode
submissions; 1043 + 1 = 1044 selected-arm submissions. Every submission has
one corresponding `enc` record and four `send last=0` records. The one idle
surface event is the initial surface fill, not a distribution.

The aggregate send gap is not the load-bearing comparison because two active
surfaces interleave: it was mean/p50/p90/p99 13.4/16/26/33 ms for both active
and 16.1/16/17/19 ms for one active. The per-surface table above answers the
actual question.

The payload margin was 1.32x in the two-active arm and 14.90x in the selected
arm. The former is below the 2x stage-overlap floor, so this record makes no
claim about overlap between internal stages. The gate explicitly permits the
same-payload throughput/regression comparison, and damage arrived every 10.2
ms while each two-active surface completed every 26.6--26.8 ms.

All failure counters were zero, both arm certificates passed their byte and
black-frame checks, the encoder input pipe stayed above 64 KiB, and neither
session-Xorg log contains `ACK_TRACE cap`.

## Failed procedure attempts retained

Three red steps prevented a false result.

1. The first correctly prefixed xrdp package was accidentally configured with
   `--disable-painter --disable-rfxcodec`. Both new arms reached the session
   but the oracle rejected a bitmap-cache order before GFX activation. The
   unchanged x035 arm certified with the same client, proving a new-package
   regression. Rebuilding with the frontier configure surface plus only
   `--enable-perf-trace` restored both certifications. The rejected package
   is not a measurement arm.
2. The first `--monitor 0` implementation used Xinerama. Xorgxrdp reports one
   6400x2400 Xinerama screen, so the payload header exposed that the selector
   had not changed the mechanism. Capture
   `i122_monitor0_x037_20260817_s20` is retained as a red intervention, not a
   timing result.
3. RandR exposes the real monitor rectangles, but initially exposes one union
   output. In the second attempt textflood queried at monotonic 2970439.586;
   the two client outputs arrived at 2970442.076. Capture
   `i122_monitor0_randr_x037_20260817_s20` is also a red intervention. The
   final payload waits at startup for the two-output RandR topology before it
   opens its stamps file or enters the measured loop.

The corresponding control captures remain beside those failed attempts so
their image and procedure identities are auditable. None is used in the
outcome above.

## Scientific gate

The raw counts close against the derived gaps and spans. The intervention is
visible both in the independent payload header and in xrdp's per-surface
coverage. No written PRD requirement is changed. The only earlier numerical
claim was deleted by #121 because its producer logger instrumented the path;
it is not used as a baseline. The final arms share client, geometry, image,
packages and configuration, with only the recorded selector changed.

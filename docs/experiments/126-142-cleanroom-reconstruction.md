# #126–#142 — clean-room reconstruction and first deployment

This is the durable execution record for the paired clean-room series. The
implementation branches are local only; the agent did not push them.

## Bases and branches

The xrdp series is `cleanroom/avc444_ffmpeg` in `/workCleanroom`, based on
`fe850a22c08a624c66bbac07e310251782e6f828`. The paired xorgxrdp series uses
the same branch name in `/workUpdateXorgXrdpCleanroom`, based on
`49bf2dd3546dc48b9d5bae62022762fde11793d0`.

| slice | xrdp commit | xorgxrdp commit, when paired |
|---|---|---|
| 126 | `9417122630b0161832ab38db973fb321a1f2f79b` | `1bd480062764040ba8791182007d63a4d260e1d0` |
| 127 | `b1293ffad32cbb786e4a271d45f2631525e5937b` | — |
| 128 | `1364cf8019fc1522c8345debc6e5b4bd869c4975` | `72b091c34d6e7f0a640a1d8dda1ab3c528da67c9` |
| 129 | `e80a39d7fe3c7883ff9846df1048817c47ffe6b4` | `5ee8bab2a1e0f156198992b8123e5a8974577a3a` |
| 130 | `00da39087ef899911799c28c98eff5d06f06cb07` | — |
| 131 | `c6dc433a9fdc9dab49381d998019e3bca17a9771` | — |
| 132 | `71163af8fb83c7c213571134591ad709b73c06e5` | — |
| 133 | `6e9347610f334f32b0200863c4d02ddf197d79f6` | — |
| 134 | `be9f0bd49ec92755a5f7dac2cb9dc774be037d64` | — |
| 135 | `cc03a3059df0b4735093526fc18cd67ae2b3a6e9` | — |
| 136 | `0faf5f899b5f2bad025d8fd75a7b445bc9bfbd8e` | `5b5288f08fb11a5c3e8df2896e721c01cec76fc4` |
| 137 | `6b4d116cb93bb9406eb11fc3d061e00dbbc2cee7` | — |
| 138 | `d027e24578004fbd7fb03a40fb6641509b28bbf9` | — |
| 139 | `53bd4152ad3085e05a209a6878687cb0a57167b5` | — |
| 140 | `4170f97b9dfd406511f3ddc9a8a0d9961f8ff5d6` | `aa03d860137d9be95a6bbf41c9ba29608fc6ec5d` |
| 141 | `e4a0ae47bb10ac679694b5b5b111ac3fb4c67014` | — |
| 142 | `a18d1305b2e930fa231045661e424bb333e20964` | — |

Each slice was authored and committed in dependency order. The backend is not
operator-selectable in an intermediate commit; activation and its complete
operator surface first appear in #142.

## Automated gates

The final trace-enabled xrdp tree passed the complete test matrix, including
203/203 xrdp tests. The default build also passed the complete matrix; its
disabled-trace TAP case proved there is no trace code, data, event string,
state or argument evaluation. Cppcheck 2.20.0, the version pinned by CI,
completed without a diagnostic. The four paired xorgxrdp tests passed: both
YUV/credit tests, the YUV conversion test, and Xorg driver loading. The xorg
build used `/workCleanroom/common` as its xup-contract include source.

The deployment smoke uses the repository oracle client and the same colour-key
payload at 1920 by 1080 and 1024 by 768. Its final result was:

| geometry | transitions | lag | settled edge fidelity | encoder errors |
|---|---:|---:|---:|---:|
| 1920×1080 | 8/8 | 0 | 1.000 | 0 |
| 1024×768 | 8/8 | 0 | 1.000 | 0 |

This smoke establishes connectivity, codec activation and the payload's final
colour state. It is not the retained Windows/macOS compatibility matrix and
does not supply a numerical performance result.

## Red deployment result and history correction

The first #142 image was red at 1920 by 1080 and green at 1024 by 768. The
capability probe passed and the client selected AVC444v2, but the first
production capture terminated the backend with `bad-configuration`. The
failure did not loop or fall back: the session hung up once as specified.

The xup capture contract pads coded height to 16 rows. A 1920-by-1080 capture
therefore contains two 1920-by-1088 views in each of two slots, for 12,533,760
bytes. The #133 runner had independently rounded visible height to two rows,
so it rejected the correctly sized capture. #142's pre-confirm probe repeated
the two-row calculation and certified a geometry different from production.
The 1024-by-768 leg concealed the defect because 768 satisfies both rules.

The correction was folded into the owning commits rather than appended to the
series. #133 now consumes the capture contract's 16-row coded height and has a
1920-by-1080 to 1920-by-1088 regression. #142 probes the same coded geometry,
including its no-monitor session fallback. #133 through #142 were replayed;
the commit table above is the corrected history. The normative requirements
now state this shared geometry obligation explicitly.

The numerical replay then exercised the trace schema as a consumer for the
first time and found omissions which source-only gates could not expose. The
corrections were likewise folded into their owning slices: #126's formatter
uses only its supported integer conversions and keeps child-role state
compile-time absent; #139 identifies main and auxiliary child events; #140
emits runtime-selectable, terminal-only transport and acknowledgement records
with frame identity, exact byte count and all frontiers needed to evaluate the
configured window. A source TAP now checks the selectors, guards, roles and
schemas. With tracing disabled, these fields, strings, branches and argument
evaluation remain absent. No production policy changed in these corrections.

## Final numerical replay

The retained capture is
`PR-demo/mac_bisect_matrix/captures/i142_local_matrix_20260825T054802Z/`.
It certifies and runs dense/sparse chroma crossed with wire windows 1/2 in the
prescribed `A B C D D C B A` order. Each leg is an exact 20-second,
one-monitor 3840x2400 oracle-client run with full-screen textflood.

Sparse raised this producer-paced workload's delivered rate by 8.3% at
window 1 and 9.0% at window 2, reduced classified video-command bytes per
frame by 49.3--49.4%, and reduced mean encoder-pump-to-transport/credit
latency from 32.6--33.2 ms to 18.8--19.5 ms. All eight rates close to terminal
frame count divided by the complete window. Command/byte accounting closes by
frame identity, every latency segment is non-negative, and all decompositions
close within `7.11e-15` ms.

Every leg has a 1.00x producer margin, so these are comparable end-to-end
textflood workload results, not unconstrained server ceilings and not evidence
about absent pipeline overlap. Window 2 was genuinely exercised: its traces
contain states window 1 cannot admit. Its rate effect remained below 1% in
both policies, providing no reason to change the shipped window-1 default.

The preceding red captures are retained beside the result. They supplied no
timing claim and drove corrections to certification topology, trace schema and
roles, terminal-only egress identity, readiness, exact-window rate closure and
explicit boundary accounting. Details and all distributions are in the
retained capture README.

## Local interactive arm

The final pair is deployed as x042 on host endpoint
`127.0.0.1:40058`. It runs Ubuntu 24.04 with FFmpeg 6.1.1, a regular minimal
XFCE desktop, LXTerminal for antialiased text and an unmodified default xterm.
No visual payload or measurement sidecar autostarts.

The immutable image is
`localhost/xrdp-bisect:cleanroom-a18d1305-aa03d860-u2404-xfce-notrace.pfe378533`.
It contains:

* `xrdp-dev` version
  `0.10.80+git20260825052420.a18d1305b2e9`, package SHA-256
  `fe378533a2f7cbaeedb078612631df93e05279d47b66d7402457a34130ddfc23`;
* `xorgxrdp-dev` version
  `1:0.10.80+git20260825030213.aa03d860137d`, package SHA-256
  `112b1c21b9c4cf584ad8e4a6e790f3b7ef46569ed542cc2ecb8cf5b1029b8e9a`;
* dense AVC with automatic mode selection, libx264, 32-pixel chroma width
  alignment, eager slot acknowledgement, a one-frame wire window, sparse
  chroma disabled, and performance tracing compile-time absent.

The live `gfx.toml` SHA-256 is
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`.
The post-smoke state has no tester Xorg session, no payload autostart and no
smoke configuration marker. `chroma-probe`, `colorkey_x11`,
`codescroll10.sh`, `textflood`, `lxterminal` and `xterm` are installed for the
owner's interactive replay.

## Remaining #142 gate

#126 through #141 are complete. #142's numerical and automated gates are
complete. It remains open only for the owner's retained real-client matrix on
x042. The interactive matrix
must still cover the supported AVC420/AVC444 modes, Windows and macOS, one and
two monitors, resize, fine chroma and motion. The local oracle smoke and
numerical replay cannot substitute for real-client rendering evidence.

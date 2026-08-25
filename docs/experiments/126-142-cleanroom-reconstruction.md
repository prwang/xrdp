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

## 2026-08-25 reword-only history rewrite and certificate correction

The source tree recorded above was not changed, but closure review found that
the #129, #132 and #140 commit messages did not meet the per-slice prose gate.
The series was rewritten only to correct those messages. The old and new final
tree are both `5ceed6c1fb3a52d7daa1343b0da6029a3aa7f2f1`; the current commit
map is:

| slice | current xrdp commit |
|---|---|
| 126 | `9417122630b0161832ab38db973fb321a1f2f79b` |
| 127 | `b1293ffad32cbb786e4a271d45f2631525e5937b` |
| 128 | `1364cf8019fc1522c8345debc6e5b4bd869c4975` |
| 129 | `36eda78d65b15944541294b393fe4034f6e97057` |
| 130 | `cc561ae8c85c1cc186edf809d22307ad4f0048f2` |
| 131 | `1fa455617ea252b730b189278e9d751d51c9b089` |
| 132 | `775e2f7b638a56760f65a25faa1d7d63a148570d` |
| 133 | `0695f0ff7883e8ded2f0039ca1ab973f9d2f2394` |
| 134 | `80943d8ada3a66c0c0b21adf4c710092685914f1` |
| 135 | `6b8a6c44adcbb0e19592346f4e34688e50987fb2` |
| 136 | `b66e087e3350f829b5897ea7126796ea82640e31` |
| 137 | `a8b38592cfa49761f83ab2b4b9050c686b3e9b53` |
| 138 | `c149c431b33df20594d3033629ce902e3a359605` |
| 139 | `1580c13cb13119a66e5ec4730665239f4c5dcdd0` |
| 140 | `702f4061bca2d89edb329431cbe6f2aabaf98547` |
| 141 | `6c42be1ebd2e3e30ca7638f518e2992f12e412ab` |
| 142 | `e0ee19616fcd4f4ca20fa95df88207e69360c79f` |

Fresh default and trace-enabled builds each passed the complete test matrix,
including 203/203 xrdp tests. The default package is
`0.10.80+git20260825061317.e0ee19616fcd`, SHA-256
`f254dc5b429c26ea202aac3c801a2803c99c2c59d6ba5e0c8cf66854f1c21d23`.
The paired xorgxrdp commit and package are unchanged.

x042 was rebuilt from that final commit as image
`localhost/xrdp-bisect:cleanroom-e0ee1961-aa03d860-u2404-xfce-notrace.pf254dc5b`.
Profile closure then found that the old certificates claiming 1920x1080 had
actually negotiated 1024x768, plus independent AVC420 framing, reset-header
and verdict-matching defects in the certifier. The invalid certificates and
both correction rounds are retained in
`PR-demo/mac_bisect_matrix/captures/i142_x042_profile_cert_20260825T062424Z/`.

After correction, automatic AVC444v2, forced AVC444v2, AVC444v1, AVC420 and
sparse AVC444v2 each passed the pipe, topology-specific wire and black-frame
gates at negotiated 1920x1080. The automatic profile was restored and the
rendered smoke again passed 8/8 transitions, zero lag, edge fidelity 1.000 and
zero encoder errors at both 1920x1080 and 1024x768. The remaining real-client
matrix stated above is unchanged.

## 2026-08-25 exact-slice closure audit and current deployment

The earlier final-tree gates did not prove that every historical commit was
independently buildable. The exact-commit audit first stopped at #133: its
default build passed 87/87, but its trace-enabled build did not compile because
the single-role constructor referred to `force_idr`, a parameter introduced
only by #137. The red run is retained at
`PR-demo/mac_bisect_matrix/captures/i142_slice_audit_20260825T070302Z/`.

The correction assigns #133's only child the main trace role. #137 now changes
that assignment to `!force_idr` when the ordinary-main and forced-IDR
auxiliary roles first coexist. Pinned astyle 3.4.14 then found a continuation
indentation defect at #141; it too was corrected in its owning commit before
#142 was replayed. These history repairs leave the final source tree unchanged
at `5ceed6c1fb3a52d7daa1343b0da6029a3aa7f2f1`. The current commit tail is:

| slice | current xrdp commit |
|---|---|
| 133 | `8cec0977d247538d4b9f837e358461a1338e7a16` |
| 134 | `d26748ed27daffc74c50b1bbdf7c4c5ed05881b2` |
| 135 | `59a898028ab1e48bc4622056142c07c39a99c7df` |
| 136 | `be9a3f71ca3266a98924a9bbabf9f81d49eb8bef` |
| 137 | `c97dde4923fda88b958b7bece59df209701e401c` |
| 138 | `f1283729cd83ce7e49b4b451597e8e1676e83034` |
| 139 | `a0c171caee2b84e68b875d6e10ae7e4684d0c8dd` |
| 140 | `ed6db7a434c37ac50da46c7fdab0ee8d496aa102` |
| 141 | `585c9b8e323d5d8ee23ef452fe9112b6489870fe` |
| 142 | `b38c63473c5297250af325d2770c5219ede69d48` |

The earlier #126--#132 identities remain current. The xorgxrdp identities are
also unchanged, ending at `3dc52da1321644bda7678fb246d815dc27bd9bef`.
Both branches retain their pinned ancestors and contain exactly 17 xrdp and
five paired xorgxrdp commits.

The corrected retained audit is
`PR-demo/mac_bisect_matrix/captures/i142_slice_audit_20260825T071722Z/`.
It records 33 green xrdp full-suite rows, 17 green xrdp diff/astyle/cppcheck
rows, five green paired xorgxrdp build/test rows and five green producer
diff-review rows. #142 passes 203/203 in both default and trace-enabled builds.
The audit README identifies the three retained harness/formatter red logs and
their dispositions instead of hiding them.

Closure review also corrected two inconsistent common-gate sentences in the
PRD. Performance tracing is private to xrdp and does not create an xorgxrdp
"trace mode" or a wire mismatch; xorgxrdp is built against the exact checked-
out header in either case. Likewise, xrdp's pinned astyle/cppcheck scripts are
xrdp repository gates, not a licence to rewrite xorgxrdp's differently styled
pinned base. Producer commits retain their own clean build/tests,
`git diff --check`, matching-header gate and changed-hunk review.

Because the current final source tree is byte-identical to the previously
certified one, the xrdp package payload was retained byte-for-byte and only its
package metadata was rebuilt to name commit `b38c63473c52`. Extracted payload
file contents, modes, ownership and symlinks compare identical. The package is
`xrdp-dev_0.10.80+git20260825074835.b38c63473c52_amd64.deb`, SHA-256
`382e50b0adba4ea6cd5fc8d8cb9e0eeaa8b1434c7a73110bef87ff0519625367`.
The paired xorgxrdp package remains
`1:0.10.80+git20260825064920.3dc52da13216`.

x042 now runs image
`localhost/xrdp-bisect:cleanroom-b38c6347-3dc52da1-u2404-xfce-notrace.p382e50b0`,
image ID `8f0bf6f3061cd4415c12a8e610ebd877459d456ab305b6fcd59ed48de6e038ae`.
All five 1920x1080 profile certificates passed again on that identity, and the
automatic dense profile with SHA-256
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`
was restored. The final rendered smoke passed both required geometries with
8/8 transitions, zero lag, edge fidelity 1.000 and zero encoder errors. The
handoff state has no tester Xorg/sesexec process, smoke marker, payload
autostart or performance trace file. The Windows/macOS visual matrix remains
the only open #142 acceptance work.

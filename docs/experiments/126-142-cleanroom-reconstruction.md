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
| 133 | `131c3ca0d49194e2a65ea39f348b311515ac6c1f` | — |
| 134 | `e0d4102435d39914c44478a5960b6964242e16b6` | — |
| 135 | `422503d997ecfa2daee815070436a066d3c50e10` | — |
| 136 | `181f0c825438940c0680eafbfcc6765f25cf9a8f` | `5b5288f08fb11a5c3e8df2896e721c01cec76fc4` |
| 137 | `b75a876f95109e07c01565d990b60dd85015735c` | — |
| 138 | `409dc7e21a3580d172de7f10f79a8f089a322237` | — |
| 139 | `6260fc3fae28984f8c9557d15c19b4b8abf3e107` | — |
| 140 | `447278dbbc378484b6e1c4157730432b8617ad4f` | `aa03d860137d9be95a6bbf41c9ba29608fc6ec5d` |
| 141 | `9e53e16235795d5c9fa6454c7b9d8f06a86613e1` | — |
| 142 | `ae2a4674ab96c5b30b71afaa2ca915fc5cf8972e` | — |

Each slice was authored and committed in dependency order. The backend is not
operator-selectable in an intermediate commit; activation and its complete
operator surface first appear in #142.

## Automated gates

The final trace-enabled xrdp tree passed the complete test matrix, including
195/195 xrdp tests. The default build also passed the complete matrix; its
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

## Local interactive arm

The corrected pair is deployed as x040 on host endpoint
`127.0.0.1:40056`. It runs Ubuntu 24.04 with FFmpeg 6.1.1, a regular minimal
XFCE desktop, LXTerminal for antialiased text and an unmodified default xterm.
No visual payload or measurement sidecar autostarts.

The immutable image is
`localhost/xrdp-bisect:cleanroom-ae2a4674-aa03d860-u2404-xfce.p736e0983`.
It contains:

* `xrdp-dev` version
  `0.10.80+git20260825041007.ae2a4674ab96`, package SHA-256
  `26d79257660324829c25d3115f7a265973b2006345ee7c9145b3e8b466fd536c`;
* `xorgxrdp-dev` version
  `1:0.10.80+git20260825030213.aa03d860137d`, package SHA-256
  `112b1c21b9c4cf584ad8e4a6e790f3b7ef46569ed542cc2ecb8cf5b1029b8e9a`;
* dense AVC with automatic mode selection, libx264, 32-pixel chroma width
  alignment, eager slot acknowledgement, a one-frame wire window, sparse
  chroma disabled, and the trace build armed to `/var/log/xrdp-perf/enc`.

The live `gfx.toml` SHA-256 is
`2646d4b1152cc3c59a7e3d8f358426f56b675e7fc5416323d4144985fafc7c5e`.
The post-smoke state has no tester Xorg session, no payload autostart and no
smoke configuration marker. `chroma-probe`, `colorkey_x11`,
`codescroll10.sh`, `textflood`, `lxterminal` and `xterm` are installed for the
owner's interactive replay.

## Remaining #142 gates

#126 through #141 are complete. #142 remains open for the retained real-client
matrix and the specified eight-leg numerical replay. The interactive matrix
must still cover the supported AVC420/AVC444 modes, Windows and macOS, one and
two monitors, resize, fine chroma and motion. The numerical replay must retain
the prescribed `A B C D D C B A` order and semantic trace accounting; the
qualitative smoke above cannot substitute for either result.

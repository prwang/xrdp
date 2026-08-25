# #142 x042 final interactive handoff

x042 is the final local interactive arm on `127.0.0.1:40058`. It uses regular
minimal XFCE on Ubuntu 24.04 with FFmpeg 6.1.1 and no payload autostart. The
xrdp package was built from `a18d1305b2e930fa231045661e424bb333e20964`
without `--enable-perf-trace`; xorgxrdp is
`aa03d860137d9be95a6bbf41c9ba29608fc6ec5d`.

The immutable image is
`localhost/xrdp-bisect:cleanroom-a18d1305-aa03d860-u2404-xfce-notrace.pfe378533`,
image ID
`4af90fe9c3e5671d84b73af3a5494cb13f2668b6ac7c7db176d5a26c24178d27`.
Package SHA-256 values are:

* xrdp: `fe378533a2f7cbaeedb078612631df93e05279d47b66d7402457a34130ddfc23`;
* xorgxrdp: `112b1c21b9c4cf584ad8e4a6e790f3b7ef46569ed542cc2ecb8cf5b1029b8e9a`.

The live `gfx.toml` SHA-256 is
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`.
It selects dense automatic AVC, libx264, eager slot acknowledgement, window 1,
and disables sparse chroma. The default xrdp binary contains no performance
trace event/schema strings, the deployment defines no trace environment, and
no performance output directory exists.

The topology-specific wire certificate is `certs/x042.cert`. The rendered
post-deploy smoke passed at both required sizes:

| geometry | transitions | lag | settled edge fidelity | encoder errors |
|---|---:|---:|---:|---:|
| 1920x1080 | 8/8 | 0 | 1.000 | 0 |
| 1024x768 | 8/8 | 0 | 1.000 | 0 |

After smoke, the whole test session was logged off. The retained state has no
tester Xorg process, no smoke marker, and no automatic visual payload. The
interactive tools `colorkey_x11`, `chroma-probe`, `codescroll10.sh`,
`textflood`, LXTerminal and default xterm are installed. x041, the disposable
trace-enabled numerical arm, is scaled to zero.

# #125C owner CPU deployment: probe green, leaf rewrite black

## Provenance

The owner attached `xrdp_log_ffmpeg6_cpu_certified_blackscreen.txt` on
2026-08-24 and identified the source as a manually deployed Ubuntu 24.04.4 LTS
machine using FFmpeg 6 and CPU encoding. The first log timestamp is
2026-08-24 10:29:54.375 -0400, or 2026-08-24 14:29:54.375Z. The attachment is
retained verbatim as `xrdp.log`; its SHA-256 is
`193ad1ffeeda895ebcdbde9f5f85fb27048e2468e1ca7009a80df5b5b588ae78`.

The attachment does not include the machine's exact package version, full
`gfx.toml`, ffmpeg build configuration, rejected H.264 access units or xrdp
commit/package identity. Do not infer those missing values from the filename.

## What the log proves

The pre-confirm single-child probe at 2560x1440 returned `OK` in 99 ms. Live
AVC444 then enabled the two-child auxiliary-leaf topology. Its first transform
failed, no pair was shipped and the client remained black. Each later damage
recreated two children and met the same rejection: 23 failures and 46 child
spawns occur between the first spawn at 10:30:00.848 and the final retained
failure at 10:30:03.204.

Source inspection established why the probe was a false green: it leaves
`aux_intra_leaf` disabled, while live AVC444 forces it on. It does not establish
which leaf guard rejected this distribution's bitstream because the runtime
collapses all guard failures into one generic message and retained no rejected
bytes. The local Ubuntu 24.04.4 reproduction must supply that missing evidence.

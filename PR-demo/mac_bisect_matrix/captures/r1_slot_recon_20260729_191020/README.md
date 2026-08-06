# R1 — capture-slot recon at monitorCount = 2 (BACKLOG #45 recon gate R1)

Taken 2026-07-29 with `PR-demo/mac_bisect_matrix/r1_slot_recon.sh 60`.

**Question.** FR-CAPTURE-8 gives each monitor two capture shmem slots so
frame N+1 can be captured while frame N encodes. The slot is chosen
globally as `(rect_id + 1) & 1`, and `rect_id` advances once per *send* —
so at an even monitor count the parity is constant per monitor. Does that
pin each monitor to one slot and leave the mechanism inert? Derived from
source; #45 requires it measured before step 6c changes the arithmetic.

**Setup.** Server: arm-q — arm-n's encoder config (VAAPI CQP 444 + LTR
aux-chain, byte-identical below the header) on xorgxrdp `957fa794ebdc`
= `5b9650cafbc3` plus one recon-only `LOG_LEVEL_INFO` line per AVC444
send. Client: host dummy X server presenting 2 × 1024×768 (virtual
2048×768) driving `xfreerdp3 /multimon /gfx:AVC444` at the pod's loopback
port — client side never enters the fleet. Session content
`SESSION_KIND=code` (line-scroll in a full-desktop xterm, so **both**
monitors are damaged every frame; a single damaged monitor would make
`rect_id` advance by 1 and alternate trivially, which is not the case
under test). 60 s, 1100 sends.

**Result — CONFIRMED** (`VERDICT.txt` for the generated report):

| measurement | value |
|---|---|
| full-pass consecutive sends reusing the same slot | **1079 / 1079** (zero changes) |
| monitor 1 distinct slots used in the whole run | **1** (second slot never written) |
| monitor 0 slot changes | 18, **all on partial passes**; 17 of them with the previous frame already acked |
| a monitor holding two outstanding frames in two slots | **1 in 1100 sends (0.09 %)** |
| max (`rect_id − rect_id_ack`) | **2** — global budget saturated; 543/1100 sends at depth 2 |

The gate had been written as "neither monitor ever changes slot". Monitor
0 does change slot on partial passes — a case the wording did not
anticipate — but those flips are not pipelining, and the property step 6c
depends on (per-monitor two-slot overlap) is inert at 0.09 %. The overlap
the session actually gets is cross-monitor, via the per-item ack.

**Files.** `r1slot.txt` raw records · `VERDICT.txt` generated report ·
`session-xorg.log` full server-side session log · `client.log`,
`client-xorg.log`, `client-monitors.txt` client side · `pod.log`.

The instrumentation is recon-only: revert xorgxrdp `957fa79` and retire
arm-q when step 6 lands.

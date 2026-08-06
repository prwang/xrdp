#!/bin/bash
# clock_log.sh — 1 Hz host operating-point log for BACKLOG #78's
# observe-only clock comparison (owner-approved design, 2026-08-02;
# clock PINNING was explicitly declined, so this only READS).
#
# One line per second, tab-separated, into $1 (default stdout):
#   <epoch.ms>  sclk=<MHz>  mclk=<MHz>  fclk=<MHz>  socclk=<MHz>
#   dcefclk=<MHz>  busy=<pct>  cpu=<min|median|max MHz over all cores>
#
# The active DPM level is the line amdgpu marks with '*'. VCN VCLK/DCLK
# are NOT exposed on this host (no pp_dpm_vclk, debugfs empty in the
# LXC) — stated here so the reader of a capture knows the encode
# engine's own clock is inferred, never read.
#
# This is a HARNESS sidecar at human rate (1 Hz), not per-frame
# telemetry: it never touches the measured process, the pods, or the
# perf ring (coding rule 5 / #61g: no sampler on the measured path —
# a 1 Hz cat of sysfs costs microseconds per second).
#
# Usage: clock_log.sh [outfile]   — runs until killed; SIGTERM-clean.
set -u
OUT="${1:-/dev/stdout}"
DEV=/sys/class/drm/card0/device

active_mhz()
{
    # print the MHz of the '*'-marked level, or '-' when none is marked
    awk '/\*/ { gsub(/Mhz.*/,"",$2); print $2; found=1 }
         END { if (!found) print "-" }' "$1" 2>/dev/null
}

cpu_mhz_summary()
{
    # min|mean|max MHz over all cores (no gawk asort dependency)
    cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null |
        awk '{ s+=$1; if (mn=="" || $1<mn) mn=$1; if ($1>mx) mx=$1; n++ }
             END { if (n==0) { print "-|-|-" }
                   else { printf "%d|%d|%d", mn/1000, s/n/1000, mx/1000 } }'
}

trap 'exit 0' TERM INT
while :
do
    t=$(date +%s.%3N)
    # mclk/fclk/dcefclk carry NO '*' marker on this APU (dynamic UMC
    # control) and are expected to read '-': stated so a reader does
    # not mistake '-' for a broken sampler. power = GPU average power
    # in mW from hwmon power1_average (microwatts) -- the duty proxy
    # that stands in for the unobservable VCN clock.
    printf '%s\tsclk=%s\tmclk=%s\tfclk=%s\tsocclk=%s\tbusy=%s\tpower_mw=%s\tcpu=%s\tlevel=%s\n' \
        "$t" \
        "$(active_mhz "$DEV/pp_dpm_sclk")" \
        "$(active_mhz "$DEV/pp_dpm_mclk")" \
        "$(active_mhz "$DEV/pp_dpm_fclk")" \
        "$(active_mhz "$DEV/pp_dpm_socclk")" \
        "$(cat "$DEV/gpu_busy_percent" 2>/dev/null || echo -)" \
        "$(awk '{ printf "%d", $1/1000 }' "$DEV"/hwmon/hwmon*/power1_average 2>/dev/null || echo -)" \
        "$(cpu_mhz_summary)" \
        "$(cat "$DEV/power_dpm_force_performance_level" 2>/dev/null || echo -)" \
        >> "$OUT"
    sleep 1
done

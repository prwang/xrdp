#!/bin/sh
# Local wrapper for T4 end-to-end frame accounting. The measurement itself
# is t4_measure.sh, a PERSISTENT on-T4 deploy (CLAUDE.md "T4 test
# methodology"): this wrapper checksum-compares the versioned copy against
# the installed one, re-installs it only when it changed, then invokes it
# as a single non-interactive command. No bash-over-ssh heredocs.
#
# Usage (from the dev box):
#   bash PR-demo/t4_profile/frame_accounting.sh              # orbit + 12s
#   NO_DRAG=1 bash PR-demo/t4_profile/frame_accounting.sh    # passive
#
# T4 host resolution: $T4 env, else /root/.t4_host (one place to update
# when the instance is recreated from the AMI), else fail.
#
# Reference result (2026-07-26, PRE-4B serial pipeline, xrdp 52099149 +
# xorgxrdp ee1ec01, nvenc, owner load on the bottom 4K monitor): 20.1 fps
# at EVERY stage, zero drops, client queue_depth=0, wire ~8 Mbit/s of ~98
# available.  Cycle p50 ~52ms = 5.6 cap->enc + 30.1 ENCODE (main+aux 4K,
# synchronous) + <1 send + ~5 ack->next-cap: the synchronous dual-4K
# encode was the fps ceiling (single shmem slot; see PRD FR-CAPTURE-8).
T4=${T4:-$(cat /root/.t4_host 2>/dev/null)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
if [ -z "$T4" ]; then
    echo "ABORT: set T4=user@host or write it to /root/.t4_host" >&2
    exit 1
fi

D=$(cd "$(dirname "$0")" && pwd)
LOCAL_SUM=$(md5sum "$D/t4_measure.sh" | cut -d' ' -f1)
REMOTE_SUM=$(ssh -i "$T4_KEY" "$T4" \
             "md5sum /usr/local/bin/t4_measure.sh 2>/dev/null | cut -d' ' -f1")
if [ "$LOCAL_SUM" != "$REMOTE_SUM" ]; then
    echo "installing t4_measure.sh ($LOCAL_SUM) on $T4"
    scp -q -i "$T4_KEY" "$D/t4_measure.sh" "$T4:/tmp/t4_measure.sh"
    ssh -i "$T4_KEY" "$T4" \
        "sudo install -m 755 /tmp/t4_measure.sh /usr/local/bin/t4_measure.sh"
fi

ssh -i "$T4_KEY" "$T4" \
    "SECS=${SECS:-12} ORBIT_X=${ORBIT_X:-670} ORBIT_Y=${ORBIT_Y:-1740}" \
    "ORBIT_R=${ORBIT_R:-280} REVS=${REVS:-40} NO_DRAG=${NO_DRAG:-0}" \
    "t4_measure.sh"

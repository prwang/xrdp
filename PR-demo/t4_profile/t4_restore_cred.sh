#!/bin/sh
# t4_restore_cred.sh — make a RECREATED T4 loginable over RDP again.
#
# Why this exists (2026-07-28): the T4 is recreated from an AMI at will
# (CLAUDE.md "T4 test methodology"). cloud-init re-locks the default
# `ubuntu` account's password on the new instance (`passwd -S ubuntu` ->
# state "L"), while /root/.ubuntu_cred survives inside the image. The
# result is an RDP login that fails with `pam_authenticate failed:
# Authentication failure` even though the stored credential is correct —
# an hour-burning symptom that looks like a broken deploy.
#
# This restores the documented invariant: ubuntu's RDP password IS the
# contents of root-owned /root/.ubuntu_cred. The credential never leaves
# the box, is never printed, and never becomes a process argument.
#
# Usage (from the dev box):
#   bash PR-demo/t4_profile/t4_restore_cred.sh
#   T4=ubuntu@host T4_KEY=/root/.ssh/key bash .../t4_restore_cred.sh
set -eu

T4=${T4:-$(cat /root/.t4_host 2>/dev/null || true)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
if [ -z "$T4" ]; then
    echo "ABORT: set T4=user@host or write it to /root/.t4_host" >&2
    exit 1
fi

# All credential handling happens ON the T4, inside a single root shell:
# read the file, pipe it straight into chpasswd. chpasswd rewrites the
# whole shadow field, which also clears cloud-init's "!" lock prefix.
ssh -o StrictHostKeyChecking=no -i "$T4_KEY" "$T4" '
    set -e
    sudo test -s /root/.ubuntu_cred || {
        echo "ABORT: /root/.ubuntu_cred missing or empty on the T4" >&2
        exit 1; }
    before=$(sudo passwd -S ubuntu | awk "{print \$2}")
    sudo sh -c "printf %s:%s ubuntu \"\$(cat /root/.ubuntu_cred)\" | chpasswd"
    after=$(sudo passwd -S ubuntu | awk "{print \$2}")
    echo "ubuntu password state: $before -> $after (P = usable, L = locked)"
    [ "$after" = "P" ] || { echo "ABORT: account still not usable" >&2; exit 1; }
'
echo "T4 RDP credential restored from the on-box store."

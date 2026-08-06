#!/bin/bash
# ssh to the remote real-Windows GRID host
. /root/.testvm_cred
export DISPLAY=:99 SSH_ASKPASS=/root/.win_askpass.sh SSH_ASKPASS_REQUIRE=force
setsid -w ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o ConnectTimeout=12 -p 22 Administrator@43.98.187.122 "$@" 2>/dev/null

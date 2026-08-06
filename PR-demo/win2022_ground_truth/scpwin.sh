#!/bin/bash
# scp to/from the remote Windows host. usage: scpwin.sh <src> <dst>
export DISPLAY=:99 SSH_ASKPASS=/root/.win_askpass.sh SSH_ASKPASS_REQUIRE=force
setsid -w scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o ConnectTimeout=15 -P 22 "$@" 2>/dev/null

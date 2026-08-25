#!/bin/bash
# Exercise the AVC444 dynamic-resize contract with one FreeRDP client.
set -eu

PORT=${1:-40059}
OUT=${2:-$(mktemp -d /tmp/xrdp-resize-contract.XXXXXX)}
CREDENTIAL_FILE=${XRDP_RESIZE_CREDENTIAL_FILE:-/root/.oracle_cred}
DISPLAY_NUMBER=${XRDP_RESIZE_DISPLAY_NUMBER:-97}
DISPLAY_NAME=":${DISPLAY_NUMBER}"
XVFB_PID=
CLIENT_PID=

cleanup()
{
    if [ -n "$CLIENT_PID" ] && kill -0 "$CLIENT_PID" 2>/dev/null
    then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    if [ -n "$XVFB_PID" ] && kill -0 "$XVFB_PID" 2>/dev/null
    then
        kill "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"
test -s "$CREDENTIAL_FILE"
Xvfb "$DISPLAY_NAME" -screen 0 2800x1600x24 -nolisten tcp \
    >"$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 1

PASSWORD=$(tr -d '\r\n' <"$CREDENTIAL_FILE")
DISPLAY="$DISPLAY_NAME" xfreerdp3 \
    "/v:127.0.0.1:${PORT}" /u:probe444 "/p:$PASSWORD" \
    /cert:ignore /sec:tls \
    '/gfx:AVC444,small-cache:off,thin-client:off' \
    /size:2196x1250 +dynamic-resolution \
    /client-hostname:xrdp-resize-contract >"$OUT/client.log" 2>&1 &
CLIENT_PID=$!
sleep 5

WINDOW=$(DISPLAY="$DISPLAY_NAME" timeout 5s xdotool search \
    --onlyvisible --pid "$CLIENT_PID" | tail -1)
printf 'port=%s\nclient_pid=%s\nwindow=%s\n' \
    "$PORT" "$CLIENT_PID" "$WINDOW" | tee "$OUT/result.txt"

DISPLAY="$DISPLAY_NAME" timeout 5s xdotool windowsize --sync \
    "$WINDOW" 2198 1250
printf 'requested=2198x1250 client_alive=%s\n' \
    "$(kill -0 "$CLIENT_PID" 2>/dev/null && printf yes || printf no)" \
    | tee -a "$OUT/result.txt"
sleep 2

DISPLAY="$DISPLAY_NAME" timeout 5s xdotool windowsize --sync \
    "$WINDOW" 2412 1344
printf 'requested=2412x1344 client_alive_immediate=%s\n' \
    "$(kill -0 "$CLIENT_PID" 2>/dev/null && printf yes || printf no)" \
    | tee -a "$OUT/result.txt"
sleep 5

if kill -0 "$CLIENT_PID" 2>/dev/null
then
    printf 'client_alive_after_5s=yes\nclient_exit=still-running\n' \
        | tee -a "$OUT/result.txt"
else
    set +e
    wait "$CLIENT_PID"
    CLIENT_STATUS=$?
    set -e
    CLIENT_PID=
    printf 'client_alive_after_5s=no\nclient_exit=%s\n' "$CLIENT_STATUS" \
        | tee -a "$OUT/result.txt"
fi

printf 'output=%s\n' "$OUT"

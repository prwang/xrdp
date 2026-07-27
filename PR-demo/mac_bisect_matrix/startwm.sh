#!/bin/sh
# Container session script. SESSION_KIND=xfce (pod env -> /etc/session_kind
# via entrypoint) launches the full desktop — real content for color
# verdicts; default is the labeled color banner (deterministic, and a
# black/frozen screen is unambiguously a pipeline failure).
if [ "$(cat /etc/session_kind 2>/dev/null)" = "xfce" ]; then
    exec dbus-launch --exit-with-session startxfce4
fi
exec /usr/local/bin/banner.sh

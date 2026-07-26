#!/bin/sh
# Container session script: no desktop environment — every arm shows the
# same deterministic full-screen banner (arm id + slowly cycling colour
# field) so the ONLY variable between arms is the encoder config/binary,
# and a black screen is unambiguously a pipeline failure, not idle content.
exec /usr/local/bin/banner.sh

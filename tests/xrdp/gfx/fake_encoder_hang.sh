#!/bin/sh
# Probe fixture: an "encoder" that accepts any argv and produces nothing.
# Models cold hardware-encoder init exceeding the probe deadline (T4
# NVENC cold boot, 2026-07-26). The probe must classify this as TIMEOUT
# (environmental), never as CONTENT evidence about header policy
# (PRD FR-PROBE-6).
exec sleep 30

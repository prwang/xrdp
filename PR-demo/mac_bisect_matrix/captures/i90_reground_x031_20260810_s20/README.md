# No measurement — geometry setup failed

This first x031 attempt supplied the `3840x2400R` mode name without the
matching modeline. `setup_monitors.sh` found DUMMY0 still at 2560x1440 and
aborted before RDP login. There is no trace, rate or encoder result here.

The corrected leg copied the exact proven modeline from the archived i92/i87
wrappers and is recorded in the sibling
`i90_reground_x031_20260810_s20_a2` capture.

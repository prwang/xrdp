# #142 local numerical replay — first logout request timed out

No timing leg ran and no performance number from this directory is usable.

Dense/window-1 certification again stopped at whole-session cleanup. XFCE's
first bounded fast-logout request did not return; issuing the same permitted
whole-session operation once more immediately afterward ended the session.
No individual GUI process was killed or relaunched.

The certifier now permits two explicitly bounded whole-session logout
requests, reports when the first does not complete, and still fails unless the
user's Xorg process is gone afterward. This is cleanup idempotence, not a
retry or fallback in the xrdp component under test. This run was not resumed.

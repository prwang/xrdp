Yes—change this on the **physical Incus host**, not inside this container or a k3s pod.

The current soft limit is back to **16384 pages (64 MiB)**. Our fleet runbook records your previous setting as **262144 pages (1 GiB)**, applied temporarily. Restore that documented value—not the smaller 65536-page allowance I initially suggested—and persist it:

```sh
printf '%s\n' 'fs.pipe-user-pages-soft = 262144' |
  sudo tee /etc/sysctl.d/90-xrdp-pipe-budget.conf

sudo sysctl -p /etc/sysctl.d/90-xrdp-pipe-budget.conf

sysctl fs.pipe-user-pages-soft fs.pipe-user-pages-hard fs.pipe-max-size
```

Expected values:

```text
fs.pipe-user-pages-soft = 262144
fs.pipe-user-pages-hard = 0
fs.pipe-max-size = 1048576
```

Only the soft limit needs changing. This raises a **host-wide per-user allowance**, without preallocating 1 GiB or granting additional container privileges. Container root maps to host UID 1000, so these pipes share that account’s quota. Exceeding the soft limit restricts new pipes to two pages and denies enlargement. [Kernel documentation](https://docs.kernel.org/admin-guide/sysctl/fs.html#pipe-user-pages-soft)

No host or Incus restart is needed. The persistent file survives host reboots; restarting a container alone does not restore a temporary host setting.

**40062 status:** the intentional overflow is committed as `c729a508` and deployed. Both even-size visual smoke checks passed, but its certificate remains red because encoder pipes received only 8 KiB. This is not confirmation of the odd-pixel hypothesis. Ports 40060 and 40061 are unchanged.

Tell me once the host setting is applied. I’ll preserve the failed evidence, recreate only 40062’s test pod from the identical image, and rerun certification and final smoke before handing it over for Windows testing. The explicit undo/spec correction remains conditional on reproducing the predicted Windows failure.

Deployment and blocker records are committed through `6e84e8d1`. This note is also at `/home/tester/.turn_draft_20260906-151800.md` in 40062; it is **not yet a request to start the interactive test**.

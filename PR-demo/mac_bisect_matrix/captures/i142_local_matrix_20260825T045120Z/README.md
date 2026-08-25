# #142 local numerical replay — trace collection shakedown

No performance number from this directory is usable.

All four exact profiles passed their topology-specific deployment
certificates. A1 then completed its 20-second payload, but the collection
helper attempted to invoke `sudo` inside the root-running measurement
container. This Ubuntu 24.04 image intentionally has no `sudo`, so collection
reported no perf ring even though `/var/log/xrdp-perf/enc.134` existed and was
growing. The gate therefore had zero send events and aborted the matrix.

The retained A1 directory contains the empty-analysis red result and exact
environment evidence. Its 681,509,974-byte oracle dump was discarded as
declared; it is not timing evidence. Perf collection now lists the root-owned
ring directly through the existing server abstraction. This run was not
resumed and none of its partial data is mixed into the replacement matrix.

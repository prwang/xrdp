# #142 local numerical replay — rollout pod-selection shakedown

No certification or timing leg ran and no performance number from this
directory is usable.

The dense/window-1 rollout completed, but the harness selected the first pod
returned by the deployment label. Kubernetes still exposed the completed old
pod during that transition, so the exact-config readback failed before
certification. Pod selection now requires `status.phase=Running` and rejects
an empty result. This run was not resumed.

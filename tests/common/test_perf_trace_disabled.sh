#!/bin/sh

set -eu

top_builddir=../..
top_srcdir=${srcdir:-.}/../..

if grep -q '^#define XRDP_PERF_TRACE 1' "$top_builddir/config_ac.h"
then
    echo '1..0 # SKIP performance trace is enabled'
    exit 0
fi

test_dir=$(mktemp -d /tmp/xrdp-perf-disabled-XXXXXX)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

sed 's/^+//' > "$test_dir/check.c" <<'EOF'
+#include <config_ac.h>
+#include "perf_trace.h"
+
+int
+main(void)
+{
+    int side_effect = 0;
+
+    PERF_TRACE("event=disabled_sentinel value=%d", side_effect++);
+    return side_effect;
+}
EOF

${CC:-cc} -O0 -I"$top_builddir" -I"$top_srcdir/common" \
    "$test_dir/check.c" -o "$test_dir/check"
"$test_dir/check"

if nm "$test_dir/check" | grep -q 'perf_trace'
then
    echo "disabled binary retains a perf_trace symbol" >&2
    exit 1
fi
if strings "$test_dir/check" | grep -q 'disabled_sentinel'
then
    echo "disabled binary retains an event literal" >&2
    exit 1
fi

echo '1..1'
echo 'ok 1 - disabled trace has no side effect, symbol or event string'

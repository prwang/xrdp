#!/bin/bash
# Compile each real serializer in place, discarding unrelated code at link.
set -eu
probe_build=$(mktemp -d /tmp/i142-bounds.XXXXXX)
printf 'Build artifacts: %s\n' "$probe_build"
for tree in /work /workCleanroom; do
    case "$tree" in
        /work) label=dev; variant=();;
        /workCleanroom) label=clean; variant=(-DCLEANROOM_PROBE);;
    esac
    includes=(-I"$tree" -I"$tree/xrdp" -I"$tree/common"
              -I"$tree/libxrdp" -I"$tree/third_party/tomlc99")
    timeout 20s gcc -DHAVE_CONFIG_H -ffunction-sections -fdata-sections -O0 \
        "${includes[@]}" -c "$tree/xrdp/xrdp_encoder.c" \
        -o "$probe_build/$label-encoder.o"
    timeout 20s gcc "${variant[@]}" -DHAVE_CONFIG_H -ffunction-sections \
        -fdata-sections -O0 "${includes[@]}" \
        /work/PR-demo/lib/i142_metablock_bounds_probe.c \
        "$probe_build/$label-encoder.o" "$tree/xrdp/xrdp_region.o" \
        -Wl,--gc-sections -L"$tree/common/.libs" \
        -Wl,-rpath,"$tree/common/.libs" -lcommon -o "$probe_build/$label"
    printf '%s serializer:\n' "$label"
    timeout 5s "$probe_build/$label" 2360 1033
    timeout 5s "$probe_build/$label" 1820 1171
    timeout 5s "$probe_build/$label" 1820 1202
done

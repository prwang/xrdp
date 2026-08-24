#!/bin/sh

# Keep internal work-item labels out of files and messages an administrator
# reads. Historical records and source comments are intentionally out of
# scope; the compiled binary check sees only runtime strings.

set -eu

echo "1..1"

test_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
source_root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
build_root=$(CDPATH= cd -- "${top_builddir:-$source_root}" && pwd)
forbidden='BACKLOG|PRD([[:space:]]|\[)|FR-[A-Z]|clean-room'

fail()
{
    echo "not ok 1 - operator surface: $*"
    exit 1
}

for file in \
    "$source_root/xrdp/gfx.toml" \
    "$source_root/docs/man/gfx.toml.5.in" \
    "$source_root/PR-demo/t4_profile/gfx-t4-nvenc-ltr.toml"
do
    if grep -Eq "$forbidden|#[0-9][0-9]+" "$file"
    then
        fail "internal project label in $file"
    fi
done

xrdp_binary="$build_root/xrdp/xrdp"
[ -x "$xrdp_binary" ] || fail "xrdp binary not found at $xrdp_binary"
if strings "$xrdp_binary" | grep -qE "$forbidden"
then
    fail "internal project label in an xrdp runtime string"
fi

if sed -n '/^usage(void)/,/^}/p' \
        "$source_root/PR-demo/textflood/textflood.c" | grep -qE "$forbidden"
then
    fail "internal project label in textflood help"
fi

for key in aux_ltr_chain eager_slot_ack wire_window \
           chroma_refresh_ms chroma_idle_ms
do
    grep -q "$key" "$source_root/xrdp/gfx.toml" ||
        fail "$key missing from the installed template"
    grep -Fq "\\fB${key}\\fR" "$source_root/docs/man/gfx.toml.5.in" ||
        fail "$key missing from gfx.toml(5)"
done

if grep -q 'tail_flush[[:space:]]*=' "$source_root/xrdp/gfx.toml"
then
    fail "removed tail_flush key is advertised by the installed template"
fi

echo "ok 1 - operator surface contains no internal project labels"

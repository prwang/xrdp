#!/bin/bash
# Deterministic session content for the bisect/bench fleet, selected by
# SESSION_KIND (pod env -> /etc/session_kind via entrypoint; "xfce" is
# dispatched by startwm.sh before this script runs).
#
#   banner   (default) labeled color chart + 1 Hz tick line. Color/
#            packing faults are self-evident ("the bar that says RED is
#            blue"); a stalled tick = pipeline stall, black = ack
#            starvation. Bench class: sparse small UI update.
#   scroll   colored text scrolling LINE BY LINE (1 line / 0.1 s) —
#            typical-scroll baseline: per-frame shift stays inside
#            encoder motion-search range. scrollfast = 10 lines/0.1 s,
#            the ME-defeating stress bound (~350 px per encoded frame).
#   gray     full-screen moving grayscale bands at 5 fps — large LUMA
#            motion with CONSTANT CHROMA. The FR-H264-8 discriminator:
#            an aux-refs-aux P frame is all-skip here, while the
#            FR-H264-7 all-intra leaf re-encodes every damaged MB.
#   chroma   full-screen moving color bands at 5 fps — large chroma
#            motion. NOTE: FLAT saturated bands, an intra-friendly
#            adversarial bound (see BACKLOG 2026-07-28 MB analysis),
#            not typical chroma-rich content.
#   code     scrolling syntax-highlighted C (Solarized Dark truecolor,
#            LCD subpixel AA) LINE BY LINE (1 line / 0.1 s) — the
#            realistic developer payload and, with scroll, the
#            FR-H264-8 BANDWIDTH GATE baseline (PRD FR-H264-8 (5)).
#            codefast = 10 lines/0.1 s stress bound; codeline =
#            legacy alias of code.
#
# THROUGHPUT kinds (no metronome — BACKLOG #52 / E5-2):
#   codeflood  the same corpus scroll with the sleep REMOVED (25 lines
#              per write, then straight back to the top of the loop), so
#              the producer is limited by the consumer, not by a timer.
#              Every cadence kind above is a 10 Hz metronome: E5 measured
#              52.5 ms per send against a 51.1 ms baseline and both
#              numbers were readings of that metronome, not of the server
#              (#45 GATE RESULTS, reassessed 2026-07-30). A frame-interval
#              gate needs damage to arrive FASTER than the pipeline drains
#              it, so repaints coalesce and the send interval is the
#              server's own.
#   grayflood  full-screen bands with the sleep removed — the
#              max-encode-cost bound (every macroblock damaged, every
#              frame) beside codeflood's realistic glyph damage.
#   textflood  the SAME corpus, rasterized by cairo in its OWN process
#              and handed to X as one finished image per frame over
#              MIT-SHM (PR-demo/textflood, BACKLOG #61b/#62). Use this
#              for any THROUGHPUT number. codeflood draws through the X
#              server -- glyph compositing, scroll blits, Present
#              emulation -- and #61c measured the consequence: the
#              session Xorg sits at 98.9 % of one core with NO client
#              attached, so the producer, not xrdp, sets the frame
#              period and no pipeline change is measurable against it.
#              textflood costs 7.7x less X-thread time for the same
#              pixels; its own rasterization runs on another core.
#              Producer telemetry for FR-BENCH-1 lands in
#              /tmp/e52_textflood_stamps.tsv (per-frame render/blit/
#              sync), which is what proves the payload is FASTER than
#              the pipeline rather than merely different.
# The flood kinds are for the FRAME-INTERVAL gate only. They are NOT
# byte-comparable across arms (the frame count is whatever the arm
# managed), which is exactly why the cadence kinds above are left alone:
# the FR-H264-8 bandwidth gate needs a fixed number of fixed frames.
#
# All are deterministic (fixed sequences, fixed cadence) so wire byte
# counts are comparable across arms and across runs. Line-by-line is
# the DEFAULT scroll granularity for both text classes (owner
# directive 2026-07-28): it is the regime where the main chain is
# properly inter-compressed and the aux term dominates — the regime
# FR-H264-8 optimizes and is judged on.
ARM=$(cat /etc/arm_label 2>/dev/null || echo "unknown arm")
KIND=$(cat /etc/session_kind 2>/dev/null || echo banner)

xsetroot -solid '#204060' || true

XTERM=(xterm -maximized -fa 'DejaVu Sans Mono' -fs 22 -bg black -fg white)

case "$KIND" in
scroll|scrollfast)
    exec "${XTERM[@]}" -e bash -c '
        STEP=1
        [ "$(cat /etc/session_kind 2>/dev/null)" = scrollfast ] && STEP=10
        i=0
        tput civis 2>/dev/null
        while true; do
            n=1
            while [ "$n" -le "$STEP" ]; do
                c=$((31 + (i + n) % 7))
                printf "\e[%dm%06d scroll workload: the quick brown fox jumps over the lazy dog 0123456789\e[0m\n" \
                    "$c" $((i + n))
                n=$((n + 1))
            done
            i=$((i + STEP))
            sleep 0.1
        done'
    ;;
textflood)
    # BACKLOG #61b. Override-redirect over the whole root, so no window
    # manager can re-snap it to one monitor (the failure that invalidated
    # three T4 runs with the xterm payload). No metronome: it renders as
    # fast as it can, which is what a frame-interval gate needs.
    CORPUS=/usr/local/share/code_corpus.ansi
    if [ ! -s "$CORPUS" ]; then
        while true; do
            xmessage -geometry 1200x200 \
                "NO CODE CORPUS at $CORPUS - bench invalid" 2>/dev/null \
                || sleep 5
        done
    fi
    exec /usr/local/bin/textflood --corpus "$CORPUS" \
        --stamps /tmp/e52_textflood_stamps.tsv
    ;;
gray|grayflood)
    exec "${XTERM[@]}" -e bash -c '
        DELAY=0.2
        [ "$(cat /etc/session_kind 2>/dev/null)" = grayflood ] && DELAY=
        i=0
        tput civis 2>/dev/null
        H=$(tput lines); W=$(tput cols)
        while true; do
            printf "\e[H"
            r=0
            while [ "$r" -lt "$H" ]; do
                printf "\e[48;5;%dm%*s\e[0m" $((232 + (r + i) % 24)) "$W" ""
                r=$((r + 1))
            done
            i=$((i + 1))
            [ -n "$DELAY" ] && sleep "$DELAY"
        done'
    ;;
chroma)
    exec "${XTERM[@]}" -e bash -c '
        i=0
        tput civis 2>/dev/null
        H=$(tput lines); W=$(tput cols)
        colors=(41 42 43 44 45 46)
        while true; do
            printf "\e[H"
            r=0
            while [ "$r" -lt "$H" ]; do
                printf "\e[%dm%*s\e[0m" "${colors[$(((r + i) % 6))]}" "$W" ""
                r=$((r + 1))
            done
            i=$((i + 1))
            sleep 0.2
        done'
    ;;
code|codeline|codefast|codeflood)
    # Scrolls the pre-generated ANSI corpus (real repo code, pygments
    # solarized-dark + clangd semantic tokens — see gen_code_corpus.py).
    #   code      1 line / 0.1 s (DEFAULT; codeline = legacy alias) —
    #             typical reading scroll: ~20 px per encoded frame,
    #             inside motion-search range, so a partitioned main
    #             chain tracks it (measured: 84% skip / 13% inter).
    #             FR-H264-8 bandwidth-gate baseline (PRD FR-H264-8).
    #   codefast  10 lines / 0.1 s — fast-scroll stress: ~180 px shift
    #             per encoded frame DEFEATS VAAPI motion search
    #             (measured 2026-07-28: 5.9% inter MBs), so glyph MBs
    #             re-code intra every frame.
    # 3000 lines => no frame content repeats within a bench window.
    # Fails LOUD if the corpus mount is missing — never silently
    # benchmarks a fallback.
    #
    # LCD SUBPIXEL AA (freetype RGB decimation): the container image
    # ships fontconfig 10-sub-pixel-none.conf, which ASSIGNS rgba=none
    # into every pattern — that preempts Xft.rgba resources (measured:
    # raw session residual 0.50 = pure grayscale AA), understressing
    # the aux/chroma channel vs a real LCD-tuned desktop. Override at
    # the user fontconfig layer (50-user.conf loads after 10-*, and
    # mode="assign" overwrites), which the code workload needs for the
    # colored subpixel fringes real desktops put on every glyph edge.
    mkdir -p "$HOME/.config/fontconfig"
    cat > "$HOME/.config/fontconfig/fonts.conf" <<'FCEOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <match target="pattern">
    <edit name="rgba" mode="assign"><const>rgb</const></edit>
    <edit name="lcdfilter" mode="assign"><const>lcddefault</const></edit>
    <edit name="antialias" mode="assign"><bool>true</bool></edit>
  </match>
</fontconfig>
FCEOF
    xrdb -merge <<'XRDBEOF' || true
Xft.antialias: 1
Xft.rgba: rgb
Xft.lcdfilter: lcddefault
Xft.hinting: 1
Xft.hintstyle: hintslight
XRDBEOF
    exec "${XTERM[@]}" -bg '#002b36' -fg '#839496' -fs 14 -e bash -c '
        CORPUS=/usr/local/share/code_corpus.ansi
        if [ ! -s "$CORPUS" ]; then
            while true; do
                clear
                printf "\e[41m  NO CODE CORPUS at %s — bench invalid  \e[0m\n" "$CORPUS"
                sleep 1
            done
        fi
        mapfile -t L < "$CORPUS"
        KIND=$(cat /etc/session_kind 2>/dev/null)
        STEP=1
        DELAY=0.1
        REPEAT=1
        [ "$KIND" = codefast ] && STEP=10
        # codeflood: no metronome at all, a 25-line write so the per-write
        # shell overhead is small next to the repaint it causes, and each
        # corpus line REPEATED so it wraps across the whole terminal.
        #
        # The repeat is not cosmetic. Measured 2026-07-30 from the oracle
        # dumps of the first flood pair: a corpus line is ~60 visible
        # characters, so on a 6400x2400 xterm the ink sat in the left
        # ~600 px and the 3840x2400 monitor received full-monitor damage
        # every cycle with ZERO changed pixels — 167 MB of pictures on
        # monitor 0 against 0.75 MB on monitor 1. That makes a multimon
        # throughput gate a single-monitor benchmark with a blank second
        # capture attached, which is exactly the premise #45 step 7 is
        # supposed to be judged on. Repeating fills every row edge to
        # edge, so both monitors carry real content. 32x: the corpus
        # median line is 27 visible columns and a 6400 px xterm at -fs 14
        # is ~760, so 32 copies wrap past the right edge of monitor 1.
        [ "$KIND" = codeflood ] && { STEP=25; DELAY=; REPEAT=32; }
        i=0
        tput civis 2>/dev/null
        while true; do
            n=1
            while [ "$n" -le "$STEP" ]; do
                txt="${L[$(((i + n) % ${#L[@]}))]}"
                if [ "$REPEAT" -gt 1 ]; then
                    r=1
                    while [ "$r" -lt "$REPEAT" ]; do
                        txt="$txt $txt"
                        r=$((r * 2))
                    done
                fi
                printf "%4d  %s\n" $(((i + n) % ${#L[@]})) "$txt"
                n=$((n + 1))
            done
            i=$((i + STEP))
            [ -n "$DELAY" ] && sleep "$DELAY"
        done'
    ;;
*)
    exec "${XTERM[@]}" -e bash -c '
        ARM="'"$ARM"'"
        i=0
        tput civis 2>/dev/null
        while true; do
            clear
            echo
            echo "  ================================================="
            echo "   BISECT ARM: $ARM"
            echo "  ================================================="
            echo
            printf "   %-9s \e[41m%*s\e[0m\n"  "RED"     36 ""
            printf "   %-9s \e[42m%*s\e[0m\n"  "GREEN"   36 ""
            printf "   %-9s \e[44m%*s\e[0m\n"  "BLUE"    36 ""
            printf "   %-9s \e[43m%*s\e[0m\n"  "YELLOW"  36 ""
            printf "   %-9s \e[46m%*s\e[0m\n"  "CYAN"    36 ""
            printf "   %-9s \e[45m%*s\e[0m\n"  "MAGENTA" 36 ""
            printf "   %-9s \e[47m%*s\e[0m\n"  "WHITE"   36 ""
            echo
            printf "   fine red/blue stripes: "
            for s in $(seq 1 18); do printf "\e[41m \e[44m "; done
            printf "\e[0m\n"
            echo
            echo "   tick $i"
            i=$((i + 1))
            sleep 1
        done'
    ;;
esac

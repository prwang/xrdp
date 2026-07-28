#!/bin/bash
# Deterministic session content for the bisect/bench fleet, selected by
# SESSION_KIND (pod env -> /etc/session_kind via entrypoint; "xfce" is
# dispatched by startwm.sh before this script runs).
#
#   banner   (default) labeled color chart + 1 Hz tick line. Color/
#            packing faults are self-evident ("the bar that says RED is
#            blue"); a stalled tick = pipeline stall, black = ack
#            starvation. Bench class: sparse small UI update.
#   scroll   colored text scrolling at ~10 Hz — sustained mixed
#            luma+chroma damage (terminal/browser-scroll shape).
#   gray     full-screen moving grayscale bands at 5 fps — large LUMA
#            motion with CONSTANT CHROMA. The FR-H264-8 discriminator:
#            an aux-refs-aux P frame is all-skip here, while the
#            FR-H264-7 all-intra leaf re-encodes every damaged MB.
#   chroma   full-screen moving color bands at 5 fps — large chroma
#            motion. NOTE: FLAT saturated bands, an intra-friendly
#            adversarial bound (see BACKLOG 2026-07-28 MB analysis),
#            not typical chroma-rich content.
#   code     scrolling syntax-highlighted C on the Solarized Dark
#            truecolor palette (~10 Hz) — the realistic developer
#            payload: textured glyphs, color-rich but muted theme.
#
# All are deterministic (fixed sequences, fixed cadence) so wire byte
# counts are comparable across arms and across runs.
ARM=$(cat /etc/arm_label 2>/dev/null || echo "unknown arm")
KIND=$(cat /etc/session_kind 2>/dev/null || echo banner)

xsetroot -solid '#204060' || true

XTERM=(xterm -maximized -fa 'DejaVu Sans Mono' -fs 22 -bg black -fg white)

case "$KIND" in
scroll)
    exec "${XTERM[@]}" -e bash -c '
        i=0
        tput civis 2>/dev/null
        while true; do
            for n in 1 2 3 4 5 6 7 8 9 10; do
                c=$((31 + (i + n) % 7))
                printf "\e[%dm%06d scroll workload: the quick brown fox jumps over the lazy dog 0123456789\e[0m\n" \
                    "$c" $((i + n))
            done
            i=$((i + 10))
            sleep 0.1
        done'
    ;;
gray)
    exec "${XTERM[@]}" -e bash -c '
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
            sleep 0.2
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
code|codeline)
    # Scrolls the pre-generated ANSI corpus (real repo code, pygments
    # solarized-dark + clangd semantic tokens — see gen_code_corpus.py).
    #   code      10 lines / 0.1 s — fast-scroll stress: ~180 px shift
    #             per encoded frame DEFEATS VAAPI motion search
    #             (measured 2026-07-28: 5.9% inter MBs), so glyph MBs
    #             re-code intra every frame.
    #   codeline  1 line / 0.1 s — typical reading scroll: ~20 px per
    #             encoded frame, inside motion-search range, so a
    #             partitioned main chain can actually track it.
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
        STEP=10
        [ "$(cat /etc/session_kind 2>/dev/null)" = codeline ] && STEP=1
        i=0
        tput civis 2>/dev/null
        while true; do
            n=1
            while [ "$n" -le "$STEP" ]; do
                printf "%4d  %s\n" $(((i + n) % ${#L[@]})) \
                    "${L[$(((i + n) % ${#L[@]}))]}"
                n=$((n + 1))
            done
            i=$((i + STEP))
            sleep 0.1
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

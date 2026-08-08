/* What keyframe interval does the LINKED-LIBRARY H.264 path actually use?
 *
 * xrdp's x264 encoder (xrdp/xrdp_encoder_x264.c) calls
 * x264_param_default_preset() and then sets threads, geometry, fps, the
 * rate-control method, VBV and the profile. It NEVER sets
 * i_keyint_max. So the shipped linked-library path emits a real IDR at
 * x264's library default, whatever that is -- and that default is the
 * cadence the ffmpeg path's scheduled refresh should be compared
 * against, because it is what xrdp has always done.
 *
 * This asks the library rather than trusting documentation, under the
 * presets gfx.toml actually ships.
 *
 * build: cc -O2 x264_keyint_probe.c -lx264 -o x264_keyint_probe
 * run:   ./x264_keyint_probe
 *
 * Measured on the dev box 2026-08-08: 250 in every configuration
 * below -- neither ultrafast nor veryfast nor zerolatency changes it.
 */
#include <stdint.h>
#include <x264.h>
#include <stdio.h>
int main(void){
  x264_param_t p;
  x264_param_default(&p);
  printf("x264_param_default:            i_keyint_max=%d i_keyint_min=%d\n", p.i_keyint_max, p.i_keyint_min);
  x264_param_default_preset(&p, "ultrafast", "zerolatency");
  printf("preset ultrafast/zerolatency: i_keyint_max=%d i_keyint_min=%d fps=%d/%d\n", p.i_keyint_max, p.i_keyint_min, p.i_fps_num, p.i_fps_den);
  x264_param_default_preset(&p, "veryfast", "zerolatency");
  printf("preset veryfast/zerolatency:  i_keyint_max=%d\n", p.i_keyint_max);
  return 0;
}

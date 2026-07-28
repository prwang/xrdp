#!/bin/bash
# patched FreeRDP with rdpgfx wire dump
export LD_LIBRARY_PATH="/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/common:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/libfreerdp:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/winpr/libwinpr:$LD_LIBRARY_PATH"
exec "/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/X11/xfreerdp" "$@"

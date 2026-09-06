#!/usr/bin/env python3
"""Build one test GFX plugin in this canonical checkout, using pinned SDK."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
SDK = Path('/opt/freerdp-vaapi')
if HERE != Path('/work/PR-demo/gfx_reset_client'):
    raise SystemExit('Build only in the canonical /work checkout')
build = HERE / 'build'
src = build / 'upstream'
src.mkdir(parents=True, exist_ok=True)
for row in (HERE / 'upstream.sha256').read_text().splitlines():
    digest, path = row.split()
    target = src / Path(path).name
    if not target.exists():
        url = 'https://raw.githubusercontent.com/FreeRDP/FreeRDP/3.15.0/' + path
        target.write_bytes(urllib.request.urlopen(url, timeout=10).read())
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        raise SystemExit('Upstream hash mismatch: ' + path)

def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('Patch seam count mismatch: ' + old)
    return text.replace(old, new)

header = (src / 'rdpgfx_main.h').read_text()
header = '#include <pthread.h>\n' + header
header = replace(header, '\tGENERIC_DYNVC_PLUGIN base;', '''
    GENERIC_DYNVC_PLUGIN base;
    pthread_mutex_t control_lock;
    int control_fd;
    HANDLE control_event;
    char control_path[108];
    BOOL control_started;
    BOOL reset_waiting;
    unsigned int reset_requests;
    unsigned int reset_confirms;
    unsigned int reset_ignored;''')
(build / 'rdpgfx_main.h').write_text(header)
main = (src / 'rdpgfx_main.c').read_text()
main = '#include <fcntl.h>\n' + main
main = replace(main, '\tgfx->ConnectionCaps = capsSet;', '''
    gfx->ConnectionCaps = capsSet;
    gfx->reset_waiting = FALSE;
    gfx->reset_confirms++;
    WLog_INFO(TAG, "CONTROL confirm: count=%u version=0x%08x",
              gfx->reset_confirms, capsSet.version);''')
main = replace(main, '\tswitch (header.cmdId)', '''
    /* Parse framing even while ignoring old messages. The channel's ZGFX
     * transport dictionary remains ordered; graphics state is discarded. */
    if (header.pduLength < RDPGFX_HEADER_SIZE ||
        header.pduLength > Stream_Length(s) - beg)
        return ERROR_INVALID_DATA;
    if (gfx->reset_waiting && header.cmdId != RDPGFX_CMDID_CAPSCONFIRM)
    {
        gfx->reset_ignored++;
        Stream_SetPosition(s, beg + header.pduLength);
        return CHANNEL_RC_OK;
    }
    switch (header.cmdId)''')
main = replace(main, 'static const IWTSVirtualChannelCallback rdpgfx_callbacks = { rdpgfx_on_data_received,', '''
#include "reset_control.c"
static const IWTSVirtualChannelCallback rdpgfx_callbacks = { controlled_receive,''')
main = replace(main, 'rdpgfx_on_open, rdpgfx_on_close,', 'controlled_open, controlled_close,')
# Keep all patched translation units beside the matching private header.
(build / 'rdpgfx_main.c').write_text(main)
for name in ['rdpgfx_codec.c', 'rdpgfx_codec.h', 'rdpgfx_common.c', 'rdpgfx_common.h']:
    (build / name).write_bytes((src / name).read_bytes())
env = dict(os.environ, PKG_CONFIG_PATH=str(SDK / 'lib/pkgconfig'))
version = subprocess.check_output(['pkg-config', '--modversion', 'freerdp3'], env=env, timeout=5).decode().strip()
if version != '3.15.0':
    raise SystemExit('Expected FreeRDP 3.15.0 SDK, got ' + version)
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'freerdp-client3'], env=env, timeout=5).decode())
flags += shlex.split(subprocess.check_output(
    ['pkg-config', '--libs', 'freerdp3', 'winpr3'], env=env,
    timeout=5).decode())
subprocess.run(['cc', '-std=gnu11', '-D_GNU_SOURCE', '-fPIC', '-shared', '-O2', '-g',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-Wno-deprecated-declarations', '-Wno-type-limits',
                '-I' + str(HERE), '-I' + str(build),
                str(build / 'rdpgfx_main.c'), str(build / 'rdpgfx_codec.c'),
                str(build / 'rdpgfx_common.c'), str(HERE / 'load_plugin.c'),
                '-o', str(build / 'libgfx-reset.so'), '-pthread', '-ldl',
                '-Wl,-z,defs', '-Wl,-rpath,' + str(SDK / 'lib'), *flags],
               check=True, timeout=60)
print(build / 'libgfx-reset.so')
subprocess.run(['cc', '-std=gnu11', '-D_GNU_SOURCE', '-O2', '-g',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-Wno-deprecated-declarations', '-Wno-type-limits',
                '-I' + str(HERE), '-I' + str(build),
                str(HERE / 'test_receive.c'), str(build / 'rdpgfx_codec.c'),
                str(build / 'rdpgfx_common.c'),
                '-o', str(build / 'test_receive'), '-pthread', '-ldl',
                '-Wl,-rpath,' + str(SDK / 'lib'), *flags],
               check=True, timeout=60)
subprocess.run([str(build / 'test_receive')], check=True, timeout=5)

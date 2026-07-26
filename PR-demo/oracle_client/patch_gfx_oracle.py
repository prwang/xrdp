#!/usr/bin/env python3
"""Patch FreeRDP 3.15.0 libfreerdp/gdi/gfx.c with the oracle-dump mode.

Oracle mode (FREERDP_ORACLE_DUMP=1): the AVC420/AVC444 gdi surface-command
handlers append each encoded payload to /tmp/oracle_avc_s<surfaceId>.bin
(u32-length-prefixed records) and return success BEFORE decode/present.
EndFrame then costs ~0, so the egfx frame ack timestamps arrival only:
the frame interval measured at the server is pure server+WAN pipeline —
the client contributes nothing. See BACKLOG "fps-methodology findings
(2026-07-26)" for why this instrument exists (xfreerdp's software 4:4:4
reconstruction costs ~65 ms/frame at the owner layout and ceilings
end-to-end fps at ~15 regardless of decoder).

Anchored + asserted: fails loudly on any other FreeRDP version.
Usage: patch_gfx_oracle.py <freerdp-source-root>
"""
import sys

path = sys.argv[1] + '/libfreerdp/gdi/gfx.c'
src = open(path).read()
if 'oracle_dump_enabled' in src:
    print('already patched')
    sys.exit(0)

helper = '''
/* Oracle-dump harness mode (xrdp AVC444 perf work): when
 * FREERDP_ORACLE_DUMP=1, append each encoded AVC surface command payload to
 * /tmp/oracle_avc_s<surfaceId>.bin (u32-len-prefixed records) and return
 * success WITHOUT decoding or presenting. EndFrame then completes in ~0ms,
 * so the frame ack measures server+network only -- the client contributes
 * nothing to the frame interval. */
static BOOL oracle_dump_enabled(void)
{
\tstatic int v = -1;
\tif (v < 0)
\t{
\t\tconst char* e = getenv("FREERDP_ORACLE_DUMP");
\t\tv = (e != NULL && e[0] == '1') ? 1 : 0;
\t}
\treturn v == 1;
}

static void oracle_dump(UINT16 surfaceId, const BYTE* data, UINT32 length)
{
\tchar path[64];
\tFILE* f = NULL;
\t(void)snprintf(path, sizeof(path), "/tmp/oracle_avc_s%u.bin", surfaceId);
\tf = fopen(path, "ab");
\tif (f)
\t{
\t\t(void)fwrite(&length, 4, 1, f);
\t\t(void)fwrite(data, 1, length, f);
\t\t(void)fclose(f);
\t}
}

'''
anchor = ('static UINT gdi_SurfaceCommand_AVC420(rdpGdi* gdi, '
          'RdpgfxClientContext* context,')
assert anchor in src, 'AVC420 handler anchor not found'
src = src.replace(anchor, helper + anchor, 1)

hook = '''\tif (oracle_dump_enabled())
\t{
\t\tWINPR_ASSERT(cmd);
\t\toracle_dump(WINPR_ASSERTING_INT_CAST(UINT16, cmd->surfaceId), cmd->data, cmd->length);
\t\treturn CHANNEL_RC_OK;
\t}
'''
for fn in ('AVC420', 'AVC444'):
    a = ('static UINT gdi_SurfaceCommand_%s(rdpGdi* gdi, RdpgfxClientContext* context,\n'
         '                                      const RDPGFX_SURFACE_COMMAND* cmd)\n'
         '{\n#ifdef WITH_GFX_H264\n') % fn
    assert a in src, fn + ' handler body anchor not found'
    src = src.replace(a, a + hook, 1)

open(path, 'w').write(src)
print('patched', path)

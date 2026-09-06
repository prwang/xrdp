#!/usr/bin/env python3
"""One bounded rendered reset on the corrected development server."""
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / 'PR-demo/mac_bisect_matrix/captures' / (
    'i142d_freerdp_reset_' + datetime.datetime.now(datetime.timezone.utc).
    strftime('%Y%m%dT%H%M%SZ'))
OUT.mkdir()
KUBE = ['kubectl', '-n', 'bisect-matrix']
REMOTE = KUBE + ['exec', 'deployment/xrdp-x046', '--']
events = []


def run(args, timeout=10, **kwargs):
    return subprocess.run(args, check=True, timeout=timeout, **kwargs)


def output(args, **kwargs):
    return run(args, stdout=subprocess.PIPE, **kwargs).stdout.decode()


def record(name, value):
    events.append({'utc': datetime.datetime.now(datetime.timezone.utc).
                   isoformat(), 'event': name, 'value': value})
    (OUT / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
    print(name, value, flush=True)


def snapshot(name):
    (OUT / (name + '-processes.txt')).write_text(output(
        REMOTE + ['ps', '-eLo', 'pid,ppid,tid,user,comm']))


def logoff():
    # The whole owned desktop logs off; no individual GUI is relaunched.
    script = r'''p=$(pgrep -u tester -x Xorg) || exit 0
test "$(printf '%s\n' "$p" | wc -l)" = 1 || exit 1
args=$(tr '\0' '\n' < /proc/$p/cmdline)
display=$(printf '%s\n' "$args" | sed -n '/^:[0-9][0-9]*$/p')
auth=$(printf '%s\n' "$args" | sed -n '/^-auth$/{n;p;}')
su -s /bin/sh tester -c "DISPLAY=$display XAUTHORITY=$auth xfce4-session-logout --logout"
'''
    run(REMOTE + ['sh', '-c', script])


client = None
xserver = None
armed = False
owned = False
status = 1
try:
    deployment = output(KUBE + ['get', 'deployment', 'xrdp-x046', '-o', 'json'])
    (OUT / 'deployment.json').write_text(deployment)
    image = json.loads(deployment)['spec']['template']['spec']['containers'][0]['image']
    if image != 'localhost/xrdp-bisect:dev-visible-clip-1cd9b5513637':
        raise RuntimeError('Unexpected server image: ' + image)
    processes = output(REMOTE + ['ps', '-eo', 'user,comm'])
    if 'Xorg' in processes:
        raise RuntimeError('Existing desktop: refusing to take over')
    if output(REMOTE + ['sh', '-c', 'test ! -e /etc/xrdp-smoke-colorkey && echo clear']).strip() != 'clear':
        raise RuntimeError('Payload already armed')
    snapshot('initial')
    with tempfile.TemporaryDirectory(prefix='gfx-reset-') as control:
        (OUT / 'control-directory.txt').write_text(control + '\n')
        with (OUT / 'display.txt').open('w+') as display_file, \
                (OUT / 'xvfb.log').open('w') as xlog, \
                (OUT / 'client.log').open('w') as clog:
            xserver = subprocess.Popen(
                ['Xvfb', '-displayfd', str(display_file.fileno()),
                 '-screen', '0', '1024x768x24', '-nolisten', 'tcp'],
                pass_fds=(display_file.fileno(),), stdout=xlog, stderr=xlog)
            deadline = time.monotonic() + 5
            while not (OUT / 'display.txt').read_text().strip():
                if xserver.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Fresh Xvfb failed')
                time.sleep(0.1)
            display = ':' + (OUT / 'display.txt').read_text().strip()
            env = dict(os.environ, DISPLAY=display)
            env.pop('FREERDP_ORACLE_DUMP', None)
            geometry = output(['xdotool', 'getdisplaygeometry'], env=env).strip()
            if geometry != '1024 768':
                raise RuntimeError('Unexpected client geometry: ' + geometry)
            run(REMOTE + ['sh', '-c', 'echo 1024x768 > /etc/xrdp-smoke-colorkey'])
            armed = True
            env.update(XRDP_GFX_CONTROL_DIR=control,
                       LD_PRELOAD=str(HERE / 'build/libgfx-reset.so'))
            args = ['/opt/freerdp-vaapi/bin/xfreerdp', '/v:127.0.0.1:40062',
                    '/u:tester', '/p:', '/size:1024x768', '/gfx:AVC444',
                    '/cert:ignore', '/log-level:WARN',
                    '/log-filters:com.freerdp.channels.rdpgfx.client:INFO',
                    '-clipboard', '-auto-reconnect']
            record('connect', {'image': image, 'geometry': geometry,
                               'rendering': True})
            client = subprocess.Popen(args, env=env, stdout=clog, stderr=clog)
            owned = True
            deadline = time.monotonic() + 60

            def control_command(command):
                return output([sys.executable, str(HERE / 'control.py'),
                               control, command]).strip()

            def shot(name):
                path = OUT / (name + '.png')
                run(['ffmpeg', '-hide_banner', '-loglevel', 'error',
                     '-f', 'x11grab', '-video_size', '1024x768', '-i',
                     display + '.0', '-frames:v', '1', '-y', str(path)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                im = Image.open(path).convert('RGB')
                pixels = list(im.crop((400, 300, 600, 500)).getdata())
                return [sum(p[c] for p in pixels) / len(pixels) for c in range(3)]

            window = None
            while time.monotonic() < deadline - 15:
                if client.poll() is not None:
                    raise RuntimeError('Client exited before reset')
                found = subprocess.run(['xdotool', 'search', '--name', 'FreeRDP'],
                                       env=env, stdout=subprocess.PIPE, timeout=3)
                if found.returncode == 0:
                    window = found.stdout.decode().splitlines()[0]
                    run(['xdotool', 'key', '--window', window, 'Return'], env=env)
                if Path(control, 'control.sock').exists():
                    state = control_command('status')
                    if 'confirms=1 ' in state and 'decoded=0 ' not in state:
                        # Wait for the login-time payload, not only login GFX.
                        procs = output(REMOTE + ['ps', '-eo', 'comm'])
                        if 'xterm' in procs:
                            time.sleep(2)
                            run(['xdotool', 'key', '--window', window, 'r'], env=env)
                            time.sleep(1)
                            rgb = shot('before')
                            if rgb[0] > 140 and rgb[1] < 60 and rgb[2] < 60:
                                break
                time.sleep(0.5)
            else:
                raise RuntimeError('Rendered red payload not established')
            record('before', {'control': control_command('status'), 'rgb': rgb})
            snapshot('before')
            record('reset', control_command('reset'))
            time.sleep(3)
            record('after', {'control': control_command('status'),
                             'rgb': shot('after'), 'client_exit': client.poll()})
            snapshot('after')
            if time.monotonic() >= deadline:
                raise RuntimeError('Connection exceeded 60 seconds')
            status = 0
except Exception as exc:
    record('error', str(exc))
finally:
    if armed:
        run(REMOTE + ['rm', '-f', '/etc/xrdp-smoke-colorkey'])
    if owned:
        logoff()
    for process in (client, xserver):
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
    snapshot('final')
    with (OUT / 'server-logs.tar').open('wb') as archive:
        run(REMOTE + ['tar', '-cf', '-', '/var/log/xrdp.log',
                      '/var/log/xrdp-sesman.log', '/var/log/xrdp-perf',
                      '/etc/xrdp/gfx.toml'], timeout=20, stdout=archive)
    record('capture', str(OUT))
sys.exit(status)

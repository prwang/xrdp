#!/usr/bin/env python3
"""Request one reset, or read its state, from the cooperating local client."""
import argparse
import socket
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', help='private XRDP_GFX_CONTROL_DIR')
parser.add_argument('command', choices=['reset', 'status'])
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='reply-', dir=args.directory) as replydir:
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
        client.settimeout(5)
        client.bind(replydir + '/socket')
        client.connect(args.directory + '/control.sock')
        client.send((args.command + '\n').encode())
        reply = client.recv(256).decode()
print(reply, end='')
raise SystemExit(0 if reply.startswith('status=0 ') else 1)

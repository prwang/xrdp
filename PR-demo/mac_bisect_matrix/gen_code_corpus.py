#!/usr/bin/env python3
"""Generate the ANSI syntax-highlighted code corpus for the `code`
bench workload (banner.sh).

Real-tool highlighting, no hand coloring:
  1. lexical pass: pygments CLexer + its solarized-dark style;
  2. semantic overlay: clangd LSP `textDocument/semanticTokens/full`
     (functions/types/macros/parameters colored the way a real editor
     with a language server renders them). If clangd is missing the
     corpus is lexical-only and a WARNING is printed — rerun with
     clangd installed before committing a corpus refresh.

The corpus source is real code from this repo (default: xrdp_mm.c,
5.8k lines) so the scrolled content is long enough that no frame
content repeats within a bench window. The OUTPUT is committed
(code_corpus.ansi) and shipped to the fleet via the xrdp-banner
ConfigMap: the benchmark content is frozen at generation time, so
later source edits do not silently change the payload.

Usage: gen_code_corpus.py [src] [out] [max_lines]
       defaults: /work/xrdp/xrdp_mm.c  ./code_corpus.ansi  3000
"""
import json
import os
import subprocess
import sys

from pygments.lexers.c_cpp import CLexer
from pygments.styles import get_style_by_name

SRC = sys.argv[1] if len(sys.argv) > 1 else '/work/xrdp/xrdp_mm.c'
OUT = sys.argv[2] if len(sys.argv) > 2 else \
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 'code_corpus.ansi')
MAX_LINES = int(sys.argv[3]) if len(sys.argv) > 3 else 3000

BASE0 = '839496'                       # solarized base0 default text
SEMANTIC_COLOR = {                     # solarized editor conventions
    'function': '268bd2', 'method': '268bd2',
    'class': 'b58900', 'type': 'b58900', 'struct': 'b58900',
    'enum': 'b58900', 'typeParameter': 'b58900',
    'macro': 'cb4b16',
    'enumMember': '2aa198',
    'namespace': 'b58900',
}

code = open(SRC).read()
lines = code.split('\n')[:MAX_LINES]
code = '\n'.join(lines) + '\n'

# --- pass 1: pygments lexical colors, one color per character -------
style = get_style_by_name('solarized-dark')
colors = [BASE0] * len(code)
for idx, tok, val in CLexer().get_tokens_unprocessed(code):
    s = style.style_for_token(tok)
    c = s['color'] or BASE0
    for i in range(idx, min(idx + len(val), len(colors))):
        colors[i] = c

# --- line offsets for LSP (line, col) -> char index -----------------
line_off = [0]
for ln in lines:
    line_off.append(line_off[-1] + len(ln) + 1)


def clangd_semantic_tokens():
    proc = subprocess.Popen(
        ['clangd', '--log=error'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL)

    def send(obj):
        b = json.dumps(obj).encode()
        proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b)
        proc.stdin.flush()

    def recv():
        hdr = b''
        while not hdr.endswith(b'\r\n\r\n'):
            c = proc.stdout.read(1)
            if not c:
                raise EOFError('clangd closed the pipe')
            hdr += c
        n = int(hdr.split(b'Content-Length:')[1].split(b'\r\n')[0])
        return json.loads(proc.stdout.read(n))

    uri = 'file://' + os.path.abspath(SRC)
    send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
        'processId': os.getpid(), 'rootUri': 'file:///work',
        'initializationOptions': {'fallbackFlags': [
            '-I/work', '-I/work/common', '-I/work/xrdp',
            '-I/work/libxrdp', '-I/work/libipm']},
        'capabilities': {'textDocument': {'semanticTokens': {
            'requests': {'full': True}, 'tokenTypes': [],
            'tokenModifiers': [], 'formats': ['relative']}}}}})
    init = None
    while init is None:
        m = recv()
        if m.get('id') == 1:
            init = m
    legend = init['result']['capabilities']['semanticTokensProvider'][
        'legend']['tokenTypes']
    send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
    send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {
        'textDocument': {'uri': uri, 'languageId': 'c', 'version': 1,
                         'text': code}}})
    send({'jsonrpc': '2.0', 'id': 2, 'method':
          'textDocument/semanticTokens/full',
          'params': {'textDocument': {'uri': uri}}})
    data = None
    while data is None:
        m = recv()
        if m.get('id') == 2:
            data = m['result']['data']
    proc.kill()
    toks = []
    line = col = 0
    for i in range(0, len(data), 5):
        dl, dc, length, ttype, _ = data[i:i + 5]
        line += dl
        col = col + dc if dl == 0 else dc
        toks.append((line, col, length, legend[ttype]))
    return toks


sem_applied = 0
try:
    for line, col, length, tname in clangd_semantic_tokens():
        c = SEMANTIC_COLOR.get(tname)
        if c is None or line >= len(lines):
            continue
        start = line_off[line] + col
        for i in range(start, min(start + length, len(colors))):
            colors[i] = c
        sem_applied += 1
except (OSError, EOFError, KeyError) as e:
    print('WARNING: clangd semantic overlay unavailable (%s) — '
          'lexical-only corpus' % e, file=sys.stderr)

# --- render: run-length truecolor escapes per line ------------------
with open(OUT, 'w') as f:
    for n, ln in enumerate(lines):
        cur = None
        out = []
        base = line_off[n]
        for i, ch in enumerate(ln):
            c = colors[base + i]
            if c != cur:
                out.append('\x1b[38;2;%d;%d;%dm'
                           % (int(c[0:2], 16), int(c[2:4], 16),
                              int(c[4:6], 16)))
                cur = c
            out.append(ch)
        out.append('\x1b[0m')
        f.write(''.join(out) + '\n')

print('%s: %d lines from %s, %d semantic tokens applied, %d bytes'
      % (OUT, len(lines), SRC, sem_applied, os.path.getsize(OUT)))

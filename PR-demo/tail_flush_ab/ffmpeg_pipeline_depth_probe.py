#!/usr/bin/env python3
"""Definitive one-behind test for h264_vaapi. Feed N frames holding stdin open,
count COMPLETE encoded frames (VCL slice NALs; HW encoder = 1 slice/frame) while
open vs after EOF. open<fed  => genuine tail withhold that only EOF/next flushes."""
import subprocess, threading, time
W,H=320,240; FRAME=W*H*3//2; NFRAMES=6
def make_frame(i):
    y=bytes(((i*37+j*3)&0xff) for j in range(W*H))
    uv=bytes(((i*53+j*7)&0xff) for j in range(W*H//2))
    return y+uv
def count_vcl(buf):
    n=0;i=0;L=len(buf)
    while i+4<L:
        if buf[i]==0 and buf[i+1]==0 and buf[i+2]==1:
            if (buf[i+3]&0x1f) in (1,5): n+=1
            i+=3
        elif buf[i]==0 and buf[i+1]==0 and buf[i+2]==0 and buf[i+3]==1:
            if (buf[i+4]&0x1f) in (1,5): n+=1
            i+=4
        else: i+=1
    return n
def run(label, extra, pair=False):
    args=["ffmpeg","-hide_banner","-loglevel","error","-f","rawvideo","-pix_fmt","nv12",
          "-s",f"{W}x{H}","-r","30","-i","pipe:0"]+extra+["-f","h264","pipe:1"]
    p=subprocess.Popen(args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    out=bytearray(); stop=threading.Event()
    def reader():
        while not stop.is_set():
            b=p.stdout.read(4096)
            if not b: break
            out.extend(b)
    threading.Thread(target=reader,daemon=True).start()
    fed=0
    for i in range(NFRAMES):
        if pair:
            p.stdin.write(make_frame(2*i)); p.stdin.write(make_frame(2*i+1)); fed+=2
        else:
            p.stdin.write(make_frame(i)); fed+=1
        p.stdin.flush(); time.sleep(0.15)
    time.sleep(1.0)                       # long idle, stdin STILL OPEN
    open_f=count_vcl(out)
    p.stdin.close();
    try: p.wait(timeout=5)
    except Exception: p.kill()
    stop.set(); time.sleep(0.1); eof_f=count_vcl(out)
    tag = "ONE-BEHIND" if eof_f>open_f else "no withhold"
    print(f"[{label}]  fed={fed}  frames_out(stdin OPEN)={open_f}  after EOF={eof_f}  -> {tag} (+{eof_f-open_f})")
VA=["-vaapi_device","/dev/dri/renderD128","-vf","format=nv12,hwupload","-c:v","h264_vaapi","-rc_mode","CQP","-qp","20","-bf","0"]
run("h264_vaapi async_depth 1 (single frames)", VA+["-async_depth","1"])
run("h264_vaapi async_depth 1 (PAIRS main+aux)", VA+["-async_depth","1"], pair=True)
run("h264_vaapi async_depth 2", VA+["-async_depth","2"])
run("h264_vaapi async_depth 4 (default-ish)", VA+["-async_depth","4"])
run("libx264 zerolatency threads=1 (single)", ["-c:v","libx264","-threads","1","-bf","0","-preset","ultrafast","-tune","zerolatency","-crf","18"])
print("\n(1 frame idle @30fps = 33ms; a ONE-BEHIND tail is exactly the observed 'last char withheld until next input')")

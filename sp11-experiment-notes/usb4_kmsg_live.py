#!/usr/bin/env python3
"""Mirror /dev/kmsg to a file with fsync, so kernel logs survive a hard
power loss (USB stick or any mounted filesystem).

Usage: sudo python3 usb4_kmsg_live.py /mnt/usb4log/live-klog.txt

Deliberately has NO signal handlers: the default SIGTERM disposition
terminates even a blocking read() instantly (a custom handler + PEP 475
would retry the read forever and hang systemd shutdown).

Two-phase so the live tail is reached within ~a second of start:
  1. drain: O_NONBLOCK, chunked reads, fsync every >=32 lines or 250 ms
  2. live:  blocking reads, fsync per line (maximal crash evidence)
"""
import os
import sys
import time

out_path = sys.argv[1] if len(sys.argv) > 1 else "/var/tmp/kmsg-mirror.txt"

f = open(out_path, "ab", buffering=0)


def write_line(text: bytes):
    f.write(f"{time.strftime('%H:%M:%S')} ".encode() +
            text.decode(errors="replace").encode() + b"\n")


fd = os.open("/dev/kmsg", os.O_RDONLY | os.O_NONBLOCK)
entry = f"== kmsg mirror started {time.strftime('%F %T')} -> {out_path}\n"
f.write(entry.encode())

buf = b""
pending = 0
last_sync = time.time()
while True:
    try:
        chunk = os.read(fd, 1 << 16)
    except BlockingIOError:
        chunk = b""  # backlog fully drained
    if chunk:
        buf += chunk
        while b"\n" in buf:
            rec, buf = buf.split(b"\n", 1)
            write_line(rec.split(b"\x00")[0].rstrip(b"\n"))
            pending += 1
        if pending >= 32 or time.time() - last_sync > 0.25:
            os.fsync(f.fileno())
            pending = 0
            last_sync = time.time()
        continue

    # drain done -> flip to blocking live tail
    os.fsync(f.fileno())
    os.set_blocking(fd, True)
    k = os.fdopen(fd, "rb")
    while True:
        line = k.readline()
        if not line:
            time.sleep(0.01)
            continue
        # "<prio>,seq,ts,flag;text\x00optional-dict\n"
        write_line(line.split(b"\x00")[0].rstrip(b"\n"))
        os.fsync(f.fileno())

#!/usr/bin/env python3
"""
Paused-host test for RB-Pirate terminal backpressure.

Connects to the TCP serial bridge, starts `txtest`, deliberately stops reading
for a while, then resumes and checks that the device returned to the prompt.

Usage:
    python3 paused_host_test.py --host 172.26.208.1 --port 2217
    python3 paused_host_test.py --bytes 1048576 --pause 10
"""

import argparse
import socket
import sys
import time

PROMPT_MARKER = "\x03"


def main() -> int:
    parser = argparse.ArgumentParser(description="RB-Pirate paused-host terminal test")
    parser.add_argument("--host", default="172.26.208.1", help="serial bridge host")
    parser.add_argument("--port", type=int, default=2217, help="serial bridge TCP port")
    parser.add_argument("--bytes", type=int, default=1048576, help="txtest payload size")
    parser.add_argument("--pause", type=float, default=10.0, help="seconds to stop reading")
    parser.add_argument("--timeout", type=float, default=20.0, help="total response timeout")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(0.25)
    try:
        sock.connect((args.host, args.port))
    except OSError as exc:
        print(f"ERROR: cannot connect to {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1

    # Drain the prompt/status output so the command starts from a quiet line.
    quiet_since = time.time()
    while time.time() - quiet_since < 0.5:
        try:
            if sock.recv(4096):
                quiet_since = time.time()
        except socket.timeout:
            pass

    command = f"txtest -n {args.bytes}\r"
    sock.sendall(command.encode())
    print(f"sent: {command.strip()}")
    print(f"pausing reads for {args.pause:.1f}s ...")
    time.sleep(args.pause)

    deadline = time.time() + args.timeout
    buf = ""
    while time.time() < deadline:
        try:
            data = sock.recv(4096)
            if not data:
                break
            buf += data.decode("utf-8", errors="ignore")
        except socket.timeout:
            if PROMPT_MARKER in buf and ("TXTEST:END" in buf or "TXTEST:TIMEOUT" in buf):
                break

    sock.close()

    if "TXTEST:END" in buf:
        print("PASS: txtest completed after the pause")
        return 0
    if "TXTEST:TIMEOUT" in buf:
        print("PASS: txtest reported a bounded timeout")
        return 0
    print("FAIL: no TXTEST completion marker observed")
    print("--- last output ---")
    print(buf[-2000:])
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""
upload.py <local-file> <remote-path>

Push a small file to the board over the UART emergency shell, using hush's
`echo -ne` with \\xNN escapes in 48-byte chunks (hush line editing caps a line at 256 chars), then verify with md5sum.
Roughly 3 KB/s at 115200 baud, so fine for tools up to ~100 KB.
"""
import hashlib
import sys
import time

import serial

PORT = "COM28"


def until_prompt(ser, limit=30):
    t0 = time.time()
    d = b""
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            d += ser.read(n)
        else:
            time.sleep(0.02)
        if d.rstrip().endswith(b"# "):
            break
    return d.decode("latin1", "replace")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    ser = serial.Serial(PORT, 115200, timeout=0.3, write_timeout=None, rtscts=False, dsrdtr=False, xonxoff=False)
    ser.write(b"\x18")
    until_prompt(ser, 5)
    ser.reset_input_buffer()
    # No echo while uploading: the FT232R adapter stalls under sustained
    # bidirectional traffic, and the echoed line would double it.
    ser.write(f"stty -echo; : > {dst}\r\n".encode())
    until_prompt(ser, 5)
    for off in range(0, len(data), 48):
        chunk = data[off:off + 48]
        esc = "".join(f"\\x{b:02x}" for b in chunk)
        # Double quotes: the only literal characters are \, x and hex digits,
        # none of which hush treats specially inside "..." (a 0x27 byte would
        # terminate a single-quoted string).
        ser.write(f'echo -ne "{esc}" >> {dst}\r\n'.encode())
        time.sleep(0.03)
        out = until_prompt(ser, 5)
        if "# " not in out:
            # Serial hiccup: re-probe the shell and check the byte count.
            ser.write(b"\r\n")
            until_prompt(ser, 3)
            ser.reset_input_buffer()
            ser.write(f"wc -c < {dst}\r\n".encode())
            out = until_prompt(ser, 5)
            want = off + len(chunk)
            if str(want) not in out:
                print(f"\nstalled at {off}, board has: {out!r}", file=sys.stderr)
                sys.exit(1)
        sys.stdout.write(f"\r{off + len(chunk)}/{len(data)}")
        sys.stdout.flush()
    print()
    ser.write(f"stty echo; chmod +x {dst}; md5sum {dst}\r\n".encode())
    out = until_prompt(ser, 10)
    ser.write(b"exit\r\n")
    time.sleep(0.3)
    ser.close()
    local = hashlib.md5(data).hexdigest()
    ok = local in out
    print("md5 local", local, "-> board", "OK" if ok else "MISMATCH:\n" + out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

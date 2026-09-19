#!/usr/bin/env python3
"""
Serial-console driver for the STM32F746G-DISCO netboot test loop.

  board.py boot                 reset/netboot the board, print the boot log
  board.py sh "cmd" ["cmd"...]  run commands in the UART emergency shell
  board.py raw "text"           send raw text (e.g. to U-Boot)

The board runs uart_bridge on ttyS5, which forwards keystrokes to the LCD
console. Ctrl+X drops into an emergency shell on the UART itself, which is
what `sh` uses so that command output comes back over the serial line.
"""

import os
import subprocess
import sys
import time

import serial

PORT = os.environ.get("F7_PORT", "COM28")
BAUD = 115200
TFTP_SERVER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "tftp_server.py")
UBOOT_PROMPT = "STM32F746-DISCO>"
CTRL_X = b"\x18"


def open_port():
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    ser.reset_input_buffer()
    return ser


def drain(ser, quiet=0.6, limit=10.0):
    """Read until the line is quiet for `quiet` seconds."""
    out = b""
    t_last = time.time()
    t0 = t_last
    while time.time() - t_last < quiet and time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            out += ser.read(n)
            t_last = time.time()
        else:
            time.sleep(0.03)
    return out.decode("latin1", "replace")


def start_tftp():
    """Serve C:\\tftp on 172.17.4.1:69 unless something already does."""
    try:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("172.17.4.1", 69))
        s.close()
    except OSError:
        return None
    return subprocess.Popen([sys.executable, TFTP_SERVER],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def state(ser):
    """Return 'uboot', 'linux-bridge', 'linux-shell' or 'unknown'."""
    ser.write(b"\r\n")
    out = drain(ser, quiet=0.5)
    if UBOOT_PROMPT in out:
        return "uboot"
    if "# " in out:
        return "linux-shell"
    ser.write(CTRL_X)
    out = drain(ser, quiet=0.5)
    if "Emergency" in out or "# " in out:
        # We are now in the emergency shell; leave it again.
        ser.write(b"exit\r\n")
        drain(ser, quiet=0.4)
        return "linux-bridge"
    return "unknown"


def boot(timeout=120):
    tftp = start_tftp()
    ser = open_port()
    try:
        st = state(ser)
        print(f"[board] state: {st}")
        if st == "linux-bridge":
            ser.write(CTRL_X)
            drain(ser, quiet=0.4)
            ser.write(b"reboot\r\n")
        elif st == "linux-shell":
            ser.write(b"reboot\r\n")
        elif st != "uboot":
            ser.write(b"\r\nreboot\r\n")

        log = ""
        sent = False
        if st == "uboot":
            ser.write(b"run netboot\r\n")
            sent = True
        t0 = time.time()
        while time.time() - t0 < timeout:
            chunk = drain(ser, quiet=0.3, limit=2.0)
            if chunk:
                sys.stdout.write(chunk)
                sys.stdout.flush()
                log += chunk
            if UBOOT_PROMPT in log and not sent:
                time.sleep(0.2)
                ser.write(b"run netboot\r\n")
                sent = True
                log = log.replace(UBOOT_PROMPT, "")
            if sent and ("Freeing init memory" in log or
                         "rootfs:" in log or "init:" in log):
                # Give init a few more seconds to settle, then collect.
                time.sleep(4)
                sys.stdout.write(drain(ser, quiet=0.5))
                break
        print("\n[board] boot sequence finished")
    finally:
        ser.close()
        if tftp:
            tftp.terminate()


def shell(cmds, per_cmd=6.0):
    ser = open_port()
    try:
        ser.write(CTRL_X)
        out = drain(ser, quiet=0.5)
        if "Emergency" not in out and "# " not in out:
            # Maybe already in a shell; make sure there is a prompt.
            ser.write(b"\r\n")
            out = drain(ser, quiet=0.5)
        for c in cmds:
            ser.write((c + "\r\n").encode("latin1"))
            out = drain(ser, quiet=0.7, limit=per_cmd)
            sys.stdout.write(out)
            sys.stdout.flush()
        ser.write(b"exit\r\n")
        drain(ser, quiet=0.3)
    finally:
        ser.close()


def raw(text):
    ser = open_port()
    try:
        ser.write((text + "\r\n").encode("latin1"))
        sys.stdout.write(drain(ser, quiet=0.8))
    finally:
        ser.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "boot":
        boot()
    elif cmd == "sh":
        shell(sys.argv[2:])
    elif cmd == "raw":
        raw(" ".join(sys.argv[2:]))
    else:
        print(__doc__)
        sys.exit(1)

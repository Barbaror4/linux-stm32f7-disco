"""lines.py "cmd1" "cmd2" ... - run short commands one per line in the UART
emergency shell, printing each one's output. Recovers a continuation prompt
first. Keeps lines short because the FT232R link garbles long ones."""
import sys
import time

import serial


def until_prompt(ser, limit):
    t0 = time.time()
    d = b""
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            d += ser.read(n)
        else:
            time.sleep(0.03)
        if d.rstrip().endswith(b"# ") or d.rstrip().endswith(b"> "):
            time.sleep(0.2)
            if not ser.in_waiting:
                break
    return d.decode("latin1", "replace")


def main():
    ser = serial.Serial("COM28", 115200, timeout=0.3)
    ser.write(b"\x18")
    time.sleep(0.6)
    ser.read(ser.in_waiting or 1)
    # Break out of any continuation prompt left by a garbled line.
    for _ in range(3):
        ser.write(b"\r\n")
        out = until_prompt(ser, 2)
        if out.rstrip().endswith("> "):
            ser.write(b"\x04")
            until_prompt(ser, 2)
        else:
            break
    for cmd in sys.argv[1:]:
        ser.reset_input_buffer()
        ser.write(cmd.encode("latin1") + b"\r\n")
        out = until_prompt(ser, 60)
        lines = out.replace("\r", "").split("\n")
        body = [l for l in lines[1:] if not l.endswith("# ") and l.strip()]
        print("\n".join(body))
    ser.write(b"exit\r\n")
    time.sleep(0.3)
    ser.close()


if __name__ == "__main__":
    main()

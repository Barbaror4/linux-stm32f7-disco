import serial, sys, time

d = open(sys.argv[1], "rb").read()
off = int(sys.argv[2])
chunk = d[off:off + 48]
esc = "".join(f"\\x{b:02x}" for b in chunk)
s = serial.Serial("COM28", 115200, timeout=0.3, write_timeout=5)
s.reset_input_buffer()
line = f'echo -ne "{esc}" >> /tmp/x\r\n'.encode()
print("line length", len(line))
s.write(line)
t0 = time.time()
out = b""
while time.time() - t0 < 3:
    n = s.in_waiting
    if n:
        out += s.read(n)
    else:
        time.sleep(0.05)
print(len(out), repr(out[-300:]))
s.close()

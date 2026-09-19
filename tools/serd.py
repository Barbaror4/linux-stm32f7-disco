"""
serd.py - keep COM28 open and continuously drained; serve commands over a
local TCP socket so short-lived scripts never open/close the port.

Protocol (one request per connection, UTF-8):
    RAW <text>\n      -> write text (\r\n appended), reply with output
                         collected until the shell prompt or a quiet period
    SH <cmd>\n        -> Ctrl+X into the emergency shell, run cmd, exit
    LOG\n             -> return everything received since the last LOG
"""
import socket
import threading
import time

import serial

PORT = "COM28"
LISTEN = ("127.0.0.1", 5028)

ser = serial.Serial(PORT, 115200, timeout=0.05)
lock = threading.Lock()
buf = bytearray()
logbuf = bytearray()


def reader():
    while True:
        try:
            n = ser.in_waiting
            data = ser.read(n) if n else ser.read(1)
        except Exception as e:  # noqa: BLE001
            time.sleep(0.5)
            continue
        if data:
            with lock:
                buf.extend(data)
                logbuf.extend(data)
        else:
            time.sleep(0.01)


def take():
    with lock:
        d = bytes(buf)
        buf.clear()
    return d


def wait_prompt(limit, quiet=0.6):
    t0 = time.time()
    last = time.time()
    d = b""
    while time.time() - t0 < limit:
        chunk = take()
        if chunk:
            d += chunk
            last = time.time()
            if d.rstrip().endswith(b"# "):
                time.sleep(0.15)
                if not buf:
                    break
        elif time.time() - last > quiet and d:
            break
        else:
            time.sleep(0.02)
    return d.decode("latin1", "replace")


def handle(conn):
    req = b""
    while not req.endswith(b"\n"):
        c = conn.recv(4096)
        if not c:
            return
        req += c
    kind, _, arg = req.decode("utf-8").rstrip("\n").partition(" ")
    if kind == "LOG":
        with lock:
            d = bytes(logbuf)
            logbuf.clear()
        conn.sendall(d)
        return
    if kind == "SH":
        take()
        ser.write(b"\x18")
        wait_prompt(4)
        take()
        ser.write(arg.encode("latin1") + b"\r\n")
        out = wait_prompt(150, quiet=3.0)
        ser.write(b"exit\r\n")
        wait_prompt(2)
        conn.sendall(out.encode("latin1", "replace"))
        return
    if kind == "RAW":
        take()
        ser.write(arg.encode("latin1") + b"\r\n")
        out = wait_prompt(30, quiet=1.0)
        conn.sendall(out.encode("latin1", "replace"))
        return


def main():
    threading.Thread(target=reader, daemon=True).start()
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(LISTEN)
    srv.listen(4)
    while True:
        conn, _ = srv.accept()
        try:
            handle(conn)
        finally:
            conn.close()


if __name__ == "__main__":
    main()

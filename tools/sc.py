"""sc.py [sh|raw|log] <text> - client for serd.py"""
import socket
import sys

kind = sys.argv[1].upper()
arg = " ".join(sys.argv[2:])
s = socket.create_connection(("127.0.0.1", 5028), timeout=180)
s.sendall(f"{kind} {arg}\n".encode("utf-8"))
s.shutdown(socket.SHUT_WR)
out = b""
while True:
    c = s.recv(65536)
    if not c:
        break
    out += c
text = out.decode("latin1", "replace").replace("\r", "")
if kind == "SH":
    lines = text.split("\n")
    # drop the echoed command line and the prompts
    text = "\n".join(l for l in lines[1:] if not l.endswith("# ") and l.strip())
sys.stdout.write(text + ("\n" if text and not text.endswith("\n") else ""))

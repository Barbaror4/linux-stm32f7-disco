"""Send UDP datagrams to the board for N seconds so the host keeps ARPing."""
import socket
import sys
import time

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.bind(("172.17.4.1", 0))
t0 = time.time()
n = 0
while time.time() - t0 < secs:
    s.sendto(b"x" * 32, ("172.17.4.255", 9999))
    n += 1
    time.sleep(0.1)
print("sent", n)

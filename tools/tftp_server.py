import socket
import struct
import os
import sys
import threading
import time

TFTP_ROOT = r'C:\tftp'
HOST = '172.17.4.1'
PORT = 69

OP_RRQ   = 1
OP_WRQ   = 2
OP_DATA  = 3
OP_ACK   = 4
OP_ERROR = 5
OP_OACK  = 6

def handle_client(sock, client_addr, filepath, blksize):
    try:
        with open(filepath, 'rb') as f:
            data = f.read()
    except Exception as e:
        err_msg = str(e).encode('ascii')
        err_pkt = struct.pack('!HH', OP_ERROR, 1) + err_msg + b'\x00'
        sock.sendto(err_pkt, client_addr)
        sock.close()
        return

    total_len = len(data)
    print(f"[TFTP] Serving {filepath} ({total_len} bytes) to {client_addr} with blksize={blksize}")

    block_num = 0
    if blksize != 512:
        # Send OACK
        oack = struct.pack('!H', OP_OACK) + b'blksize\x00' + str(blksize).encode('ascii') + b'\x00'
        sock.sendto(oack, client_addr)
        # Wait for ACK 0
        sock.settimeout(2.0)
        try:
            ack, _ = sock.recvfrom(512)
            op, ack_blk = struct.unpack('!HH', ack[:4])
            if op != OP_ACK or ack_blk != 0:
                print(f"[TFTP] Unexpected response to OACK: {ack}")
                sock.close()
                return
        except socket.timeout:
            print("[TFTP] Timeout waiting for ACK 0")
            sock.close()
            return

    offset = 0
    block_num = 1
    t0 = time.time()
    sock.settimeout(3.0)

    while True:
        chunk = data[offset : offset + blksize]
        pkt = struct.pack('!HH', OP_DATA, block_num & 0xFFFF) + chunk
        
        # Pace packet slightly to prevent Cortex-M7 RX overflow
        time.sleep(0.001)
        # Send with retries
        for retry in range(5):
            sock.sendto(pkt, client_addr)
            try:
                ack, addr = sock.recvfrom(512)
                if len(ack) >= 4:
                    op, ack_blk = struct.unpack('!HH', ack[:4])
                    if op == OP_ACK and ack_blk == (block_num & 0xFFFF):
                        break
            except socket.timeout:
                print(f"[TFTP] Timeout on block {block_num}, retry {retry+1}...")
        else:
            print(f"[TFTP] Transfer failed: too many retries at block {block_num}")
            break

        offset += len(chunk)
        block_num += 1

        if len(chunk) < blksize:
            # Last packet
            elapsed = time.time() - t0
            print(f"[TFTP] Transfer complete: {total_len} bytes in {elapsed:.2f}s ({total_len/1024/elapsed:.1f} KB/s)")
            break

    sock.close()

def run_server():
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((HOST, PORT))
    print(f"[TFTP Server] Listening on {HOST}:{PORT}, serving files from {TFTP_ROOT}")

    while True:
        data, client_addr = server_sock.recvfrom(1024)
        if len(data) < 4:
            continue
        opcode = struct.unpack('!H', data[:2])[0]
        if opcode != OP_RRQ:
            continue

        # Parse RRQ: filename\0mode\0[opt1\0val1\0...]
        parts = data[2:].split(b'\x00')
        if len(parts) < 2:
            continue
        filename = parts[0].decode('ascii', errors='replace').strip('/')
        mode = parts[1].decode('ascii', errors='replace').lower()

        # Parse options
        options = {}
        idx = 2
        while idx + 1 < len(parts) and parts[idx]:
            opt_name = parts[idx].decode('ascii', errors='replace').lower()
            opt_val = parts[idx+1].decode('ascii', errors='replace')
            options[opt_name] = opt_val
            idx += 2

        blksize = int(options.get('blksize', 512))
        filepath = os.path.join(TFTP_ROOT, filename)

        # Create worker socket on ephemeral port
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client_sock.bind((HOST, 0))

        t = threading.Thread(target=handle_client, args=(client_sock, client_addr, filepath, blksize), daemon=True)
        t.start()

if __name__ == '__main__':
    run_server()

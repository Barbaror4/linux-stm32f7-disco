import struct
import zlib
import time
import sys
import os

IH_MAGIC = 0x27051956
IH_OS_LINUX = 5
IH_ARCH_ARM = 2
IH_TYPE_KERNEL = 2
IH_COMP_NONE = 0

def make_uimage(image_path, output_path, load_addr=0xc0008000, ep_addr=0xc0008001, name="Linux kernel0"):
    with open(image_path, 'rb') as f:
        data = f.read()

    data_size = len(data)
    data_crc = zlib.crc32(data) & 0xFFFFFFFF
    timestamp = int(time.time())

    name_bytes = name.encode('ascii')[:32].ljust(32, b'\x00')

    # Construct header with header_crc = 0
    header_no_crc = struct.pack(
        '>IIIIIIIBBBB32s',
        IH_MAGIC,
        0,              # hcrc placeholder
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        IH_OS_LINUX,
        IH_ARCH_ARM,
        IH_TYPE_KERNEL,
        IH_COMP_NONE,
        name_bytes
    )

    header_crc = zlib.crc32(header_no_crc) & 0xFFFFFFFF

    # Construct final header
    header = struct.pack(
        '>IIIIIIIBBBB32s',
        IH_MAGIC,
        header_crc,
        timestamp,
        data_size,
        load_addr,
        ep_addr,
        data_crc,
        IH_OS_LINUX,
        IH_ARCH_ARM,
        IH_TYPE_KERNEL,
        IH_COMP_NONE,
        name_bytes
    )

    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(data)

    print(f"Created uImage '{output_path}': size={len(header) + len(data)} bytes, load=0x{load_addr:08x}, entry=0x{ep_addr:08x}")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python mkimage.py <input_image> <output_uimage> [load_addr] [entry_addr] [name]")
        sys.exit(1)
    img = sys.argv[1]
    out = sys.argv[2]
    load = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0xc0008000
    ep = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0xc0008001
    name = sys.argv[5] if len(sys.argv) > 5 else "Linux kernel0"
    make_uimage(img, out, load, ep, name)

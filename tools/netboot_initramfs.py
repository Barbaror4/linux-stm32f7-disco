import serial, time, sys, subprocess
# reboot, stop at U-Boot, netboot with sdroot=off (initramfs root)
tftp = subprocess.Popen([sys.executable, 'tftp_server.py'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
ser = serial.Serial('COM28', 115200, timeout=0.3)
def drain(quiet=0.5, limit=60):
    t0=time.time(); last=time.time(); d=b''
    while time.time()-t0 < limit:
        n=ser.in_waiting
        if n: d+=ser.read(n); last=time.time()
        elif time.time()-last > quiet: break
        else: time.sleep(0.05)
    return d.decode('latin1','replace')
ser.write(b'\x18'); time.sleep(0.8); ser.read(ser.in_waiting or 1)
ser.write(b'reboot\r\n')
t0=time.time(); buf=''
while time.time()-t0 < 60:
    buf += drain(0.2, 1.0)
    if 'Hit any key' in buf:
        ser.write(b'\r\n'); time.sleep(0.4); ser.write(b'\r\n'); break
time.sleep(0.5); ser.reset_input_buffer()
ser.write(b"setenv args 'setenv bootargs stm32_platform=stm32f7-disco console=ttyS5,115200 panic=10 sdroot=off'\r\n"); drain(0.5,5)
ser.write(b'run netboot\r\n')
t0=time.time(); log=''
while time.time()-t0 < 120:
    log += drain(0.3, 2.0)
    if 'uart_bridge' in log: break
print('\n'.join(l for l in log.split('\n') if 'init:' in l or 'uart_bridge' in l or 'Link is' in l))
ser.close(); tftp.terminate()

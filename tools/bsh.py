import serial, time, sys
# robust: run commands in the emergency shell, print output; ignores VCP replay junk
ser = serial.Serial('COM28', 115200, timeout=0.3, write_timeout=5)
def until_prompt(limit):
    t0=time.time(); d=b''
    while time.time()-t0 < limit:
        n=ser.in_waiting
        if n: d+=ser.read(n)
        else: time.sleep(0.05)
        if d.rstrip().endswith(b'# '): 
            time.sleep(0.3)
            if ser.in_waiting: continue
            break
    return d.decode('latin1','replace')
ser.write(b'\x18'); until_prompt(5)
for c in sys.argv[1:]:
    ser.reset_input_buffer()
    ser.write((c+'\r\n').encode())
    out = until_prompt(300)
    print('\n'.join(l for l in out.split('\n') if 'bridge.]' not in l and 'Emergency' not in l))
ser.write(b'exit\r\n'); time.sleep(0.3); ser.close()

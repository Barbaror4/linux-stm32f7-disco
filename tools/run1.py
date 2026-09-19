import serial,time,sys
s=serial.Serial('COM28',115200,timeout=0.3,write_timeout=5)
def until_prompt(limit):
    t0=time.time(); d=b''
    while time.time()-t0 < limit:
        n=s.in_waiting
        if n: d+=s.read(n)
        else: time.sleep(0.05)
        if d.rstrip().endswith(b'# '):
            time.sleep(0.4)
            if s.in_waiting: continue
            break
    return d.decode('latin1','replace')
s.reset_input_buffer()
s.write(sys.argv[1].encode()+b'\r\n')
out=until_prompt(float(sys.argv[2]) if len(sys.argv)>2 else 60)
print('\n'.join(l for l in out.split('\n') if 'bridge.]' not in l and 'Emergency' not in l))
s.close()

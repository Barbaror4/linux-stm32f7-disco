import serial,time,sys
s=serial.Serial('COM28',115200,timeout=0.3,write_timeout=5)
def rd(t):
    time.sleep(t); return s.read(s.in_waiting or 1).decode('latin1','replace')
s.write(b'\x03'); rd(0.5); s.write(b'\r\n'); print('P1', repr(rd(1)[-80:]))
s.write(b'\x18'); print('P2', repr(rd(1.5)[-120:]))
s.write(b'\r\n'); print('P3', repr(rd(1)[-80:]))
s.close()

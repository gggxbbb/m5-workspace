import serial, time, sys

s = serial.Serial('COM9', 115200, timeout=2)
time.sleep(0.3)
s.reset_input_buffer()
out = open('/tmp/imu.log', 'wb', buffering=0)
end = time.time() + 300   # 5 min cap
while time.time() < end:
    try:
        data = s.read(512)
        if data: out.write(data)
    except Exception as e:
        out.write(('\n[err] %s\n' % e).encode())
        time.sleep(1)
        try:
            s.close(); s = serial.Serial('COM9', 115200, timeout=2)
        except Exception:
            pass
s.close()

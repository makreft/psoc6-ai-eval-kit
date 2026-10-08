import sys, time, serial
port = sys.argv[1]
def listen(ser, seconds):
    buf = b""; end = time.time() + seconds
    while time.time() < end:
        n = ser.in_waiting
        if n: buf += ser.read(n)
        else: time.sleep(0.05)
    return buf
with serial.Serial(port, 115200, timeout=0, dsrdtr=False, rtscts=False) as ser:
    ser.dtr = True; ser.rts = True
    time.sleep(0.3); ser.reset_input_buffer()
    for label, payload, wait in [
        ("Enter", b"\r", 1.0),
        ("Passwort imagimob", b"imagimob\r", 1.5),
        ("version", b"version\r", 1.5),
        ("help", b"help\r", 1.5),
        ("Enter x3", b"\r\r\r", 1.0),
    ]:
        ser.write(payload); ser.flush()
        got = listen(ser, wait)
        print(f"  {label}: {len(got)} Bytes" + (f" -> {got[:200]!r}" if got else ""))
    print("=== Passiv 5 s (Board-Reset jetzt möglich) ===")
    got = listen(ser, 5.0)
    print(f"  {len(got)} Bytes" + (f" -> {got[:400]!r}" if got else ""))

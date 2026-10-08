import sys, time, serial

port = sys.argv[1]

def listen(ser, seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        else:
            time.sleep(0.05)
    return buf

def show(label, data):
    if data:
        print(f"  {label}: {len(data)} Bytes")
        print("    text:", repr(data[:300].decode(errors='replace')))
        print("    hex :", data[:64].hex(' '))
    else:
        print(f"  {label}: nichts")

print("=== Passives Mithören 115200, 3 s (streamt die Firmware von selbst?) ===")
with serial.Serial(port, 115200, timeout=0, dsrdtr=False, rtscts=False) as ser:
    ser.dtr = True; ser.rts = True
    ser.reset_input_buffer()
    show("passiv", listen(ser, 3.0))

for baud in [115200, 9600, 921600, 230400, 460800, 57600, 38400, 1000000, 2000000]:
    print(f"=== {baud} Baud ===")
    with serial.Serial(port, baud, timeout=0, dsrdtr=False, rtscts=False) as ser:
        ser.dtr = True; ser.rts = True
        time.sleep(0.2)
        ser.reset_input_buffer()
        for eol_name, eol in [("CRLF", b"\r\n"), ("CR", b"\r"), ("LF", b"\n")]:
            ser.write(eol); ser.flush()
            got = listen(ser, 0.5)
            ser.write(b"help" + eol); ser.flush()
            got += listen(ser, 1.0)
            show(f"help+{eol_name}", got)
            if got:
                break

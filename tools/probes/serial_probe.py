import sys, os, termios, time, select

port = sys.argv[1]
cmds = sys.argv[2:] or ["help"]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
# raw mode, 115200 8N1, no flow control
attrs[0] = 0                                   # iflag
attrs[1] = 0                                   # oflag
attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
attrs[3] = 0                                   # lflag
attrs[4] = termios.B115200
attrs[5] = termios.B115200
attrs[6][termios.VMIN] = 0
attrs[6][termios.VTIME] = 0
termios.tcsetattr(fd, termios.TCSANOW, attrs)
termios.tcflush(fd, termios.TCIOFLUSH)

def read_for(seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                chunk = os.read(fd, 4096)
                if chunk:
                    buf += chunk
            except BlockingIOError:
                pass
    return buf

# wake the console
os.write(fd, b"\r\n")
banner = read_for(1.0)
print("=== nach Enter ===")
print(banner.decode(errors="replace"))
for c in cmds:
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, (c + "\r\n").encode())
    out = read_for(2.0)
    print(f"=== Antwort auf '{c}' ===")
    print(out.decode(errors="replace"))
os.close(fd)

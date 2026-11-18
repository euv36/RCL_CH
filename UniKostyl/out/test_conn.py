import socket
import sys

host = sys.argv[1]
port = int(sys.argv[2])
sock = socket.socket()
sock.settimeout(3)
try:
    sock.connect((host, port))
    print(host, port, "OPEN")
except Exception as exc:
    print(host, port, "FAIL", exc)
finally:
    sock.close()

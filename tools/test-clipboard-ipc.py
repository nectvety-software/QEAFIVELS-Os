import socket
p="/tmp/qeaf-clip-test.sock"
def req(payload, readn=64):
    s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(p); s.sendall(payload)
    data=s.recv(readn); s.close(); return data
assert req(b"GET\n")==b"EMPTY\n"
text="QEAF clipboard tiếng Việt ✓"
body=text.encode()
assert req(b"SET "+str(len(body)).encode()+b"\n"+body)==b"OK\n"
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(p); s.sendall(b"GET\n")
hdr=b""
while not hdr.endswith(b"\n"): hdr+=s.recv(1)
n=int(hdr.split()[1]); got=b""
while len(got)<n: got+=s.recv(n-len(got))
s.close()
assert got==body, (got,body)
print("PASS: EMPTY, UTF-8 SET/GET round-trip, length framing; bytes=",len(got))

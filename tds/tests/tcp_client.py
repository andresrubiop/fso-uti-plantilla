#!/usr/bin/env python3
"""Cliente TCP mínimo para las pruebas del TD7 (solo biblioteca estándar).

  tcp_client.py echo PORT "texto"          envía líneas y muestra lo que el servidor devuelve
  tcp_client.py blocked PORT               cliente A conectado sin hablar + cliente B que envía "hi":
                                           indica si B recibió su eco en menos de 1 s
"""
import socket
import sys
import time


def echo(port: int, text: str) -> None:
    with socket.create_connection(('127.0.0.1', port), timeout=3) as s:
        for line in text.split('\\n'):
            if not line:
                continue
            s.sendall((line + '\n').encode())
            if line.startswith('quit'):
                break
            print('sent: %-8s received: %s' % (line, s.recv(1024).decode().rstrip('\n')))


def blocked(port: int) -> None:
    a = socket.create_connection(('127.0.0.1', port), timeout=3)   # connected, silent
    time.sleep(0.2)
    b = socket.create_connection(('127.0.0.1', port), timeout=1)
    b.sendall(b'hi\n')
    t0 = time.time()
    try:
        data = b.recv(1024)
        print('client B got %r after %.2f s' % (data.decode(), time.time() - t0))
    except socket.timeout:
        print('client B got nothing after 1 s: the server is busy with client A')
    a.sendall(b'quit\n')
    a.close()
    b.close()


if __name__ == '__main__':
    mode, port = sys.argv[1], int(sys.argv[2])
    if mode == 'echo':
        echo(port, sys.argv[3])
    else:
        blocked(port)

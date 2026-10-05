#!/usr/bin/env python3
"""Local bridge between the course hub (hub/dist/index.html) and a real terminal on this machine.

    python3 tds/terminal.py              listen on 127.0.0.1:8765 and print this session's key
    python3 tds/terminal.py --port 9000

The hub is a single HTML file opened from disk, and a browser page cannot start processes. This
bridge does it for the page: every WebSocket connection gets its own shell (bash) in a real
pseudo-terminal, started in the tds/ folder. The labs' "Run" buttons type their command into it.

Security. A shell reachable from a browser is dangerous: any web site you visit could try to
connect to 127.0.0.1. So the bridge
  * listens on 127.0.0.1 only (never on the network),
  * requires a random 128-bit key, new every time it starts, printed below and sent by the hub,
  * rejects pages from other sites (Origin must be a local file or localhost),
  * serves at most 3 shells, and ends them when the page disconnects or you press Ctrl+C.
It also serves, read-only, the hub page and its materiales/ and videos/ folders (http://127.0.0.1:8765/), only
to requests that name this machine (Host: localhost / 127.0.0.1), so the hub can be opened from the browser too.

GitHub Codespaces (the course repository has a .devcontainer): the bridge also accepts this codespace's own
private address for its port (https://NAME-8765.app.github.dev, from $CODESPACE_NAME), and the link it prints
opens the hub there with ?terminal=aqui.KEY: the page then connects back to the same address. Keep the port private.

Protocol (RFC 6455 WebSocket, path /term?k=KEY). Browser -> bridge, text frames with JSON:
  {"t": "in", "d": "ls\\r"}      keystrokes          {"t": "size", "c": 120, "r": 30}  window size
  {"t": "ready"}                 which tests/tmp/tdN folders exist (created by tests/run.sh tdN)
Bridge -> browser: binary frames with the terminal output, and text frames with JSON:
  {"t": "hello", "v": 1, "tds": "...", "ready": ["td0", ...]}   {"t": "ready", "ready": [...]}
  {"t": "exit", "code": 0}       the shell ended

Standard library only. Made for Ubuntu 26.04 LTS, the course platform (installed, in a VM or in WSL 2). Single-threaded select() loop: os.forkpty() must not
run while other threads exist.
"""
import argparse
import base64
import errno
import fcntl
import hashlib
import hmac
import json
import os
import re
import secrets
import select
import signal
import socket
import struct
import sys
import termios
import time
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'   # RFC 6455, section 1.3
TDS = Path(__file__).resolve().parent
LAB = TDS / 'tests' / 'tmp'                      # tests/run.sh tdN leaves each lab's working folder here
HUB = next((p for p in (TDS.parent / 'hub' / 'dist' / 'index.html', TDS.parent / 'index.html') if p.exists()),
           TDS.parent / 'hub' / 'dist' / 'index.html')   # full project, or the GitHub copy (index.html at the root)
WEB = TDS.parent                                 # served read-only: the hub page and these two folders, nothing else
WEB_DIRS = ('materiales', 'videos')
TYPES = {'.html': 'text/html; charset=utf-8', '.pdf': 'application/pdf', '.js': 'text/javascript; charset=utf-8',
         '.mp4': 'video/mp4', '.webm': 'video/webm', '.vtt': 'text/vtt; charset=utf-8', '.jpg': 'image/jpeg', '.png': 'image/png'}
MAX_SESSIONS = 3
MAX_SENDERS = 32
MAX_MESSAGE = 1 << 20
HANDSHAKE_TIMEOUT = 5.0
SEND_IDLE = 60.0                                 # a download that stops moving (paused video) is dropped after this


def codespace_host(port):
    """In GitHub Codespaces, this port's private forwarded address (NAME-PORT.app.github.dev); None elsewhere."""
    name, domain = os.environ.get('CODESPACE_NAME'), os.environ.get('GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN')
    return f'{name}-{port}.{domain}' if name and domain else None


def allowed_origin(origin, port=None):
    """A page opened from disk sends Origin: null (Firefox) or file:// (Chrome); the hub may also be served from this
    machine, or, in GitHub Codespaces, from this codespace's own private address for this port (and no other site)."""
    if origin is None:          # not a browser (tests, scripts): no web site can be behind it
        return True
    if origin in ('null', 'file://'):
        return True
    u = urlsplit(origin)
    if u.scheme in ('http', 'https') and u.hostname in ('localhost', '127.0.0.1', '::1'):
        return True
    cs = codespace_host(port)
    return cs is not None and origin == f'https://{cs}'


def allowed_host(host, port):
    """Pages are served only when asked for by this machine's name (or this codespace's address): a site that points
    its own name at 127.0.0.1 (DNS rebinding) gets nothing."""
    try:
        name = urlsplit('//' + host).hostname if host else None
    except ValueError:
        return False
    return name in ('localhost', '127.0.0.1', '::1') or (name is not None and name == codespace_host(port))


def web_file(path):
    """The file a URL path names, if the bridge may serve it: the hub page, or a file inside materiales/ or videos/."""
    p = unquote(path)
    if p in ('/', '/index.html', '/' + HUB.relative_to(WEB).as_posix()):
        return HUB if HUB.is_file() else None
    parts = p.strip('/').split('/')
    if parts[0] not in WEB_DIRS or any(x in ('', '.', '..') or '\0' in x for x in parts):
        return None
    try:
        f = WEB.joinpath(*parts).resolve(strict=True)
    except (OSError, RuntimeError):
        return None
    inside = f.is_relative_to((WEB / parts[0]).resolve())
    return f if inside and f.is_file() and f.suffix.lower() in TYPES else None


def ready():
    return sorted(p.name for p in LAB.glob('td[0-8]') if p.is_dir()) if LAB.is_dir() else []


def frame(opcode, payload):
    """Server frames are never masked (RFC 6455, section 5.1)."""
    n = len(payload)
    if n < 126:
        head = struct.pack('!BB', 0x80 | opcode, n)
    elif n < 1 << 16:
        head = struct.pack('!BBH', 0x80 | opcode, 126, n)
    else:
        head = struct.pack('!BBQ', 0x80 | opcode, 127, n)
    return head + payload


def parse_frames(buf):
    """Split complete client frames off buf. Returns ([(fin, opcode, payload)], rest)."""
    out = []
    while len(buf) >= 2:
        b0, b1 = buf[0], buf[1]
        if not b1 & 0x80:
            raise ValueError('client frames must be masked')
        n, pos = b1 & 0x7F, 2
        if n == 126:
            if len(buf) < 4:
                break
            n, pos = struct.unpack('!H', buf[2:4])[0], 4
        elif n == 127:
            if len(buf) < 10:
                break
            n, pos = struct.unpack('!Q', buf[2:10])[0], 10
        if n > MAX_MESSAGE:
            raise ValueError('frame too large')
        if len(buf) < pos + 4 + n:
            break
        mask = buf[pos:pos + 4]
        data = bytes(c ^ mask[i % 4] for i, c in enumerate(buf[pos + 4:pos + 4 + n]))
        out.append((bool(b0 & 0x80), b0 & 0x0F, data))
        buf = buf[pos + 4 + n:]
    return out, buf


def http_reply(sock, status, text):
    body = (text + '\n').encode()
    try:
        sock.sendall(f'HTTP/1.1 {status}\r\nContent-Type: text/plain; charset=utf-8\r\n'
                     f'Content-Length: {len(body)}\r\nConnection: close\r\n\r\n'.encode() + body)
    except OSError:
        pass
    sock.close()


class Sender:
    """A file on its way to the browser (the hub page, a PDF, a video): sent a piece at a time from the select() loop,
    so a big download never blocks the terminals."""

    def __init__(self, sock, head, path=None, start=0, length=0):
        self.sock, self.out, self.seen = sock, head, time.monotonic()
        self.left = length if path else 0                 # HEAD: only the headers
        self.f = None
        if path and length:
            self.f = open(path, 'rb')
            self.f.seek(start)
        sock.setblocking(False)

    def pump(self):
        """Send what the socket takes now. False when everything went out."""
        if not self.out and self.left:
            self.out = self.f.read(min(1 << 16, self.left))
            self.left = self.left - len(self.out) if self.out else 0
        if self.out:
            n = self.sock.send(self.out)
            self.out = self.out[n:]
            self.seen = time.monotonic()
        return bool(self.out or self.left)

    def close(self):
        if self.f:
            self.f.close()
        try:
            self.sock.close()
        except OSError:
            pass


def web(sock, method, path, headers, port, senders):
    """Answer a request for a page or file (GET or HEAD); registers a Sender, or replies and closes."""
    if not allowed_host(headers.get('host'), port):
        return http_reply(sock, '403 Forbidden', 'host not allowed / nombre no permitido')
    f = web_file(path)
    if f is None:
        return http_reply(sock, '404 Not Found', 'FSO terminal bridge')
    if len(senders) >= MAX_SENDERS:
        return http_reply(sock, '503 Service Unavailable', 'busy / ocupado')
    size = f.stat().st_size
    start, end, status = 0, size - 1, '200 OK'
    rng = headers.get('range')
    if rng:
        m = re.fullmatch(r'bytes=(\d*)-(\d*)', rng.strip())
        if m and (m.group(1) or m.group(2)):
            if m.group(1):
                start = int(m.group(1))
                end = min(int(m.group(2)), size - 1) if m.group(2) else size - 1
            else:                                               # bytes=-N: the last N bytes
                start = max(size - int(m.group(2)), 0)
            if start > end or start >= size:
                try:
                    sock.sendall(f'HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{size}\r\n'
                                 'Content-Length: 0\r\nConnection: close\r\n\r\n'.encode())
                except OSError:
                    pass
                sock.close()
                return None
            status = '206 Partial Content'
    length = end - start + 1 if size else 0
    head = (f'HTTP/1.1 {status}\r\nContent-Type: {TYPES[f.suffix.lower()]}\r\nContent-Length: {length}\r\n'
            'Accept-Ranges: bytes\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\n'
            'Referrer-Policy: no-referrer\r\nConnection: close\r\n'
            + (f'Content-Range: bytes {start}-{end}/{size}\r\n' if status.startswith('206') else '') + '\r\n').encode()
    s = Sender(sock, head, f if method == 'GET' else None, start, length)
    senders[sock] = s
    return None


class Session:
    """One WebSocket connection = one shell in its own pseudo-terminal and session."""

    def __init__(self, sock, shell):
        self.sock, self.buf, self.parts, self.opcode, self.inq = sock, b'', [], 0, b''
        env = dict(os.environ, TERM='xterm-256color', COLORTERM='truecolor', FSO_TDS=str(TDS), LAB=str(LAB))
        pid, fd = os.forkpty()
        if pid == 0:                                   # child: becomes the shell
            try:
                os.chdir(TDS)
                argv = [shell]
                if os.path.basename(shell) == 'bash':   # short prompt, after the student's own ~/.bashrc
                    argv += ['--rcfile', str(TDS / 'terminal.bashrc')]
                os.execvpe(shell, argv, env)
            finally:
                os._exit(127)
        self.pid, self.fd = pid, fd
        os.set_blocking(fd, False)
        try:
            self.send_json({'t': 'hello', 'v': 1, 'tds': str(TDS), 'ready': ready()})
        except OSError:                                # the browser already left: do not leave the shell behind
            os.killpg(pid, signal.SIGHUP)
            os.close(fd)
            raise

    def send(self, opcode, payload):
        self.sock.sendall(frame(opcode, payload))

    def send_json(self, obj):
        self.send(0x1, json.dumps(obj).encode())

    def pump_output(self):
        """Terminal -> browser. Returns False when the shell has ended."""
        try:
            data = os.read(self.fd, 65536)
        except BlockingIOError:
            return True
        except OSError as e:                           # EIO: the shell closed the terminal
            if e.errno != errno.EIO:
                raise
            data = b''
        if not data:
            return False
        self.send(0x2, data)
        return True

    def pump_input(self):
        """Browser -> terminal. Returns False when the browser closed the connection."""
        chunk = self.sock.recv(65536)
        if not chunk:
            return False
        frames, self.buf = parse_frames(self.buf + chunk)
        for fin, op, data in frames:
            if op == 0x8:                              # close
                return False
            if op == 0x9:                              # ping -> pong
                self.send(0xA, data)
                continue
            if op == 0xA:
                continue
            if op in (0x1, 0x2):
                self.opcode, self.parts = op, [data]
            elif op == 0x0:                            # continuation
                self.parts.append(data)
            if sum(map(len, self.parts)) > MAX_MESSAGE:
                raise ValueError('message too large')
            if fin and self.opcode == 0x1:
                self.handle(json.loads(b''.join(self.parts)))
                self.parts = []
        return True

    def handle(self, msg):
        kind = msg.get('t')
        if kind == 'in' and isinstance(msg.get('d'), str):
            if len(self.inq) > MAX_MESSAGE:
                raise ValueError('input queue full')
            self.inq += msg['d'].encode()             # written by flush_input when the terminal accepts it
        elif kind == 'size':
            cols = max(2, min(500, int(msg.get('c', 80))))
            rows = max(2, min(200, int(msg.get('r', 24))))
            fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        elif kind == 'ready':
            self.send_json({'t': 'ready', 'ready': ready()})

    def flush_input(self):
        """Queued keystrokes -> terminal, without blocking (the shell's echo must keep flowing back)."""
        try:
            self.inq = self.inq[os.write(self.fd, self.inq):]
        except BlockingIOError:
            pass

    def close(self, code=None):
        if code is not None:
            try:
                self.send_json({'t': 'exit', 'code': code})
            except OSError:
                pass
        try:
            self.send(0x8, struct.pack('!H', 1000))
        except OSError:
            pass
        self.sock.close()
        try:
            os.killpg(self.pid, signal.SIGHUP)         # the shell leads its own session: end all its jobs
        except ProcessLookupError:
            pass
        os.close(self.fd)


def handshake(sock, request, key, shell, sessions, port=None, senders=None):
    """Validate the HTTP upgrade request. Returns a Session or None (connection answered and closed, or a page or
    file on its way: see web())."""
    head = request.split(b'\r\n\r\n', 1)[0].decode('latin-1').split('\r\n')
    parts = head[0].split()
    headers = {}
    for line in head[1:]:
        if ':' in line:
            name, value = line.split(':', 1)
            headers[name.strip().lower()] = value.strip()
    if len(parts) != 3 or parts[0] not in ('GET', 'HEAD'):
        return http_reply(sock, '400 Bad Request', 'FSO terminal bridge')
    url = urlsplit(parts[1])
    if url.path != '/term':
        if headers.get('upgrade') or senders is None:
            return http_reply(sock, '404 Not Found', 'FSO terminal bridge')
        return web(sock, parts[0], url.path, headers, port, senders)
    if parts[0] != 'GET':
        return http_reply(sock, '400 Bad Request', 'FSO terminal bridge')
    given = parse_qs(url.query).get('k', [''])[0]
    if not hmac.compare_digest(given.encode(), key.encode()):
        return http_reply(sock, '403 Forbidden', 'wrong key / clave incorrecta')
    if not allowed_origin(headers.get('origin'), port):
        return http_reply(sock, '403 Forbidden', 'origin not allowed / origen no permitido')
    ws_key = headers.get('sec-websocket-key', '')
    if headers.get('upgrade', '').lower() != 'websocket' or not ws_key:
        return http_reply(sock, '426 Upgrade Required', 'WebSocket only')
    if len(sessions) >= MAX_SESSIONS:
        return http_reply(sock, '503 Service Unavailable', f'at most {MAX_SESSIONS} terminals / como máximo {MAX_SESSIONS} terminales')
    accept = base64.b64encode(hashlib.sha1((ws_key + GUID).encode()).digest()).decode()
    sock.sendall(('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                  f'Sec-WebSocket-Accept: {accept}\r\n\r\n').encode())
    return Session(sock, shell)


def banner(port, key):
    k = f'{port}.{key}'
    cs = codespace_host(port)
    print(f'Terminal del aula FSO · puente local  /  FSO hub terminal · local bridge\n'
          f'  Clave / key:  {k}')
    if cs:
        print(f'  GitHub Codespaces · abre el aula con su terminal / open the hub with its terminal:\n'
              f'    https://{cs}/?terminal=aqui.{key}#/u1/laboratorio\n'
              f'  (pestaña Puertos: el {port} debe quedar privado / Ports tab: keep port {port} private)')
    else:
        if HUB.exists():
            print(f'  Abre el aula / open the hub:\n    {HUB.as_uri()}?terminal={k}#/u1/laboratorio')
        print(f'  O en el navegador / or in the browser:\n    http://127.0.0.1:{port}/?terminal=aqui.{key}#/u1/laboratorio')
    print('  O pega la clave en el panel «Terminal» de un laboratorio / or paste the key in a lab\'s "Terminal" panel.\n'
          '  Solo conexiones de este equipo (127.0.0.1) / this machine only. Ctrl+C: cerrar / quit.', flush=True)


def serve(port, key, shell, quiet):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        listener.bind(('127.0.0.1', port))
    except OSError as e:
        sys.exit(f'No se pudo escuchar en 127.0.0.1:{port} ({e.strerror}). ¿Ya hay un puente abierto? Prueba --port.\n'
                 f'Could not listen on 127.0.0.1:{port}. Is a bridge already running? Try --port.')
    listener.listen(8)
    if not quiet:
        banner(port, key)
    pending = {}        # socket -> (buffer, deadline): waiting for the HTTP upgrade request
    sessions = {}       # socket -> Session
    senders = {}        # socket -> Sender: the hub page, PDFs and videos on their way to the browser
    by_fd = {}          # pty fd -> Session
    codes = {}          # pid -> exit code of shells reaped before their session noticed

    def drop(s, code=None):
        sessions.pop(s.sock, None)
        by_fd.pop(s.fd, None)
        s.close(code)

    try:
        while True:
            watch = [listener, *pending, *sessions, *by_fd]
            writable = [s.fd for s in sessions.values() if s.inq] + list(senders)
            readable, can_write, _ = select.select(watch, writable, [], 1.0)
            for fd in can_write:
                if fd in senders:
                    try:
                        more = senders[fd].pump()
                    except BlockingIOError:
                        more = True
                    except Exception:                   # browser gone, file changed…: never take the shells down
                        more = False
                    if not more:
                        senders.pop(fd).close()
                elif fd in by_fd:
                    try:
                        by_fd[fd].flush_input()
                    except OSError:
                        pass
            for r in readable:
                if r is listener:
                    sock, _ = listener.accept()
                    sock.settimeout(10)
                    pending[sock] = (b'', time.monotonic() + HANDSHAKE_TIMEOUT)
                elif r in pending:
                    data, deadline = pending.pop(r)
                    try:
                        chunk = r.recv(8192)
                    except OSError:
                        chunk = b''
                    if not chunk:
                        r.close()
                        continue
                    data += chunk
                    if b'\r\n\r\n' not in data:
                        if len(data) > 16384:
                            http_reply(r, '431 Request Header Fields Too Large', 'FSO terminal bridge')
                        else:
                            pending[r] = (data, deadline)
                        continue
                    try:
                        s = handshake(r, data, key, shell, sessions, port, senders)
                    except OSError:                    # the browser left during the handshake
                        r.close()
                        continue
                    if s:
                        sessions[r] = s
                        by_fd[s.fd] = s
                elif r in sessions:
                    s = sessions[r]
                    try:
                        alive = s.pump_input()
                    except (OSError, ValueError):
                        alive = False
                    if not alive:
                        drop(s)
                elif r in by_fd:
                    s = by_fd[r]
                    try:
                        alive = s.pump_output()
                    except OSError:
                        drop(s)
                        continue
                    if not alive:
                        try:
                            code = os.waitstatus_to_exitcode(os.waitpid(s.pid, 0)[1])
                        except ChildProcessError:
                            code = codes.pop(s.pid, 0)
                        drop(s, code)
            now = time.monotonic()
            for sock, (_, deadline) in list(pending.items()):
                if now > deadline:
                    pending.pop(sock)
                    sock.close()
            for sock, s in list(senders.items()):
                if now - s.seen > SEND_IDLE:
                    senders.pop(sock).close()
            while True:                                 # reap shells ended by SIGHUP
                try:
                    pid, status = os.waitpid(-1, os.WNOHANG)
                except ChildProcessError:
                    break
                if pid == 0:
                    break
                codes[pid] = os.waitstatus_to_exitcode(status)
    except KeyboardInterrupt:
        if not quiet:
            print('\nPuente cerrado / bridge closed.')
    finally:
        for s in list(sessions.values()):
            drop(s)
        for s in senders.values():
            s.close()
        listener.close()


def main():
    ap = argparse.ArgumentParser(description='Terminal local para los laboratorios del aula FSO / local terminal for the FSO hub labs.')
    ap.add_argument('--port', type=int, default=8765, help='puerto en 127.0.0.1 (por omisión 8765)')
    ap.add_argument('--key', help='clave fija de 32 cifras hexadecimales (solo para pruebas; por omisión, una nueva al azar)')
    ap.add_argument('--shell', default=os.environ.get('SHELL') or '/bin/bash', help='shell de cada terminal (por omisión $SHELL)')
    ap.add_argument('--quiet', action='store_true', help='no mostrar el aviso inicial')
    a = ap.parse_args()
    if a.key is not None and not re.fullmatch(r'[0-9a-f]{32}', a.key):
        sys.exit('--key: 32 cifras hexadecimales / 32 hex digits')
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt))
    serve(a.port, a.key or secrets.token_hex(16), a.shell, a.quiet)


if __name__ == '__main__':
    main()

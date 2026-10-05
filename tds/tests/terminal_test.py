#!/usr/bin/env python3
"""Pruebas del puente de la terminal del aula (tds/terminal.py). Las llama tests/run.sh.

    python3 tests/terminal_test.py              seguridad, protocolo, shell real y repetición de los comandos
    python3 tests/terminal_test.py --no-replay  sin repetir los comandos de los botones "Ejecutar"

Imprime una línea "✔ …" o "✘ …" por comprobación; código de salida 1 si alguna falla.
La repetición toma la misma regla y el mismo guion que el aula (hub/build.py: runnable y run_script): compila con
gcc cada programa del comando (ejecutable en tests/tmp/tdN) y lo ejecuta ahí, con la entrada abierta (un comando que
esperara teclado se quedaría colgado y fallaría), y compara su estado de salida con el de la salida grabada.
"""
import base64
import hashlib
import importlib.util
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

TDS = Path(__file__).resolve().parents[1]
LAB = TDS / 'tests' / 'tmp'
OUT = TDS / 'tests' / 'out'
CONTENT = TDS.parent / 'hub' / 'src' / 'content'
KEY = '0123456789abcdef0123456789abcdef'
GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
FAIL = 0


def check(desc, ok, detail=''):
    global FAIL
    print(f'  {"✔" if ok else "✘"} {desc}' + (f' · {detail}' if detail and not ok else ''), flush=True)
    FAIL += not ok


class WS:
    """Cliente WebSocket mínimo (lo justo para probar el puente)."""

    def __init__(self, port, key=KEY, origin=None, path=None, upgrade=True):
        self.s = socket.create_connection(('127.0.0.1', port), timeout=10)
        wkey = base64.b64encode(os.urandom(16)).decode()
        req = f'GET {path or "/term?k=" + key} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n'
        if upgrade:
            req += f'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {wkey}\r\nSec-WebSocket-Version: 13\r\n'
        if origin:
            req += f'Origin: {origin}\r\n'
        self.s.sendall((req + '\r\n').encode())
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = self.s.recv(4096)
            if not chunk:
                break
            data += chunk
        head, _, self.buf = data.partition(b'\r\n\r\n')
        self.status = int(head.split()[1]) if head else 0
        want = base64.b64encode(hashlib.sha1((wkey + GUID).encode()).digest())
        self.accepted = self.status == 101 and want in head
        self.text = ''

    def send(self, obj):
        payload, mask = json.dumps(obj).encode(), os.urandom(4)
        n = len(payload)
        head = struct.pack('!BB', 0x81, 0x80 | n) if n < 126 else struct.pack('!BBH', 0x81, 0x80 | 126, n)
        self.s.sendall(head + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))

    def frame(self, timeout):
        self.s.settimeout(timeout)
        while True:
            if len(self.buf) >= 2:
                n, pos = self.buf[1] & 0x7F, 2
                if n == 126 and len(self.buf) >= 4:
                    n, pos = struct.unpack('!H', self.buf[2:4])[0], 4
                elif n == 127 and len(self.buf) >= 10:
                    n, pos = struct.unpack('!Q', self.buf[2:10])[0], 10
                if n < 126 or pos > 2:
                    if len(self.buf) >= pos + n:
                        op, data, self.buf = self.buf[0] & 0x0F, self.buf[pos:pos + n], self.buf[pos + n:]
                        return op, data
            chunk = self.s.recv(65536)
            if not chunk:
                return 0x8, b''
            self.buf += chunk

    def json(self, kind, timeout=5):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            op, data = self.frame(end - time.monotonic())
            if op == 0x1:
                msg = json.loads(data)
                if msg.get('t') == kind:
                    return msg
            elif op == 0x2:
                self.text += data.decode('utf-8', 'replace')
            elif op == 0x8:
                return None
        return None

    def until(self, pattern, timeout=10):
        """Lee la salida de la terminal hasta que aparezca pattern (regex). Devuelve el match o None."""
        end = time.monotonic() + timeout
        while True:
            m = re.search(pattern, self.text)
            if m:
                self.text = self.text[m.end():]
                return m
            left = end - time.monotonic()
            if left <= 0:
                return None
            try:
                op, data = self.frame(left)
            except (socket.timeout, TimeoutError):
                return None
            if op == 0x2:
                self.text += data.decode('utf-8', 'replace')
            elif op == 0x8:
                return None

    def run(self, line):
        self.send({'t': 'in', 'd': line + '\r'})

    def close(self):
        try:
            self.s.sendall(struct.pack('!BB', 0x88, 0x80) + os.urandom(4))
        except OSError:
            pass
        self.s.close()


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def start(port, env=None):
    p = subprocess.Popen([sys.executable, str(TDS / 'terminal.py'), '--port', str(port), '--key', KEY, '--quiet', '--shell', '/bin/bash'],
                         env=dict(os.environ, **(env or {})))
    for _ in range(100):
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.2).close()
            return p
        except OSError:
            time.sleep(0.05)
    p.kill()
    raise SystemExit('el puente no arrancó')


def gone(pid, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        time.sleep(0.05)
    return False


def shell_pid(ws):
    ws.run('echo "PID:$$:"')
    m = ws.until(r'PID:(\d+):')
    return int(m.group(1)) if m else None


def listening_addresses(port):
    """Direcciones en las que escucha el puerto (IPv4, /proc/net/tcp; estado 0A = LISTEN)."""
    found = []
    for line in Path('/proc/net/tcp').read_text().splitlines()[1:]:
        local, state = line.split()[1], line.split()[3]
        ip, p = local.split(':')
        if int(p, 16) == port and state == '0A':
            found.append('.'.join(str(b) for b in reversed(bytes.fromhex(ip))))
    return found


def get(port, path, host=None, extra='', method='GET'):
    """Petición HTTP cruda al puente: (estado, cabeceras, cuerpo)."""
    with socket.create_connection(('127.0.0.1', port), timeout=10) as s:
        s.sendall(f'{method} {path} HTTP/1.1\r\nHost: {host or f"127.0.0.1:{port}"}\r\n{extra}Connection: close\r\n\r\n'.encode())
        data = b''
        while True:
            c = s.recv(1 << 16)
            if not c:
                break
            data += c
    head, _, body = data.partition(b'\r\n\r\n')
    lines = head.decode('latin-1').split('\r\n')
    hdrs = {k.strip().lower(): v.strip() for k, v in (x.split(':', 1) for x in lines[1:] if ':' in x)}
    return int(lines[0].split()[1]), hdrs, body


def web_tests():
    """El puente sirve el aula (y sus PDF y videos) a este equipo, y en un Codespace solo a la dirección de ese Codespace."""
    hub = next((p for p in (TDS.parent / 'hub' / 'dist' / 'index.html', TDS.parent / 'index.html') if p.exists()), None)
    pdf = next(iter(sorted((TDS.parent / 'materiales').glob('unidad-1/*.pdf'))), None)
    port = free_port()
    bridge = start(port)
    try:
        if hub:
            st, h, body = get(port, '/')
            check('sirve el aula en http://127.0.0.1:PUERTO/ (200, HTML)', st == 200 and h.get('content-type', '').startswith('text/html')
                  and b'HUB_CONFIG' in body and len(body) == hub.stat().st_size, f'{st} {h.get("content-type")}')
            st, h, body = get(port, '/', method='HEAD')
            check('HEAD / → solo cabeceras', st == 200 and body == b'' and int(h.get('content-length', -1)) == hub.stat().st_size)
        if pdf:
            rel = pdf.relative_to(TDS.parent).as_posix()
            st, h, body = get(port, '/' + rel)
            check('sirve los PDF de materiales/', st == 200 and h.get('content-type') == 'application/pdf' and body == pdf.read_bytes(), str(st))
            st, h, body = get(port, '/' + rel, extra='Range: bytes=100-199\r\n')
            check('pedido parcial (Range) → 206 con esos 100 bytes', st == 206 and body == pdf.read_bytes()[100:200]
                  and h.get('content-range') == f'bytes 100-199/{pdf.stat().st_size}', f'{st} {h.get("content-range")}')
        check('nada fuera del aula, materiales/ y videos/ (tds/terminal.py → 404)', get(port, '/tds/terminal.py')[0] == 404)
        check('no se sale de su carpeta (/materiales/../ESTADO.md y %2e%2e → 404)',
              get(port, '/materiales/../ESTADO.md')[0] == 404 and get(port, '/materiales/%2e%2e/ESTADO.md')[0] == 404)
        check('un sitio que apunta su nombre a 127.0.0.1 (DNS rebinding: Host ajeno) → 403', get(port, '/', host=f'ejemplo.com:{port}')[0] == 403)
        check('fuera de un Codespace no acepta direcciones de Codespaces', WS(port, origin=f'https://prueba-{port}.app.github.dev').status == 403)
    finally:
        bridge.kill()
    port = free_port()
    bridge = start(port, {'CODESPACE_NAME': 'prueba', 'GITHUB_CODESPACES_PORT_FORWARDING_DOMAIN': 'app.github.dev'})
    try:
        own = f'prueba-{port}.app.github.dev'
        ws = WS(port, origin=f'https://{own}')
        check('en un Codespace acepta su propia dirección privada (https://NOMBRE-PUERTO.app.github.dev)', ws.accepted)
        if ws.accepted:
            ws.close()
        check('…pero no la de otro Codespace ni otro puerto', WS(port, origin=f'https://otro-{port}.app.github.dev').status == 403
              and WS(port, origin=f'https://prueba-{port + 1}.app.github.dev').status == 403)
        check('…ni otra página web', WS(port, origin='https://andresrubiop.github.io').status == 403)
        if hub:
            check('sirve el aula pedida por la dirección del Codespace', get(port, '/', host=own)[0] == 200)
    finally:
        bridge.kill()


def protocol_tests():
    port = free_port()
    bridge = start(port)
    try:
        addrs = listening_addresses(port)
        check('solo escucha en 127.0.0.1 (nunca en la red)', addrs == ['127.0.0.1'], str(addrs))
        check('clave incorrecta → 403', WS(port, key='f' * 32).status == 403)
        check('sin clave → 403', WS(port, path='/term').status == 403)
        check('otra página web (Origin https://ejemplo.com) → 403', WS(port, origin='https://ejemplo.com').status == 403)
        check('otra ruta → 404', WS(port, path=f'/shell?k={KEY}').status == 404)
        check('sin pedir WebSocket → 426', WS(port, upgrade=False).status == 426)

        check('el aula abierta desde el disco en Chrome (Origin: file://) → 101', WS(port, origin='file://').accepted)
        ws = WS(port, origin='null')                  # así llega el aula abierta desde el disco en Firefox
        hello = ws.json('hello') if ws.accepted else None
        check('el aula (Origin: null) con la clave → 101 y saludo', bool(hello and hello.get('tds') == str(TDS)), str(hello))
        ws.run('printf "%s%s:%s:%s:%s:\\n" R X "$PWD" "$LAB" "$FSO_TDS"')   # "RX:" no aparece en el eco de la orden
        m = ws.until(r'RX:([^:\r\n]*):([^:\r\n]*):([^:\r\n]*):')
        check('la shell arranca en tds/ con $LAB y $FSO_TDS', bool(m and m.groups() == (str(TDS), str(LAB), str(TDS))), str(m and m.groups()))
        ws.run('tty')
        check('es una terminal real (tty → /dev/pts/N)', bool(ws.until(r'/dev/pts/\d+')))
        ws.send({'t': 'size', 'c': 123, 'r': 31})
        ws.run('stty size')
        check('toma el tamaño de la ventana del aula (stty size → 31 123)', bool(ws.until(r'\b31 123\b')))
        ws.send({'t': 'ready'})
        rd = ws.json('ready')
        check('informa qué TD están preparados (tests/tmp/tdN)', bool(rd) and rd['ready'] == sorted(p.name for p in LAB.glob('td[0-8]') if p.is_dir()), str(rd))
        if (LAB / 'td1').is_dir():
            ws.run('cd "$LAB/td1" && ./echo hola mundo')
            check('ejecuta un comando de un TD como el botón «Ejecutar»', bool(ws.until(r'[\r\n]hola mundo\r')))
        ws.run('printf "%s\\n" "$(printf "x%.0s" $(seq 3000))" | wc -c')
        check('entrada larga (3 KB de una vez) sin trabarse', bool(ws.until(r'\b3001\b', timeout=10)))

        others = [WS(port) for _ in range(2)]
        check('hasta 3 terminales a la vez', all(o.accepted for o in others))
        check('la 4.ª terminal → 503', WS(port).status == 503)
        pids = [shell_pid(o) for o in others]
        for o in others:
            o.close()
        check('al cerrar la página, su shell termina', all(p and gone(p) for p in pids), str(pids))
        ws.run('exit 7')
        ex = ws.json('exit')
        check('si la shell termina, el aula recibe su código (exit 7)', bool(ex) and ex.get('code') == 7, str(ex))

        ws2 = WS(port)
        pid = shell_pid(ws2)
        bridge.send_signal(signal.SIGTERM)
        try:
            bridge.wait(5)
            ended = True
        except subprocess.TimeoutExpired:
            ended = False
        check('al cerrar el puente (SIGTERM, como Ctrl+C) termina con sus shells', ended and bool(pid) and gone(pid))
    finally:
        if bridge.poll() is None:
            bridge.kill()


def load_build():
    spec = importlib.util.spec_from_file_location('hub_build', TDS.parent / 'hub' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def replay():
    build = load_build()
    runnable = build.runnable
    names = sorted({n for f in CONTENT.glob('*/60-laboratorio.html') for n in re.findall(r'@@out:([\w./-]+?)(?:#[\d,-]+)?@@', f.read_text())})
    done, skipped, bad, compiles = 0, set(), [], 0
    env = dict(os.environ, LAB=str(LAB), FSO_TDS=str(TDS))
    for name in names:
        f = OUT / f'{name}.txt'
        if not f.exists():
            continue
        lines = f.read_text().rstrip('\n').split('\n')
        cmd = runnable(name, lines[0])
        if not cmd:
            continue
        td = name.split('/')[0]
        if not (LAB / td).is_dir():
            skipped.add(td)
            continue
        m = re.fullmatch(r'\[exit status (\d+)\]', lines[-1])
        want = int(m.group(1)) if m else 0
        script = ' && '.join(build.run_script(name, cmd))         # lo mismo que escribe «▶ Ejecutar»
        compiles += script.count(' && gcc ') + script.startswith('gcc ')
        p = subprocess.Popen(['bash', '-c', script], cwd=TDS, env=env, stdin=subprocess.PIPE,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            rc = p.wait(60)
            rc = 128 - rc if rc < 0 else rc          # muerto por la señal N: bash lo muestra como 128 + N
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            rc = 'se quedó esperando (¿lee del teclado?)'
        p.stdin.close()
        done += 1
        if rc != want:
            bad.append(f'{name}: {rc} en lugar de {want}')
    check(f'los {done} guiones de «▶ Ejecutar» ({compiles} compilaciones con gcc) terminan como su salida grabada', not bad, '; '.join(bad))
    # «⚙ Compilar con gcc»: cada archivo C con programas compila con las líneas del aula
    fails = []
    srcs = sorted({f.relative_to(TDS).as_posix() for f in TDS.glob('td[0-8]/*.c')})
    n = 0
    for rel in srcs:
        cs = build.compile_script(rel)
        if not cs:
            continue
        n += sum(1 for l in cs if l.startswith('gcc '))
        r = subprocess.run(['bash', '-c', ' && '.join(cs)], cwd=TDS, env=env, capture_output=True, text=True)
        if r.returncode:
            fails.append(f'{rel}: {r.stderr.strip()[:200]}')
    check(f'«⚙ Compilar con gcc»: los {n} programas compilan con -Wall -Wextra -Werror', n > 0 and not fails, '; '.join(fails))
    if skipped:
        print(f'  (sin preparar, no se repitieron: {", ".join(sorted(skipped))} → tests/run.sh)')


if __name__ == '__main__':
    protocol_tests()
    web_tests()
    if '--no-replay' in sys.argv:
        pass
    elif (TDS.parent / 'hub' / 'build.py').exists():
        replay()
    else:                                   # copia de estudiantes (GitHub): sin el código del aula no hay guiones que repetir
        print('  · sin hub/build.py (copia de estudiantes): no se repiten los guiones de «▶ Ejecutar»')
    sys.exit(1 if FAIL else 0)

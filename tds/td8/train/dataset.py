"""Synthetic data for the TD8 micro-LLM: requests in Spanish or English → tool call.

Every example is a text of UTF-8 bytes:

    Q: lista los archivos de docs\n
    A: list_files("docs")\n

The model only learns to translate the intent into ONE call from a closed catalogue of tools
(the equivalent of the MCP "connectors" of an agentic OS). File and folder names are generated
at random so that the model learns to COPY them from the request (attention), not to memorise them.
None of this is safe by itself: the OS decides whether to run the call (policy + Landlock in agentd).
"""
import random

TOOLS = ['list_files', 'read_file', 'file_info', 'count_lines', 'search', 'disk_usage',
         'processes', 'system_info', 'make_dir', 'write_note', 'delete_file', 'none']

_STEMS = ['notas', 'plan', 'informe', 'datos', 'tareas', 'lista', 'main', 'readme', 'log', 'config',
          'foto', 'backup', 'reporte', 'examen', 'agenda', 'todo', 'notes', 'report', 'data', 'draft',
          'server', 'client', 'budget', 'syllabus', 'lab', 'td8', 'utils', 'chat', 'old', 'temp',
          'viejo', 'nuevo', 'atajo', 'enlace', 'borrador', 'apuntes', 'proyecto', 'factura', 'horario', 'resumen',
          'copia', 'respaldo', 'clave', 'secreto', 'video', 'musica', 'jueves', 'agentd', 'kernel', 'shadow',
          'hosts', 'passwd', 'queue', 'zeta', 'wifi', 'yoga', 'ejemplo', 'gjk', 'quiz', 'fixme']
_EXTS = ['.txt', '.md', '.c', '.h', '.csv', '.log', '.json', '.py', '.png', '.pdf', '.conf', '.sh']
_DIRS = ['docs', 'notas', 'fotos', 'proyectos', 'src', 'tmp', 'reports', 'backup', 'music', 'labs',
         'escritorio', 'descargas', 'downloads', 'data', 'archive', 'clase', 'work', 'tests']
_SYL = ['ka', 'lo', 'mi', 're', 'ta', 'no', 'si', 'pu', 'de', 'va', 'xo', 'fe', 'ri', 'bu', 'ze', 'an',
        'ol', 'in', 'ex', 'um', 'qu', 'ch', 'ly', 'or']
_WORDS = ['comprar leche', 'reunion a las 5', 'llamar a ana', 'revisar el lab', 'meeting at 5',
          'buy bread', 'call mom', 'fix the bug', 'estudiar fork', 'entregar el td', 'pagar la luz',
          'backup del disco', 'review pull request', 'water the plants', 'leer el capitulo 3']
_TERMS = ['error', 'TODO', 'main', 'warning', 'fork', 'pid', 'socket', 'password', 'ERROR', 'mutex',
          'hola', 'root', 'failed', 'include', 'sleep']
_DANGEROUS = ['/etc/passwd', '/etc/shadow', '../secreto.txt', '../../etc/hosts', '/home/ana/.ssh/id_rsa',
              '/boot/vmlinuz', '/', '~/.bashrc', '/var/log/syslog', '../../../root']


_ALPHA = 'abcdefghijklmnopqrstuvwxyz'


def _name(r: random.Random) -> str:
    # half syllables, half random letters from the WHOLE alphabet (and the odd digit): the model must learn
    # to copy any character, not only those of a handful of syllables
    if r.random() < 0.5:
        return ''.join(r.choice(_SYL) for _ in range(r.randint(2, 4)))
    s = ''.join(r.choice(_ALPHA) for _ in range(r.randint(3, 10)))
    if r.random() < 0.2:
        s += str(r.randint(0, 99))
    return s


def _file(r: random.Random) -> str:
    x = r.random()
    if x < 0.08:
        return r.choice(_DANGEROUS)
    stem = r.choice(_STEMS) if x < 0.55 else _name(r)
    if r.random() < 0.25:
        stem += r.choice(['_', '-', '']) + str(r.randint(1, 2026))
    f = stem + ('' if r.random() < 0.1 else r.choice(_EXTS))     # sometimes without an extension (Makefile, atajo)
    if r.random() < 0.18:
        f = r.choice(_DIRS) + '/' + f
    return f


def _dir(r: random.Random) -> str:
    x = r.random()
    if x < 0.06:
        return r.choice(['/etc', '/', '..', '../..', '/home', '/root', '/tmp'])
    d = r.choice(_DIRS) if x < 0.6 else _name(r)
    if r.random() < 0.15:
        d = r.choice(_DIRS) + '/' + d
    return d


def _text(r: random.Random) -> str:
    if r.random() < 0.6:
        return r.choice(_WORDS)
    return ' '.join(_name(r) for _ in range(r.randint(1, 3)))


def _term(r: random.Random) -> str:
    return r.choice(_TERMS) if r.random() < 0.6 else _name(r)


# (Spanish templates, English templates, function that produces the call)
T = {
    'list_files': (
        ['lista los archivos de {d}', 'que hay en {d}?', '¿qué hay en {d}?', 'muestra la carpeta {d}',
         'enséñame los archivos en {d}', 'dime qué archivos tiene {d}', 'ls {d}', 'contenido de la carpeta {d}',
         'quiero ver los archivos de {d}', 'lista el directorio {d}'],
        ['list the files in {d}', 'what is in {d}?', 'show me the folder {d}', 'which files are in {d}',
         'ls {d}', 'list the directory {d}', 'show the contents of {d}', 'what files does {d} have?']),
    'list_here': (
        ['lista mis archivos', '¿qué archivos tengo?', 'muestra mis archivos', 'ls', '¿qué hay aquí?'],
        ['list my files', 'what files do I have?', 'show my files', 'ls', 'what is here?']),
    'read_file': (
        ['lee {f}', 'muéstrame {f}', 'abre el archivo {f}', '¿qué dice {f}?', 'imprime el contenido de {f}',
         'cat {f}', 'léeme el archivo {f}', 'quiero leer {f}'],
        ['read {f}', 'show me {f}', 'open the file {f}', 'what does {f} say?', 'print {f}', 'cat {f}',
         'display the file {f}', 'read the file {f}']),
    'file_info': (
        ['¿cuánto pesa {f}?', 'información de {f}', '¿cuándo se modificó {f}?', 'tamaño de {f}',
         'stat {f}', 'detalles del archivo {f}'],
        ['how big is {f}?', 'info about {f}', 'when was {f} modified?', 'size of {f}', 'stat {f}',
         'details of the file {f}']),
    'count_lines': (
        ['¿cuántas líneas tiene {f}?', 'cuenta las líneas de {f}', 'wc -l {f}', 'número de líneas de {f}'],
        ['how many lines does {f} have?', 'count the lines in {f}', 'wc -l {f}', 'number of lines of {f}']),
    'search': (
        ["busca '{t}' en {f}", "encuentra '{t}' dentro de {f}", "¿aparece '{t}' en {f}?",
         "grep '{t}' {f}", "busca la palabra '{t}' en {f}"],
        ["search for '{t}' in {f}", "find '{t}' in {f}", "grep '{t}' {f}", "does {f} contain '{t}'?",
         "look for '{t}' in {f}"]),
    'disk_usage': (
        ['¿cuánto espacio libre queda?', 'espacio en disco', '¿está lleno el disco?', 'df',
         'uso del disco', '¿cuánto disco me queda?'],
        ['how much disk space is left?', 'disk usage', 'is the disk full?', 'df', 'free disk space']),
    'processes': (
        ['¿qué procesos están corriendo?', 'muestra los procesos', '¿qué programa usa más CPU?', 'ps',
         'lista los procesos', '¿qué se está ejecutando?'],
        ['show running processes', 'what is using the CPU?', 'list processes', 'ps', 'what is running?',
         'top processes']),
    'system_info': (
        ['¿qué versión del núcleo tengo?', 'información del sistema', '¿cuánta memoria tiene el equipo?',
         '¿cuánto tiempo lleva encendido?', 'uname', 'datos del sistema operativo'],
        ['which kernel am I running?', 'system info', 'how much memory does this machine have?', 'uptime',
         'uname', 'tell me about this system']),
    'make_dir': (
        ['crea la carpeta {d}', 'haz un directorio llamado {d}', 'mkdir {d}', 'nueva carpeta {d}'],
        ['create a folder named {d}', 'make a directory {d}', 'mkdir {d}', 'new folder {d}']),
    'write_note': (
        ["anota '{t}' en {f}", "escribe '{t}' en {f}", "agrega '{t}' al archivo {f}", "guarda '{t}' en {f}"],
        ["write '{t}' to {f}", "add '{t}' to {f}", "note '{t}' in {f}", "save '{t}' in {f}"]),
    'delete_file': (
        ['borra {f}', 'elimina el archivo {f}', 'rm {f}', 'quiero borrar {f}', 'elimina {f}'],
        ['delete {f}', 'remove the file {f}', 'rm {f}', 'erase {f}', 'please delete {f}']),
    'none': (
        ['cuéntame un chiste', '¿qué hora es en Tokio?', 'hola', 'escribe un poema', '¿quién ganó el partido?',
         '¿cómo estás?', 'hazme un resumen de la historia de Ecuador', 'traduce hola al francés', 'gracias'],
        ['tell me a joke', 'hello', "what's the weather?", 'write a poem', 'who are you?', 'thanks',
         'translate hello to french', 'who won the game?', 'sing a song']),
}


def _call(tool: str, f: str = '', d: str = '', t: str = '') -> str:
    if tool in ('list_files', 'make_dir'):
        return f'{tool}("{d}")'
    if tool == 'list_here':
        return 'list_files(".")'
    if tool in ('read_file', 'file_info', 'count_lines', 'delete_file'):
        return f'{tool}("{f}")'
    if tool == 'search':
        return f'search("{t}", "{f}")'
    if tool == 'write_note':
        return f'write_note("{f}", "{t}")'
    return f'{tool}()'


def example(r: random.Random) -> tuple[str, str]:
    """Returns (request, call)."""
    # the tools that COPY arguments (paths, texts) are the hard ones: they show up more often
    tool = r.choices(list(T), weights=[1.5 if k in ('search', 'write_note', 'file_info', 'count_lines', 'delete_file', 'read_file') else 1.0 for k in T])[0]
    es, en = T[tool]
    tpl = r.choice(es if r.random() < 0.5 else en)
    f, d = _file(r), _dir(r)
    t = _term(r) if tool == 'search' else _text(r)
    req = tpl.format(f=f, d=d, t=t)
    # surface variations: initial capital, politeness, punctuation
    if r.random() < 0.15:
        req = r.choice(['por favor ', 'please ', 'agente, ', 'hey, ']) + req
    if r.random() < 0.3:
        req = req[0].upper() + req[1:]
    if r.random() < 0.15 and not req.endswith('?'):
        req += r.choice(['.', '!', ' por favor', ' please'])
    return req, _call(tool, f=f, d=d, t=t)


def encode(req: str, call: str) -> tuple[bytes, int]:
    """Full text in bytes and the position where the answer starts (what the model must predict)."""
    prompt = f'Q: {req}\nA: '.encode()
    return prompt + (call + '\n').encode(), len(prompt)


if __name__ == '__main__':
    r = random.Random(0)
    for _ in range(12):
        q, a = example(r)
        print(f'{q!r:60} -> {a}')

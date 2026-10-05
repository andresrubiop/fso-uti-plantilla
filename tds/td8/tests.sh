#!/usr/bin/env bash
# Pruebas del TD8 (las llama tests/run.sh con OUT, TMP y BIN definidos).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
B=$BIN/td8
W=$TMP/td8
FAILS=0
mkdir -p "$W" "$OUT/td8"
cd "$W" || exit 2
for f in tinyllm agentd agent agent_exec; do ln -sf "$B/$f" "$f"; done
ln -sfn "$HERE/weights" weights
ln -sfn "$HERE/train" train          # el comando que muestra el aula (train/compare.py) funciona tal cual aquí

rec() {
    local name=$1 shown=$2 real=${3:-$2} rc
    { printf '$ %s\n' "$shown"; bash -c "$real"$'\n''exit $?' 2>&1; rc=$?
      [ $rc -ne 0 ] && printf '[exit status %d]\n' "$rc"; } | anonymize > "$OUT/td8/$name.txt"
}
anonymize() { sed -e "s#$(cd "$HERE/.." && pwd)#~/fso/tds#g" -e "s#$HOME#~#g" -e "s/$(id -un)/student/g" -e "s/\b$(hostname)\b/host/g"; }
chk() { local d=$1; shift; if "$@"; then printf '  \342\234\224 %s\n' "$d"; else printf '  \342\234\230 %s\n' "$d"; FAILS=$((FAILS + 1)); fi; }
has() { grep -q -- "$2" "$OUT/td8/$1.txt"; }

# ---------------------------------------------------------------- el modelo solo
rec tinyllm "./tinyllm -v \"lista los archivos de docs\""
chk "tinyllm traduce la petición a una llamada" has tinyllm 'list_files("docs")'
# métricas reales de la inferencia para la animación A9.3 (CSV clave,valor)
python3 - "$OUT/td8/tinyllm.txt" > "$OUT/td8/inference.csv" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
num = lambda pat: re.search(pat, t).group(1)
rows = {
    'params': num(r'model\s*: (\d+) parameters'), 'layers': num(r'(\d+) layers'), 'd': num(r'd=(\d+)'),
    'weights_bytes': num(r'weights\s*: (\d+) bytes'), 'kv_bytes': num(r'KV cache\s*: (\d+) bytes'),
    'prompt_tokens': num(r'tokens\s*: (\d+) of prompt'), 'gen_tokens': num(r'\+ (\d+) generated'),
    'load_ms': num(r'load ([0-9.]+) ms'), 'infer_ms': num(r'inference ([0-9.]+) ms'), 'tokens_per_s': num(r'\(([0-9]+) tokens/s\)'),
    'faults_load': num(r'page faults: (\d+) during load'), 'faults_infer': num(r'(\d+) during the first inference'),
}
print('key,value')
for k, v in rows.items():
    print(f'{k},{v}')
PY
chk "métricas de inferencia (A9.3)" [ "$(wc -l < "$OUT/td8/inference.csv")" -eq 13 ]
rec equivalence "python3 train/compare.py ./tinyllm 200"
chk "C y NumPy dan respuestas idénticas" has equivalence "200/200"

# ---------------------------------------------------------------- espacio de trabajo
rm -rf workspace && mkdir -p workspace/docs
printf 'Laboratorio TD8\nfork, exec, pipe, mmap, sockets\n' > workspace/notas.txt
printf '#include <stdio.h>\nint main(void)\n{\n    return 0;\n}\n' > workspace/main.c
printf 'ok start\nerror: disk full\nok retry\nerror: timeout\n' > workspace/log.txt
printf 'borrar\n' > workspace/viejo.txt
touch workspace/docs/plan.md workspace/docs/syllabus.pdf
ln -sf /etc/passwd workspace/atajo                  # un enlace simbólico que "escapa"
rm -f audit.log

./agentd -w workspace -a audit.log > agentd.out 2>&1 &
D=$!
for _ in $(seq 50); do [ -S agentd.sock ] && break; sleep 0.05; done

q() { local o=${3:-}; rec "$1" "./agent $o\"$2\"" "./agent $o\"$2\""; }
q list      "lista los archivos de docs"
q read      "muéstrame notas.txt"
q lines     "¿cuántas líneas tiene main.c?"
q search    "busca 'error' en log.txt"
q note      "anota 'examen el lunes' en agenda.md"
q mkdir     "crea la carpeta proyectos"
q disk      "¿cuánto espacio libre queda?"
q uname     "¿qué versión del núcleo tengo?"
q ps        "show running processes"
q delete_no "borra viejo.txt" "-n "
q delete_yes "borra viejo.txt" "-y "
q deny_abs  "borra /etc/passwd"
q deny_dots "lee ../../etc/shadow"
q deny_link "lee atajo"
q none      "cuéntame un chiste"
chk "list_files en docs"                     has list 'plan.md'
chk "read_file devuelve el contenido"        has read 'fork, exec, pipe'
chk "count_lines cuenta 5 líneas"            has lines 'main.c: 5 lines'
chk "search encuentra 2 líneas"              has search '2 matching line'
chk "write_note agrega la nota"              has note 'note appended to agenda.md'
chk "make_dir crea la carpeta"               has mkdir 'directory proyectos created'
chk "disk_usage responde"                    has disk 'GB available'
chk "system_info responde"                   has uname 'Linux'
chk "processes responde"                     has ps 'processes; the 5'
chk "delete_file pide consentimiento y respeta el 'no'" has delete_no 'cancelled by the user'
chk "delete_file con consentimiento"         has delete_yes 'viejo.txt deleted'
chk "política: ruta absoluta denegada"       has deny_abs 'policy: deny'
chk "política: '..' denegado"                has deny_dots 'policy: deny'
chk "política: enlace simbólico que escapa"  has deny_link 'outside the workspace'
chk "sin herramienta para un chiste"         has none 'policy: deny'

# 8 clientes a la vez: el pool de hilos los atiende a todos
rec concurrent "for i in \$(seq 8); do ./agent \"lista mis archivos\" & done; wait" \
    "for i in \$(seq 8); do ./agent 'lista mis archivos' > c\$i.out & done; wait; cat c*.out | grep -c '^model:'"
chk "8 peticiones concurrentes atendidas" [ "$(tail -1 "$OUT/td8/concurrent.txt")" = "8" ]
kill "$D"; wait "$D" 2>/dev/null
cp audit.log "$OUT/td8/audit_log.txt"
chk "cada decisión queda en la bitácora" [ "$(wc -l < audit.log)" -ge 17 ]

# ---------------------------------------------------------------- defensa en profundidad
./agentd -w workspace -a audit.log --no-policy > agentd.out 2>&1 &
D=$!
for _ in $(seq 50); do [ -S agentd.sock ] && break; sleep 0.05; done
q nopolicy_link "lee atajo"
q nopolicy_abs  "lee /etc/passwd"
kill "$D"; wait "$D" 2>/dev/null
chk "sin política, Landlock bloquea el enlace" has nopolicy_link 'Permission denied'
chk "sin política, Landlock bloquea /etc/passwd" has nopolicy_abs 'Permission denied'
rec sandbox "./agent_exec workspace read_file /etc/hostname; ./agent_exec --no-sandbox workspace read_file /etc/hostname"
chk "el mismo programa, con y sin Landlock" [ "$(grep -c 'Permission denied' "$OUT/td8/sandbox.txt")" -eq 1 ]

[ "$FAILS" -eq 0 ]

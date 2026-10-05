#!/usr/bin/env bash
# Pruebas automáticas de los laboratorios (TD0–TD8).
#   tds/tests/run.sh            todas            tds/tests/run.sh td3     solo un TD
# Cada caso:
#   1. ejecuta los programas tal como los escribe un estudiante (./programa en una carpeta de trabajo),
#   2. guarda la salida REAL en tests/out/tdN/<caso>.txt (el aula la incrusta con <!--@@out:tdN/caso@@-->),
#   3. comprueba lo esperado ("✔" o "✘").
# Los casos con salida no determinista (pids, carreras) se registran sin exigir un valor exacto.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$ROOT/bin
OUT=$ROOT/tests/out
TMP=$ROOT/tests/tmp
ONLY=${1:-}
PASS=0
FAIL=0

[ -x "$BIN/td1/echo" ] || { echo "Compila primero: make -C $ROOT"; exit 2; }
# Con un TD (run.sh td3) solo se rehace su carpeta: el puente de la terminal (tds/terminal.py) usa las demás.
if [ -n "$ONLY" ]; then rm -rf "${TMP:?}/$ONLY"; else rm -rf "$TMP"; fi
mkdir -p "$TMP" "$OUT"

ok() { printf '  \342\234\224 %s\n' "$1"; PASS=$((PASS + 1)); }
ko() { printf '  \342\234\230 %s\n' "$1"; FAIL=$((FAIL + 1)); }
check() { local d=$1; shift; if "$@"; then ok "$d"; else ko "$d"; fi; }
has() { grep -q -- "$2" "$OUT/$1.txt"; }               # has td1/echo "texto"

# Carpeta de trabajo del TD con enlaces a sus programas: los comandos se escriben como "./echo".
workdir() {
    W=$TMP/$1
    mkdir -p "$W" "$OUT/$1"
    for f in "$BIN/$1"/*; do [ -x "$f" ] && ln -sf "$f" "$W/$(basename "$f")"; done
    cd "$W" || exit 2
}

# record <td/caso> <comando mostrado> [comando real]: guarda "$ comando", la salida y el estado de salida.
record() {
    local name=$1 shown=$2 real=${3:-$2} rc
    { printf '$ %s\n' "$shown"; bash -c "$real"$'\n''exit $?' 2>&1; rc=$?
      [ $rc -ne 0 ] && printf '[exit status %d]\n' "$rc"; } | anonymize > "$OUT/$name.txt"
    LAST_RC=$(tail -1 "$OUT/$name.txt" | sed -n 's/^\[exit status \([0-9]*\)\]$/\1/p')
    LAST_RC=${LAST_RC:-0}
}

# Las salidas se publican en el aula: sin nombre de usuario, de equipo ni rutas personales.
anonymize() { sed -e "s#$ROOT#~/fso/tds#g" -e "s#$HOME#~#g" -e "s/$(id -un)/student/g" -e "s/\b$(hostname)\b/host/g"; }

port() { echo $((20000 + RANDOM % 20000)); }
wait_port() { for _ in $(seq 50); do (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null && return 0; sleep 0.05; done; return 1; }

want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }

# ------------------------------------------------------------------------------------------ TD0
if want td0; then echo "TD0 · entorno"; workdir td0
    record td0/errors "./errors"
    check "exit_if muestra el error de open y termina con 1" has td0/errors "missing.txt: No such file or directory"
    echo "some notes" > notes.txt
    record td0/errors_ok "./errors notes.txt"
    check "open de un archivo existente" has td0/errors_ok "opened as file descriptor 3"
    # Explorar el sistema: versión del núcleo, distribución, PID 1, módulos y tiempos de arranque
    record td0/explore "uname -srm; grep PRETTY_NAME /etc/os-release; ps -p 1 -o comm=; lsmod | tail -n +2 | wc -l; nproc; systemd-analyze"
    check "uname muestra el núcleo Linux" has td0/explore "Linux"
    if command -v systemd-analyze > /dev/null && systemd-analyze > /dev/null 2>&1; then
        systemd-analyze | sed -n 's/^Startup finished in //p' | tr '+' '\n' | sed -n 's/^ *\([0-9.]*\)s (\([a-z]*\)).*/\2,\1/p' | sed '1i stage,seconds' > "$OUT/td0/boot.csv"
        check "tiempos de arranque por etapa (systemd-analyze)" [ "$(wc -l < "$OUT/td0/boot.csv")" -ge 3 ]
    fi
fi

# ------------------------------------------------------------------------------------------ TD1
if want td1; then echo "TD1 · read/write"; workdir td1
    record td1/echo "./echo hola mundo"
    check "echo copia sus argumentos" [ "$(sed -n 2p "$OUT/td1/echo.txt")" = "hola mundo" ]
    record td1/echo_error "./echo blabla < . 1>&0"
    check "echo detecta el error de write" has td1/echo_error "write: Bad file descriptor"
    record td1/strace_echo "strace -e trace=write ./echo hola mundo > /dev/null"
    check "strace: una llamada write por palabra, espacio y salto" [ "$(grep -c '^write(1' "$OUT/td1/strace_echo.txt")" -eq 4 ]
    record td1/write_number_od "./write_number | od -A d -t x1"
    check "write_number escribe 8 + 8 bytes" has td1/write_number_od "0000016"
    record td1/read_number "./write_number | ./read_number"
    check "read_number reconstruye el long y el contador" has td1/read_number "value = 5, bytes written = 8"
    printf 'first line\nsecond line\n' > input.txt
    record td1/read "./read < input.txt"
    cp "$(command -v ls)" binary.bin
    record td1/read_copy "./read < binary.bin > copy.bin && cmp binary.bin copy.bin && echo identical"
    check "read copia un binario byte a byte" has td1/read_copy "identical"
    record td1/read_error "./read < ."
    check "read detecta que la entrada es un directorio" has td1/read_error "read: Is a directory"
    record td1/read_struct "./write_struct | ./read_struct"
    check "estructura leída con sus 24 bytes" has td1/read_struct "bytes written = 24"
    record td1/struct_sizes "./struct_sizes"
    check "tamaños 24 / 16 / 10" has td1/struct_sizes "size 10"
    record td1/strace_startup "strace -r ./echo hola > /dev/null     # the whole life of the process" \
        "strace -r -o trace.txt ./echo hola > /dev/null; cat trace.txt"
    check "strace: execve al inicio y exit_group al final" [ "$(grep -c 'execve\|exit_group' "$OUT/td1/strace_startup.txt")" -ge 2 ]
    record td1/bench_read "./bench_read      # 8 MiB file, buffers of 1 B to 1 MiB"
    grep -E '^[0-9]' "$OUT/td1/bench_read.txt" | sed '1i buffer_bytes,read_calls,seconds,MB_per_s,ns_per_iteration' > "$OUT/td1/bench_read.csv"
    check "bench_read: 11 tamaños de búfer medidos" [ "$(grep -cE '^[0-9]' "$OUT/td1/bench_read.txt")" -eq 11 ]
    LIBC=$(ldd ./echo | awk '/libc\.so/ {print $3}')
    record td1/objdump_getppid "objdump -d --no-show-raw-insn $LIBC | grep -A4 -E '^[0-9a-f]+ <getppid@@'" \
        "objdump -d --no-show-raw-insn $LIBC | grep -m1 -A4 -E '^[0-9a-f]+ <getppid@@'"
    check "getppid: número 110 (0x6e) en eax y la instrucción syscall" has td1/objdump_getppid "mov    \$0x6e,%eax"
    record td1/objdump_write "objdump -d --no-show-raw-insn $LIBC | grep -A6 -E '^[0-9a-f]+ <__write_nocancel@@'" \
        "objdump -d --no-show-raw-insn $LIBC | grep -m1 -A6 -E '^[0-9a-f]+ <__write_nocancel@@'"
    check "write: número 1 en eax, syscall y prueba de error (rax > -4096)" has td1/objdump_write "syscall"
fi

# ------------------------------------------------------------------------------------------ TD2
if want td2; then echo "TD2 · archivos, FIFO, dup, directorios"; workdir td2
    record td2/filetype_dir "./filetype < /etc"
    check "fstat: directorio" has td2/filetype_dir "directory"
    record td2/filetype_reg "./filetype < ./filetype"
    check "fstat: archivo regular" has td2/filetype_reg "regular file"
    record td2/filetype_pipe "echo hi | ./filetype"
    check "fstat: tubería" has td2/filetype_pipe "FIFO"
    printf abcdefgh > donnees.txt
    record td2/alias_open "./alias_open"
    record td2/alias_dup "./alias_dup"
    check "dos open = dos desplazamientos (abcdabcd)" has td2/alias_open "abcdabcd"
    check "dup = desplazamiento compartido (abcdefgh)" has td2/alias_dup "abcdefgh"
    record td2/fifo_chat "./fifo_server chat.fifo & sleep 0.3; echo 'hola desde el cliente' | ./fifo_client chat.fifo; wait"
    check "el servidor FIFO recibe el mensaje" has td2/fifo_chat "hola desde el cliente"
    cp "$(command -v ls)" photo.bin
    record td2/fifo_binary "./fifo_server bin.fifo > received.bin & sleep 0.3; ./fifo_client bin.fifo < photo.bin; wait; cmp photo.bin received.bin && echo 'binary file received intact'"
    check "la FIFO transmite un archivo binario intacto" has td2/fifo_binary "intact"
    mkdir -p demo/sub && echo hi > demo/a.txt && ln -sf a.txt demo/link && mkfifo demo/pipe
    record td2/myls "./myls demo"
    check "myls: tipo de cada entrada" has td2/myls "l link"
    record td2/myls_lR "./myls -lR demo"
    check "myls -l muestra el destino del enlace" has td2/myls_lR "link -> a.txt"
    # Explorar el sistema de archivos del equipo: inodos, enlaces, metadatos, archivo borrado pero abierto, extents
    echo "hola inodo" > notas.txt
    record td2/links "ln notas.txt copia.txt; ln -s notas.txt atajo; ls -li notas.txt copia.txt atajo"
    check "enlace duro: mismo número de inodo" [ "$(awk '$NF == "notas.txt" && $(NF-1) != "->" {print $1}' "$OUT/td2/links.txt")" = "$(awk '$NF == "copia.txt" {print $1}' "$OUT/td2/links.txt")" ]
    record td2/stat "stat notas.txt"
    check "stat: 2 enlaces duros" has td2/stat "Links: 2"
    record td2/unlink_open "exec 3< notas.txt; rm notas.txt copia.txt; readlink /proc/\$\$/fd/3; cat <&3; ls -l atajo; cat atajo"
    check "archivo borrado pero abierto: sigue legible" has td2/unlink_open "(deleted)"
    check "enlace simbólico colgante: ENOENT" has td2/unlink_open "No such file or directory"
    record td2/fs "df -iT . | cut -c1-80; stat -f -c 'block size: %S bytes' ."
    record td2/extents "head -c 10000000 /dev/urandom > big.bin; sync big.bin; filefrag -v big.bin"
    check "filefrag muestra los extents" has td2/extents "extents found"
fi

# ------------------------------------------------------------------------------------------ TD3
if want td3; then echo "TD3 · búferes y fork"; workdir td3
    for i in 1 2 3 4 5; do
        record td3/buffer_${i}_pipe "./buffer_$i 2>&1 | cat -u"
        record td3/buffer_${i}_tty "./buffer_$i            # en una terminal" "script -qc ./buffer_$i /dev/null | tr -d '\r'"
    done
    check "búfer por línea (terminal) respeta el orden con \\n" [ "$(sed -n 2p "$OUT/td3/buffer_4_tty.txt")" = "Hello" ]
    check "búfer completo (tubería) invierte el orden" [ "$(sed -n 2p "$OUT/td3/buffer_4_pipe.txt")" = "World!" ]
    check "_exit pierde el contenido del búfer" [ "$(grep -c Hello "$OUT/td3/buffer_5_pipe.txt")" -eq 0 ]
    for i in 0 1 2 3; do record td3/fork_pid_$i "./fork_pid_$i | cat"; done
    check "sin solución, 'My pid is' aparece dos veces" [ "$(grep -o 'My pid is' "$OUT/td3/fork_pid_0.txt" | wc -l)" -eq 2 ]
    for i in 1 2 3; do check "solución $i: una sola vez" [ "$(grep -o 'My pid is' "$OUT/td3/fork_pid_$i.txt" | wc -l)" -eq 1 ]; done
    mkdir -p demo && touch demo/a.txt demo/b.txt
    record td3/launch "./launch out.txt ls -1 demo; cat out.txt"
    check "launch redirige y reporta el estado" has td3/launch "exited with status 0"
    record td3/strace_launch "strace -f -r -e trace=process,openat,dup2,write,close,execve ./launch out.txt ls -1 demo" \
        "strace -f -r -e trace=process,openat,dup2,write,close,execve -o trace.txt ./launch out.txt ls -1 demo 2>/dev/null; grep -v 'openat.*\\.so\\|ENOENT\|/etc/ld.so\|locale\|gconv' trace.txt"
    check "strace -f: el hijo hace dup2 y execve" [ "$(grep -c 'dup2\|execve' "$OUT/td3/strace_launch.txt")" -ge 3 ]
    record td3/launch_segv "./launch out.txt ./segfault"
    check "launch reporta la señal SIGSEGV" has td3/launch_segv "killed by signal 11 (SIGSEGV)"
    record td3/launch_127 "./launch out.txt nocommand"
    check "comando inexistente: estado 127" has td3/launch_127 "status 127"
    # Exploración de la memoria: zonas y ASLR, asignación perezosa y copia en escritura (datos reales)
    record td3/zones "./zones"
    check "zones: heap y stack en /proc/self/maps" [ "$(grep -c '\[heap\]\|\[stack\]' "$OUT/td3/zones.txt")" -ge 2 ]
    record td3/zones_aslr "for i in 1 2 3; do ./zones | grep main; done"
    check "ASLR: main cambia de dirección en cada ejecución" [ "$(awk '/^code/{print $3}' "$OUT/td3/zones_aslr.txt" | sort -u | wc -l)" -eq 3 ]
    record td3/lazy "./lazy 1024"
    check "asignación perezosa: un fallo de página por página tocada" awk '/^pages touched/ { d = $3 - $9; exit !(d >= -64 && d <= 64) }' "$OUT/td3/lazy.txt"
    record td3/cow "./cow 256"
    check "copia en escritura: leer no copia" awk '/child reads/ { exit !($5 < 64) }' "$OUT/td3/cow.txt"
    check "copia en escritura: una copia por página escrita" awk '/child writes/ { d = $3 - $5; exit !(d >= -64 && d <= 64) }' "$OUT/td3/cow.txt"
    record td3/lifecycle "./lifecycle"
    check "zombi: ps muestra el estado Z antes de waitpid" grep -q ' Z ' "$OUT/td3/lifecycle.txt"
    check "waitpid recupera el estado 7" has td3/lifecycle "WEXITSTATUS = 7"
    check "huérfano: lo adopta otro proceso" has td3/lifecycle "now it is"
fi

# ------------------------------------------------------------------------------------------ TD4
if want td4; then echo "TD4 · tuberías"; workdir td4
    record td4/mypipe "./mypipe"
    check "el hijo lee los 120 bytes" has td4/mypipe "read 120 bytes"
    record td4/mypipe_big "./mypipe 10000 > /dev/null; echo \"exit status of mypipe: \$?\""
    check "más de 64 KiB: el padre muere por SIGPIPE (141)" has td4/mypipe_big "exit status of mypipe: 141"
    record td4/mypipe_bug "timeout 3 ./mypipe_bug 10000 > /dev/null; echo \"exit status: \$? (124 = killed by timeout: it was blocked)\""
    check "sin cerrar el extremo de lectura: bloqueo" has td4/mypipe_bug "exit status: 124"
    # La carrera: el hijo lee 500 bytes y sale mientras el padre sigue escribiendo (30 ejecuciones por tamaño)
    race='for n in 50 100 200 300 400 500 1000; do ok=0; sp=0; for i in $(seq 30); do ./mypipe $n >/dev/null 2>&1; case $? in 0) ok=$((ok+1));; 141) sp=$((sp+1));; esac; done; printf "%5d messages: %2d x status 0   %2d x status 141 (SIGPIPE)\n" $n $ok $sp; done'
    record td4/mypipe_race "for n in 50 100 … 1000; do run ./mypipe n 30 times; count exit statuses; done" "env --default-signal=PIPE bash -c '$race'"
    check "carrera: con pocos mensajes el padre termina bien" grep -q "50 messages: 30 x status 0" "$OUT/td4/mypipe_race.txt"
    check "carrera: con 1000 mensajes siempre SIGPIPE" grep -q "1000 messages:  0 x status 0   30 x status 141" "$OUT/td4/mypipe_race.txt"
    record td4/pipe_capacity "python3 -c 'import os, fcntl; r, w = os.pipe(); print(fcntl.fcntl(w, fcntl.F_GETPIPE_SZ), \"bytes\")'"
    check "capacidad por defecto de una tubería = 65536" has td4/pipe_capacity "65536 bytes"
    record td4/upper "./upper | head -4"
    check "upper: ps en mayúsculas" has td4/upper "CMD"
    mkdir -p demo && echo "a b" > demo/one.txt && echo c > demo/two.txt
    record td4/pipeline "cd demo && ../pipeline"
    check "pipeline = ls -l | tr -d '[:blank:]'" [ "$(sed 1d "$OUT/td4/pipeline.txt")" = "$(cd demo && ls -l | tr -d '[:blank:]' | anonymize)" ]
    record td4/pipeline_args "./pipeline_args ls -l demo -- grep txt -- wc -l"
    check "tres comandos encadenados" [ "$(sed -n 2p "$OUT/td4/pipeline_args.txt")" = "2" ]
    record td4/pipeline_status "./pipeline_args ls -- sh -c 'exit 3'; echo \"status: \$?\""
    check "devuelve el estado del último comando" has td4/pipeline_status "status: 3"
    record td4/pipeline_signal "./pipeline_args ls -- sh -c 'kill -TERM \$\$'"
    check "informa la señal del último comando" has td4/pipeline_signal "killed by SIGTERM"
fi

# ------------------------------------------------------------------------------------------ TD5
if want td5; then echo "TD5 · señales y memoria compartida"; workdir td5
    record td5/mysignal "./mysignal 2 & P=\$!; sleep 0.3; kill -USR1 \$P; sleep 0.2; kill -STOP \$P; kill -USR1 \$P; sleep 0.2; kill -CONT \$P; sleep 0.2; kill -USR1 \$P; wait \$P; echo \"exit status: \$? (128 + 10 = killed by SIGUSR1)\""
    check "dos señales capturadas, la tercera termina el proceso" has td5/mysignal "exit status: 138"
    check "SIGUSR1 enviada con el proceso detenido queda pendiente" [ "$(grep -c 'received signal 10' "$OUT/td5/mysignal.txt")" -eq 2 ]
    record td5/mmap_signal "./receiver & sleep 0.3; echo 'hello from the sender' | ./sender; wait"
    check "mmap + SIGUSR1: el mensaje llega" has td5/mmap_signal "received: hello from the sender"
    record td5/mmap_limits "./mmap_limits"
    check "la página completa existe, la siguiente da SIGBUS" has td5/mmap_limits "byte 4096 raised SIGBUS"
    for m in none read write exec ew; do record td5/map_$m "./map_$m"; done
    check "PROT_NONE: lectura prohibida" has td5/map_none "exit status 139"
    check "PROT_READ: escritura prohibida" has td5/map_read "exit status 139"
    check "PROT_WRITE: lee y escribe" has td5/map_write "\*p = 42"
    record td5/load_use_add "./load_add && ./use_add"
    check "código copiado a un archivo y ejecutado" has td5/load_use_add "f(42, 12) = 54"
    ./shm_counter --remove > /dev/null 2>&1
    record td5/shm_counter "./shm_counter; ./shm_counter; ./shm_counter; ./shm_counter --remove"
    check "el segmento persiste entre ejecuciones (1, 2, 3)" has td5/shm_counter "counter = 3"
    record td5/shm_fork "./shm_fork"
    check "16 procesos registrados con fetch-and-add atómico" has td5/shm_fork "16 processes registered (expected 16)"
    record td5/shm_race "for i in 1 2 3 4 5; do ./shm_race 5 | cut -d: -f1; done"
fi

# ------------------------------------------------------------------------------------------ TD6
if want td6; then echo "TD6 · hilos y concurrencia"; workdir td6
    record td6/threads "./threads"
    check "8 hilos terminados" has td6/threads "all 8 threads finished"
    record td6/tickets_race "./tickets_race 100000 | tail -3"
    record td6/tickets_mutex "./tickets_mutex 100000 | tail -3"
    record td6/tickets_sem "./tickets_sem 100000 | tail -3"
    check "mutex: exactamente 100000" has td6/tickets_mutex "100000 tickets sold out of 100000 (tickets = 0)"
    check "semáforo: exactamente 100000" has td6/tickets_sem "100000 tickets sold out of 100000 (tickets = 0)"
    record td6/prodcons "./prodcons | tail -4"
    check "productor/consumidor alternados: suma correcta" has td6/prodcons "OK"
    record td6/prodcons_multi "./prodcons_multi 4 100000"
    check "1 productor, 4 consumidores: cada valor una sola vez" has td6/prodcons_multi "OK"
    record td6/philosophers "./philosophers 3 | tail -3"
    check "con orden global de palillos no hay interbloqueo" has td6/philosophers "dinner finished"
    record td6/philosophers_deadlock "timeout 3 ./philosophers_deadlock 3 | tail -8; echo \"exit status: \${PIPESTATUS[0]} (124 = still blocked after 3 s)\""
    # Mediciones reales: costo de crear hilos y procesos, Peterson sin y con barrera, y el futex de un mutex
    record td6/create_cost "./create_cost 2000"
    check "crear un proceso cuesta más que crear un hilo" awk '/times a thread/ { exit !($5 > 1) }' "$OUT/td6/create_cost.txt"
    record td6/peterson "for i in 1 2 3 4 5; do ./peterson; done; for i in 1 2 3 4 5; do ./peterson_fence; done"
    check "Peterson con barrera: nunca pierde incrementos" [ "$(grep -c 'with fence: .*lost 0$' "$OUT/td6/peterson.txt")" -eq 5 ]
    record td6/lock_cost "./lock_cost 1; ./lock_cost 8"
    record td6/lock_futex "strace -f -c -e trace=futex ./lock_cost 1 100000; strace -f -c -e trace=futex ./lock_cost 8 100000" \
        "for t in 1 8; do strace -f -c -e trace=futex ./lock_cost \$t 100000 2>&1 | grep -E 'lock/unlock| futex\$'; done"
    check "sin contención, el mutex casi no llama al núcleo" [ "$(awk '/ futex$/ {print $4; exit}' "$OUT/td6/lock_futex.txt")" -le 2 ]
fi

# ------------------------------------------------------------------------------------------ TD7
if want td7; then echo "TD7 · sockets"; workdir td7
    P=$(port)
    record td7/sock "./sock $P & sleep 0.3; nc -z 127.0.0.1 $P && echo 'client connected'; wait"
    check "servidor mínimo acepta y cierra" has td7/sock "connection accepted"
    TC=$ROOT/tests/tcp_client.py
    P=$(port); ./echo_server $P > /dev/null & S=$!; wait_port $P
    record td7/echo "python3 tcp_client.py echo $P 'hola\\nmundo\\nquit'" "python3 $TC echo $P 'hola\\nmundo\\nquit'"
    check "servidor eco" has td7/echo "received: mundo"
    record td7/echo_iterative "python3 tcp_client.py blocked $P     # iterative server" "python3 $TC blocked $P"
    check "servidor iterativo: el segundo cliente espera" has td7/echo_iterative "busy with client A"
    kill $S; wait $S 2>/dev/null
    P=$(port); ./echo_server $P -f > /dev/null & S=$!; wait_port $P
    record td7/echo_fork "python3 tcp_client.py blocked $P     # server with fork" "python3 $TC blocked $P"
    check "servidor con fork: atiende a los dos" has td7/echo_fork "client B got 'hi"
    kill $S; wait $S 2>/dev/null
    mkdir -p www && printf '<h1>FSO · UTI</h1>\n' > www/index.html && cp "$(command -v ls)" www/program.bin
    for mode in iterative fork thread pool epoll; do
        P=$(port); ./http_server -p $P -m $mode -d www > /dev/null & S=$!; wait_port $P
        record td7/http_$mode "curl -s -i http://localhost:PORT/ | tr -d '\r'   # mode $mode" "curl -s -i http://127.0.0.1:$P/ | tr -d '\r'"
        check "http ($mode): 200 y el contenido" has td7/http_$mode "FSO · UTI"
        curl -s -o got.bin "http://127.0.0.1:$P/program.bin"
        check "http ($mode): archivo binario idéntico" cmp -s got.bin www/program.bin
        N=$(seq 40 | xargs -P 8 -I{} curl -s -o /dev/null -w '%{http_code}\n' "http://127.0.0.1:$P/" | grep -c 200)
        check "http ($mode): 40 peticiones concurrentes, 40 respuestas 200" [ "$N" -eq 40 ]
        if [ $mode = pool ]; then
            record td7/http_errors "curl -s -o /dev/null -w '%{http_code}' .../missing.html ; curl --path-as-is .../../etc/passwd" \
                "curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:$P/missing.html; curl -s -o /dev/null -w '%{http_code}\n' --path-as-is http://127.0.0.1:$P/../etc/passwd"
            check "404 para lo que no existe y 403 para '..'" [ "$(sed 1d "$OUT/td7/http_errors.txt" | tr '\n' ' ')" = "404 403 " ]
        fi
        # Server models under load: 1 then 8 slow clients (1 s) among 32 (TD7 · 4).
        record td7/load_$mode "./http_server -m $mode &  ./load PORT 1 31 1000; ./load PORT 8 24 1000" \
            "./load $P 1 31 1000; ./load $P 8 24 1000"
        kill $S; wait $S 2>/dev/null
    done
    fast() { awk '/fast/ {print $4}' "$OUT/td7/load_$1.txt" | sed -n "$2p"; }   # median, run 1 or 2
    check "iterativo: 1 cliente lento hace esperar a los rápidos" [ "$(fast iterative 1)" -ge 800 ]
    check "pool de 8: 8 clientes lentos lo saturan" [ "$(fast pool 1)" -lt 300 ] && [ "$(fast pool 2)" -ge 800 ]
    for mode in fork thread epoll; do
        check "$mode: los rápidos no esperan a los lentos" [ "$(fast $mode 1)" -lt 300 ] && [ "$(fast $mode 2)" -lt 300 ]
    done

    # Explorations (TD7 · 4): TCP states, the listen queue, TCP vs UDP, addresses, AF_UNIX.
    P=$(port)
    record td7/tcp_states "./tcp_states PORT; ss -tn state time-wait '( dport = :PORT )'" \
        "./tcp_states $P; ss -tn state time-wait '( dport = :$P )' | sed 's/:$P\\b/:PORT/'"
    check "estados TCP: la conexión queda en la cola antes de accept" has td7/tcp_states "queue 1/4) | client ESTABLISHED | server -"
    check "estados TCP: quien cierra primero pasa por FIN_WAIT2 y TIME_WAIT" [ "$(grep -c 'FIN_WAIT2\|127.0.0.1:PORT' "$OUT/td7/tcp_states.txt")" -eq 2 ]
    record td7/backlog "./backlog 4 8"
    check "listen(4): 5 conexiones completas en cola, 3 en SYN_SENT" has td7/backlog "accept queue 5 | clients ESTABLISHED 5, SYN_SENT 3"
    check "listen(4): tras aceptar, los SYN reenviados entran" has td7/backlog "all 8 clients accepted"
    record td7/backlog_cookies "nstat -n; ./backlog 2 8; nstat | grep -E 'Syncookies|ListenOverflows'" \
        "nstat -n; ./backlog 2 8; nstat | grep -E 'SyncookiesSent|ListenOverflows' | awk '{print \$1, \$2}'"
    record td7/udp "./udp"
    check "TCP junta tres write en un read" has td7/udp '11 bytes: "onetwothree"'
    check "UDP conserva los límites de cada datagrama" has td7/udp 'recvfrom #3: 5 "three"'
    check "UDP pierde datagramas sin avisar y el núcleo los cuenta" \
        [ "$(awk '/lost$/ {print $(NF-1)}' "$OUT/td7/udp.txt")" -gt 0 ]
    record td7/addr "./addr 7000"
    check "sin htons, 7000 se convierte en 22555" has td7/addr "sin_port = 7000: .*port 22555"
    check "getaddrinfo resuelve IPv4 e IPv6" has td7/addr "IPv6 ::1 port 443"
    record td7/local "./local fso.sock"
    check "AF_UNIX: la dirección es un archivo de tipo socket" has td7/local "a file of type socket"
fi

# ------------------------------------------------------------------------------------------ TD8
if want td8 && [ -x "$ROOT/td8/tests.sh" ]; then echo "TD8 · SO agéntico con un micro-LLM"
    mkdir -p "$OUT/td8"
    OUT=$OUT TMP=$TMP BIN=$BIN "$ROOT/td8/tests.sh" && ok "td8/tests.sh" || ko "td8/tests.sh"
fi

# ------------------------------------------------------------------------------------------ Terminal del aula
# Puente tds/terminal.py: seguridad, protocolo, shell real y los comandos con botón «Ejecutar» repetidos en tests/tmp.
if want terminal; then echo "Terminal del aula · puente tds/terminal.py"
    res=$(python3 "$ROOT/tests/terminal_test.py" 2>&1); echo "$res"
    PASS=$((PASS + $(grep -c '✔' <<< "$res"))); FAIL=$((FAIL + $(grep -c '✘' <<< "$res")))
fi

cd "$ROOT" || exit 2
echo
echo "Resultado: $PASS correctas, $FAIL fallidas · salidas reales en tests/out/"
[ "$FAIL" -eq 0 ]

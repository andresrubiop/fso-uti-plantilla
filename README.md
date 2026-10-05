# Laboratorios de Fundamentos de los Sistemas Operativos (UTI)

Este repositorio es **tuyo**: lo creó GitHub Classroom al aceptar la tarea. Aquí trabajas los laboratorios TD0–TD8
y entregas tu código.

## Trabajar en GitHub Codespaces (recomendado: no instalas nada)

1. Abre el codespace **desde la tarea** (botón *Open in GitHub Codespaces*) o aquí: *Code → Codespaces → Create
   codespace on main*. Su uso lo cubre el beneficio educativo del curso. La primera vez tarda unos minutos.
2. En la terminal de abajo: `python3 tds/terminal.py`
3. Abre con Ctrl+clic el enlace `https://…app.github.dev/?terminal=aqui.…` que imprime: es el aula con su terminal
   conectada a tu codespace («▶ Ejecutar», «Preparar TDn», «▶_ Terminal»).
4. Edita el código en VS Code (`tds/td1/…`) y **guarda tu trabajo en GitHub**: panel *Source Control* (Ctrl+Shift+G)
   → escribe un mensaje → *Commit* → *Sync Changes*. Así queda entregado.
5. **Informe de cada laboratorio:** en LaTeX con la plantilla del curso (aula → pestaña **Tarea** de cada unidad:
   qué debe cubrir, plantilla, ejemplo y guía de Prism). Guarda el PDF y el `.zip` en `informes/tdN/`
   de este repositorio y súbelos igual que el código. IA generativa permitida, con declaración.

- Deja el puerto 8765 **privado** (así viene).
- Al terminar, detén el codespace (github.com/codespaces → «…» → *Stop*). Para volver, abre el mismo: conserva
  tus archivos.

## Otras formas

- **Aula en línea** (sin terminal): https://andresrubiop.github.io/fso-uti/
- **En tu Ubuntu 26.04:** `git clone` de este repositorio, `sudo apt install build-essential manpages-dev manpages-posix-dev strace gdb curl netcat-openbsd iproute2 e2fsprogs python3 python3-numpy`, `make -C tds`, `python3 tds/terminal.py`.

Uso de IA generativa: permitido y declarado. Se evalúa que puedas explicar y verificar cada decisión.

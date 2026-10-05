#!/usr/bin/env bash
# Prepara el Codespace del curso (se ejecuta una vez, al crearlo): paquetes de Ubuntu y laboratorios compilados.
set -e
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential manpages-dev manpages-posix-dev strace gdb curl netcat-openbsd iproute2 e2fsprogs python3 python3-numpy
make -C tds -s
echo
echo "Listo. Terminal del aula: python3 tds/terminal.py   (abre el enlace https://… que imprime)"

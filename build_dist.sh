#!/bin/bash
# Arma dist/PSP/GAME/SUBWAY con el EBOOT y (si existe) la carpeta data/ generada con tools/extract.py
set -e
cd "$(dirname "$0")"
make
make -C exhandler      # capturador de excepciones (crash.txt)
rm -rf dist
mkdir -p dist/PSP/GAME/SUBWAY
cp EBOOT.PBP dist/PSP/GAME/SUBWAY/
cp exhandler/exhandler.prx dist/PSP/GAME/SUBWAY/
cp subway.elf dist/subway.elf   # para traducir crash.txt a funcion y linea (tools/crash.sh); no va a la PSP
if [ -d data ]; then cp -r data dist/PSP/GAME/SUBWAY/; rm -rf dist/PSP/GAME/SUBWAY/data/wav dist/PSP/GAME/SUBWAY/data/meshes.txt dist/PSP/GAME/SUBWAY/data/.version; fi
echo "Listo: dist/PSP/GAME/SUBWAY (copiala a la Memory Stick en ms0:/PSP/GAME/)"

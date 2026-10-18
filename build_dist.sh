#!/bin/bash
# Arma dist/PSP/GAME/SUBWAY con el EBOOT y (si existe) la carpeta data/ generada con tools/extract.py
set -e
cd "$(dirname "$0")"
# siempre desde cero: con objetos viejos, un .c sin cambios no se recompila aunque cambie un .h que usa
# (paso con NodeRT en scene.h: anim.o se quedo con la estructura antigua y la camara y los personajes salian rotos)
make clean > /dev/null
make -C exhandler clean > /dev/null 2>&1 || true
make -C exhandler/loader clean > /dev/null 2>&1 || true
make
make -C exhandler      # capturador de excepciones (crash.txt)
make -C exhandler/loader   # plan B para cargarlo con kubridge
rm -rf dist
mkdir -p dist/PSP/GAME/SUBWAY
cp EBOOT.PBP dist/PSP/GAME/SUBWAY/
cp exhandler/exhandler.prx exhandler/loader/exloader.prx dist/PSP/GAME/SUBWAY/
cp subway.elf dist/subway.elf   # para traducir crash.txt a funcion y linea (tools/crash.sh); no va a la PSP
if [ -d data ]; then cp -r data dist/PSP/GAME/SUBWAY/; rm -rf dist/PSP/GAME/SUBWAY/data/wav dist/PSP/GAME/SUBWAY/data/meshes.txt dist/PSP/GAME/SUBWAY/data/.version; fi
echo "Listo: dist/PSP/GAME/SUBWAY (copiala a la Memory Stick en ms0:/PSP/GAME/)"

#!/bin/bash
# Arma dist/PSP/GAME/SUBWAY con el EBOOT y (si existe) la carpeta data/ generada con tools/extract.py
#   ./build_dist.sh            version publica (sin devlog)
#   DEVLOG=1 ./build_dist.sh   version de pruebas: devlog.txt, rastreo de tirones y capturador de cuelgues (crash.txt)
set -e
cd "$(dirname "$0")"
DEVLOG=${DEVLOG:-0}
# siempre desde cero: con objetos viejos, un .c sin cambios no se recompila aunque cambie un .h que usa
# (paso con NodeRT en scene.h: anim.o se quedo con la estructura antigua y la camara y los personajes salian rotos)
make clean > /dev/null
make -C exhandler clean > /dev/null 2>&1 || true
make -C exhandler/loader clean > /dev/null 2>&1 || true
make DEVLOG=$DEVLOG
rm -rf dist
mkdir -p dist/PSP/GAME/SUBWAY
cp EBOOT.PBP dist/PSP/GAME/SUBWAY/
if [ "$DEVLOG" != 0 ]; then
    make -C exhandler      # capturador de excepciones (crash.txt)
    make -C exhandler/loader   # plan B para cargarlo con kubridge
    cp exhandler/exhandler.prx exhandler/loader/exloader.prx dist/PSP/GAME/SUBWAY/
    cp subway.elf dist/subway.elf   # para traducir crash.txt a funcion y linea (tools/crash.sh); no va a la PSP
fi
if [ -d data ]; then
    cp -rL data dist/PSP/GAME/SUBWAY/   # -L: si data es un enlace, se copia lo que hay dentro
    rm -rf dist/PSP/GAME/SUBWAY/data/{wav,meshes.txt,.version,ICON0.PNG,PIC1.PNG}
fi
[ "$DEVLOG" != 0 ] && echo "Version de PRUEBAS (con devlog)" || echo "Version publica (sin devlog)"
echo "Listo: dist/PSP/GAME/SUBWAY (copiala a la Memory Stick en ms0:/PSP/GAME/)"

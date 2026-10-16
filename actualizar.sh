#!/bin/bash
# Actualiza el codigo, regenera data/ solo si el extractor ha cambiado, compila y arma dist/PSP/GAME/SUBWAY.
#
#   ./actualizar.sh                         usa el primer .apk de esta carpeta
#   ./actualizar.sh subway.apk              APK concreto
#   ./actualizar.sh subway.apk "/mnt/c/Users/Joan jimenez/Desktop"
#                                           ademas copia la carpeta PSP ahi (p. ej. el escritorio de Windows)
set -e
cd "$(dirname "$0")"

echo "== 1/3 Bajando cambios =="
git pull --ff-only || echo "(git pull no pudo actualizar; se compila lo que hay)"

echo "== 2/3 Datos del juego =="
APK=${1:-$(ls *.apk 2>/dev/null | head -1)}
VER=$(cat tools/extract.py tools/unityscene.py | sha1sum | cut -c1-16)
if [ -f data/.version ] && [ "$(cat data/.version)" = "$VER" ]; then
    echo "data/ esta al dia (el extractor no ha cambiado)"
else
    if [ -z "$APK" ] || [ ! -f "$APK" ]; then
        echo "Hay que regenerar data/ pero no encuentro el APK. Copialo a esta carpeta o pasalo: ./actualizar.sh archivo.apk"
        exit 1
    fi
    [ -f ~/venv-subway/bin/activate ] && source ~/venv-subway/bin/activate
    echo "El extractor ha cambiado (o es la primera vez): regenerando data/ desde $APK ..."
    python3 tools/extract.py "$APK" data/
    echo "$VER" > data/.version
fi

echo "== 3/3 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
./build_dist.sh

if [ -n "$2" ]; then
    mkdir -p "$2"
    cp -r dist/PSP "$2/"
    echo "Copiado a: $2/PSP"
fi

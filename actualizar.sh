#!/bin/bash
# Actualiza el codigo, regenera data/ solo si el extractor ha cambiado, compila y arma dist/PSP/GAME/SUBWAY.
#
#   ./actualizar.sh                         usa el primer .apk de esta carpeta
#   ./actualizar.sh subway.apk              APK concreto
#   ./actualizar.sh subway.apk "/mnt/c/Users/Joan jimenez/Desktop"
#                                           ademas copia la carpeta PSP ahi (p. ej. el escritorio de Windows)
#
# Halloween (1.4 clasica): si en esta carpeta hay un APK de la 1.4 (un nombre con "1.4" o "1-4", p. ej.
# subway-surfers-1-4-0.apk), se saca tambien el tema de Halloween y Zombie Jake. Tambien se puede indicar con
# APK14=ruta.apk ./actualizar.sh ...
set -e
cd "$(dirname "$0")"

if [ -z "$SS_ACTUALIZADO" ]; then
    echo "== 1/3 Bajando cambios =="
    git pull --ff-only || echo "(git pull no pudo actualizar; se compila lo que hay)"
    # si git pull ha cambiado este mismo script, bash seguiria leyendo la version vieja: se vuelve a empezar
    SS_ACTUALIZADO=1 exec "$0" "$@"
fi

echo "== 2/3 Datos del juego =="
APK14=${APK14:-$(ls *1.4*.apk *1-4*.apk 2>/dev/null | head -1)}
APK=${1:-$(ls *.apk 2>/dev/null | grep -v -e '1\.4' -e '1-4' | head -1)}
VER=$( (cat tools/extract.py tools/unityscene.py; echo "halloween:$APK14") | sha1sum | cut -c1-16)
if [ -f data/.version ] && [ "$(cat data/.version)" = "$VER" ]; then
    echo "data/ esta al dia (el extractor no ha cambiado)"
else
    if [ -z "$APK" ] || [ ! -f "$APK" ]; then
        echo "Hay que regenerar data/ pero no encuentro el APK. Copialo a esta carpeta o pasalo: ./actualizar.sh archivo.apk"
        exit 1
    fi
    [ -f ~/venv-subway/bin/activate ] && source ~/venv-subway/bin/activate
    echo "El extractor ha cambiado (o es la primera vez): regenerando data/ desde $APK ..."
    if [ -n "$APK14" ] && [ -f "$APK14" ]; then
        echo "Con el tema de Halloween de $APK14"
        python3 tools/extract.py "$APK" data/ --halloween "$APK14"
    else
        python3 tools/extract.py "$APK" data/
    fi
    echo "$VER" > data/.version
fi

echo "== 3/3 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
./build_dist.sh

if [ -n "$2" ]; then
    # no se crea la carpeta destino: si no existe es que la ruta esta mal escrita (y en /mnt/c/Users no hay permiso)
    if [ ! -d "$2" ]; then
        echo
        echo "No existe la carpeta: $2"
        echo "El juego SI se ha compilado: esta en $(pwd)/dist/PSP"
        echo "Escritorios encontrados en Windows (usa uno de estos, entre comillas):"
        ls -d /mnt/c/Users/*/Desktop /mnt/c/Users/*/Escritorio /mnt/c/Users/*/OneDrive*/Desktop \
              /mnt/c/Users/*/OneDrive*/Escritorio 2>/dev/null | sed 's/^/    "/; s/$/"/' || true
        exit 1
    fi
    cp -r dist/PSP "$2/"
    echo "Copiado a: $2/PSP"
fi

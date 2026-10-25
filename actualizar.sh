#!/bin/bash
# Actualiza el codigo, regenera data/ solo si el extractor ha cambiado, compila y arma dist/PSP/GAME/SUBWAY.
#
#   ./actualizar.sh                         usa el primer .apk de esta carpeta
#   ./actualizar.sh subway.apk              APK concreto
#   ./actualizar.sh subway.apk "/mnt/c/Users/Joan jimenez/Desktop"
#                                           ademas copia la carpeta PSP ahi (p. ej. el escritorio de Windows)
#
# Contenido de la 1.4 (Halloween, personajes nuevos, menus): si en esta carpeta hay un APK de la 1.4 clasica, se
# saca tambien lo que trae hasta esa version. Se reconoce por dentro (tools/apk_kind.py), se llame como se llame.
# Tambien se puede indicar con APK14=ruta.apk ./actualizar.sh ...
#
# Sale la version publica (sin devlog). Para la de pruebas (devlog.txt y crash.txt): DEVLOG=1 ./actualizar.sh ...
set -e
cd "$(dirname "$0")"

if [ -z "$SS_ACTUALIZADO" ]; then
    echo "== 1/3 Bajando cambios =="
    git pull --ff-only || echo "(git pull no pudo actualizar; se compila lo que hay)"
    # si git pull ha cambiado este mismo script, bash seguiria leyendo la version vieja: se vuelve a empezar
    SS_ACTUALIZADO=1 exec "$0" "$@"
fi

echo "== 2/3 Datos del juego =="
# que APK es cada uno: se mira dentro (el de la 1.4 clasica trae ThemeAssets), no el nombre
APK10=""
for f in *.apk; do
    [ -f "$f" ] || continue
    case "$(python3 tools/apk_kind.py "$f")" in
        14) [ -z "$APK14" ] && APK14="$f" ;;
        10) [ -z "$APK10" ] && APK10="$f" ;;
    esac
done
APK=${1:-$APK10}
if [ -n "$APK" ] && [ -f "$APK" ] && [ "$(python3 tools/apk_kind.py "$APK")" = 14 ]; then
    echo "$APK es el APK de la 1.4: hace falta tambien el de la 1.0 (la base). Copia los dos a esta carpeta."
    exit 1
fi
if [ -n "$APK14" ]; then echo "APK de la 1.4 clasica: $APK14"
else echo "AVISO: no hay APK de la 1.4 clasica (1.4.0, la de Halloween de 2012): sin Halloween ni contenido de la 1.4"; fi
VER=$( (cat tools/extract.py tools/unityscene.py; echo "apk14:$APK14") | sha1sum | cut -c1-16)
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
        echo "Con el contenido de la 1.4 de $APK14"
        python3 tools/extract.py "$APK" data/ --apk14 "$APK14"
    else
        python3 tools/extract.py "$APK" data/
    fi
    echo "$VER" > data/.version
fi

echo "== 3/3 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
./build_dist.sh
echo
if [ -f data/hw_tex.bin ]; then
    echo "Halloween y contenido de la 1.4: INCLUIDOS (el tema se activa solo del 24 de octubre al 4 de noviembre,"
    echo "segun la fecha de la consola: compruebala en Ajustes del sistema > Fecha y hora)"
else
    echo "Halloween y contenido de la 1.4: NO incluidos. Copia a esta carpeta el APK de Subway Surfers 1.4.0"
    echo "(la version de Halloween de 2012) y vuelve a ejecutar ./actualizar.sh"
fi

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

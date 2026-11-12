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
# Sin el stick analogico (consolas con el stick roto): NOANALOG=1 ./actualizar.sh ... (se puede juntar con DEVLOG=1)
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
        16) [ -z "$APK16" ] && APK16="$f" ;;
        17) [ -z "$APK17" ] && APK17="$f" ;;
        18) [ -z "$APK18" ] && APK18="$f" ;;
        19) [ -z "$APK19" ] && APK19="$f" ;;
        110) [ -z "$APK110" ] && APK110="$f" ;;
        111) [ -z "$APK111" ] && APK111="$f" ;;
        15) [ -z "$APK15" ] && APK15="$f" ;;
        14) [ -z "$APK14" ] && APK14="$f" ;;
        10) [ -z "$APK10" ] && APK10="$f" ;;
    esac
done
APK=${1:-$APK10}
if [ -n "$APK" ] && [ -f "$APK" ] && [ "$(python3 tools/apk_kind.py "$APK")" != 10 ]; then
    echo "$APK no es el APK de la 1.0: hace falta tambien el de la 1.0 (la base). Copialos todos a esta carpeta."
    exit 1
fi
if [ -n "$APK15" ]; then echo "APK de la 1.5.2: $APK15 (mundo nuevo, Navidad y tablas)"; fi
if [ -n "$APK14" ]; then echo "APK de la 1.4 clasica: $APK14"
else echo "AVISO: no hay APK de la 1.4 clasica (1.4.0, la de Halloween de 2012): sin Halloween ni contenido de la 1.4"; fi
VER=$( (cat tools/extract.py tools/unityscene.py; echo "apk14:$APK14 apk15:$APK15 w15:${WORLD15:-1} apk16:$APK16 apk17:$APK17 apk18:$APK18 apk19:$APK19 apk110:$APK110 apk111:$APK111") | sha1sum | cut -c1-16)
if [ -f data/.version ] && [ "$(cat data/.version)" = "$VER" ]; then
    echo "data/ esta al dia (el extractor no ha cambiado)"
else
    if [ -z "$APK" ] || [ ! -f "$APK" ]; then
        echo "Hay que regenerar data/ pero no encuentro el APK. Copialo a esta carpeta o pasalo: ./actualizar.sh archivo.apk"
        exit 1
    fi
    [ -f ~/venv-subway/bin/activate ] && source ~/venv-subway/bin/activate
    echo "El extractor ha cambiado (o es la primera vez): regenerando data/ desde $APK ..."
    OPTS=()
    if [ -n "$APK14" ] && [ -f "$APK14" ]; then echo "Con el contenido de la 1.4 de $APK14"; OPTS+=(--apk14 "$APK14"); fi
    # de la 1.5.2: las tablas, Elf Tricky y su mapa (el principal; WORLD15=0 deja solo el clasico)
    if [ -n "$APK15" ] && [ -f "$APK15" ]; then echo "Con el contenido de la 1.5.2 de $APK15"; OPTS+=(--apk15 "$APK15"); fi
    # la 1.6: Tony y sus tablas (en todos los mapas) y su mapa de Nueva York en data/w16 (del 3 al 29 de enero)
    if [ -n "$APK16" ] && [ -f "$APK16" ]; then echo "Con el contenido de la 1.6 de $APK16"; OPTS+=(--apk16 "$APK16"); fi
    # la 1.7: Carmen, sus tablas y su mapa de Rio en data/w17 (del 30 de enero al 27 de febrero)
    if [ -n "$APK17" ] && [ -f "$APK17" ]; then echo "Con la 1.7 de $APK17 (en pruebas)"; OPTS+=(--apk17 "$APK17"); fi
    # la 1.8: Roberto, sus tablas, la busqueda de huevos (en todos los mapas) y su mapa de Roma en data/w18 (del 28 de
    # febrero al 3 de abril)
    if [ -n "$APK18" ] && [ -f "$APK18" ]; then echo "Con la 1.8 de $APK18"; OPTS+=(--apk18 "$APK18"); fi
    # la 1.9: Kim, sus tablas y su mapa de Sidney en data/w19 (del 4 de abril al 1 de mayo)
    if [ -n "$APK19" ] && [ -f "$APK19" ]; then echo "Con la 1.9 de $APK19"; OPTS+=(--apk19 "$APK19"); fi
    # la 1.10: Harumi, sus tablas, la caza semanal de munecas y su mapa de Tokio en data/w110 (del 2 al 29 de mayo)
    if [ -n "$APK110" ] && [ -f "$APK110" ]; then echo "Con la 1.10 de $APK110"; OPTS+=(--apk110 "$APK110"); fi
    # la 1.11: Nick, Hotrod, Flamingo, la caza de Miami y su mapa en data/w111 (del 30 de mayo al 27 de junio)
    if [ -n "$APK111" ] && [ -f "$APK111" ]; then echo "Con la 1.11 de $APK111"; OPTS+=(--apk111 "$APK111"); fi
    export WORLD15=${WORLD15:-1}
    python3 tools/extract.py "$APK" data/ "${OPTS[@]}"
    echo "$VER" > data/.version
fi

echo "== 3/3 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
./build_dist.sh
echo
if [ -f data/w15/world.bin ]; then echo "Mapa de la 1.5.2: incluido (el principal; en Halloween se juega el clasico)"; fi
if [ -f data/w111/world.bin ]; then echo "Mapa de la 1.11 (Miami): incluido (del 30 de mayo al 27 de junio, con su caza semanal; WORLD111=1 lo deja siempre)"; fi
if [ -f data/w110/world.bin ]; then echo "Mapa de la 1.10 (Tokio): incluido (del 2 al 29 de mayo, con la caza semanal; WORLD110=1 lo deja siempre)"; fi
if [ -f data/w19/world.bin ]; then echo "Mapa de la 1.9 (Sidney): incluido (del 4 de abril al 1 de mayo; WORLD19=1 lo deja siempre)"; fi
if [ -f data/w18/world.bin ]; then echo "Mapa de la 1.8 (Roma): incluido (del 28 de febrero al 3 de abril; WORLD18=1 lo deja siempre)"; fi
if [ -f data/w17/world.bin ]; then echo "Mapa de la 1.7 (Rio): incluido (del 30 de enero al 27 de febrero; WORLD17=1 lo deja siempre)"; fi
if [ -f data/w16/world.bin ]; then echo "Contenido de la 1.6: incluido (Tony y Liberty del 3 al 29 de enero, con su mapa; WORLD16=1 deja su mapa siempre)"; fi
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

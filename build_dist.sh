#!/bin/bash
# Arma dist/PSP/GAME/SUBWAY con el EBOOT y (si existe) la carpeta data/ generada con tools/extract.py
set -e
cd "$(dirname "$0")"
make
rm -rf dist
mkdir -p dist/PSP/GAME/SUBWAY
cp EBOOT.PBP dist/PSP/GAME/SUBWAY/
if [ -d data ]; then cp -r data dist/PSP/GAME/SUBWAY/; rm -rf dist/PSP/GAME/SUBWAY/data/wav dist/PSP/GAME/SUBWAY/data/meshes.txt; fi
echo "Listo: dist/PSP/GAME/SUBWAY (copiala a la Memory Stick en ms0:/PSP/GAME/)"

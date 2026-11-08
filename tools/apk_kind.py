"""Dice que APK de Subway Surfers es, mirando dentro (no el nombre del archivo):
  15  -> la 1.5.x (la de Navidad de 2012: trae las tablas, HoverboardManager)
  14  -> la 1.4 clasica (la de Halloween de 2012: trae ThemeAssets)
  10  -> otra version de Unity 3.5 con los datos del juego (la base: la 1.0)
  ?   -> no es un APK del juego o no se puede leer
Uso: python3 tools/apk_kind.py archivo.apk"""
import sys
import zipfile

try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        dll = z.read('assets/bin/Data/Managed/Assembly-CSharp.dll')
    print('15' if b'HoverboardManager' in dll else '14' if b'ThemeAssets' in dll else
          '10' if b'CharacterModels' in dll else '?')
except Exception:
    print('?')

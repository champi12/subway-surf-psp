"""Dice que APK de Subway Surfers es, mirando dentro (no el nombre del archivo):
  18  -> la 1.8 (Roma: trae la busqueda de huevos, EggHuntManager)
  17  -> la 1.7 (Rio: trae la tabla de velocidad, speedboard)
  16  -> la 1.6 (trae personajes y tablas con fecha de caducidad: CharacterExpirationDate)
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
    print('18' if b'EggHuntManager' in dll else '17' if b'speedboard' in dll else '16' if b'CharacterExpirationDate' in dll else '15' if b'HoverboardManager' in dll else '14' if b'ThemeAssets' in dll else
          '10' if b'CharacterModels' in dll else '?')
except Exception:
    print('?')

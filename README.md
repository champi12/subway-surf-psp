# Subway Surfers para PSP

Port nativo para PSP de **Subway Surfers 1.0** (Android, Kiloo/SYBO, 2012). No es un juego "parecido": el código
del juego original (C# de Unity, `Assembly-CSharp.dll`) se ha decompilado y **traducido a C** clase por clase, y
los modelos, texturas, animaciones, sonidos y la estructura de los tramos de vía se **extraen del propio APK**.

> El repositorio **no contiene ningún recurso del juego**. Cada uno genera la carpeta `data/` con su propia copia
> del APK usando `tools/extract.py`.

## Qué se ha portado

| Original (Unity/C#) | Port (C) | Notas |
|---|---|---|
| `Track`, `TrackChunk`, `TrackChunkCollection` | `src/scene.c` | Los 52 tramos originales con sus probabilidades y rangos de distancia (`zMinimum`/`zMaximum`) |
| `Randomizer`, `RandomizerTarget`, `Selector`, `Group`, `RandomizeOffset`, `Mirror` | `src/scene.c` | Misma selección aleatoria de obstáculos y monedas que el original |
| `SpawnPointManager`, `Upgrades` | `src/spawn.c` | Mismas probabilidades y distancias de aparición de power-ups |
| `Character`, `Running`, `Game` | `src/game.c` | Carriles, salto, rodar, gravedad (200), altura de salto (20/40), tropiezos, choques (impacto X/Y/Z igual que el original) |
| `CharacterController` / triggers de PhysX | `src/physics.c` | Cápsula contra cajas orientadas (rampas), escalón de 5, rayos hacia abajo |
| `FollowingGuard` | `src/game.c` | Inspector y perro: persecución, salto retrasado, captura |
| `Jetpack`, `Hoverboard`, `SuperSneakers`, `CoinMagnet`, `DoubleScoreMultiplier` | `src/game.c` | Duraciones de `Upgrades.cs` |
| `MovingTrain`, `MovingCoin`, `CoinLine`, `CoinJumpCurve`, `InAirCoinsManager` | `src/scene.c`, `src/game.c` | Trenes en marcha con el mismo truco de collider del original |
| `GameStats` | `src/game.c` | Puntos por metro × multiplicador, monedas, bonus de power-ups |
| `RunnerAnimPlayer`, `CharacterPickupParticles` | `src/game.c` | Sonidos de pasos por fotograma y escala pentatónica de las monedas |
| Componente `Animation` de Unity | `src/anim.c` | `CrossFade`, `PlayQueued`, capas, pesos y *mixing transforms* |
| Shader `Custom/Distorted/*` (mundo curvado) | `src/render.c` | La misma fórmula del shader GLSL, calculada por vértice en la CPU |
| NGUI (fuentes Lilita/Titan, atlas) | `src/hud.c` | Marcador con las fuentes originales |
| Audio (`So`, `AudioStateLoop`) | `src/audio.c` | Efectos con tono/volumen aleatorio como el original; música MP3 por hardware o AT3 |

Los números (velocidad 110→220 en 180 s, cámara a (0, 33, −33), distancia del guardia 10–30, etc.) son los
**guardados en la escena del APK**, no los valores por defecto del código.

### Pendiente

* Misiones (el multiplicador sube con ellas), personajes desbloqueables y la palabra del día.
* Tutorial (el tramo existe; de momento se empieza como si ya se hubiera completado).
* Partículas (chispas de la tabla, polvo del jetpack).

## Cómo generar los datos

Necesitas Python 3 y:

    pip install -r tools/requirements.txt
    python3 tools/extract.py com.kiloo.subwaysurf_1.0.apk data/

Genera:

| Archivo | Contenido |
|---|---|
| `world.bin` | 6.900 objetos de la escena: tramos, obstáculos, colliders, monedas, personaje, cámara |
| `mesh.bin` | 100 modelos (≈130.000 triángulos), troceados en bloques de 64 unidades para descartar lo que no se ve |
| `tex.bin` | Texturas a 512×512 como máximo (la PSP no admite más): 16 bits para el escenario y los trenes, paleta de 256 colores para el resto |
| `anim.bin` | 95 animaciones muestreadas a 30 fps |
| `sfx.bin` | Efectos en PCM 16 bits 22.050 Hz |
| `ui.bin` | Atlas y fuentes de NGUI |
| `music/` | La música original (`Hr_music_newFunky.mp3`) |
| `wav/` | Todos los sonidos en WAV, por si se quieren convertir a AT3 |

### Música en AT3 (opcional)

El juego reproduce el MP3 original con el decodificador por hardware de la PSP, así que no hace falta convertir nada.
Si prefieres AT3, conviértelo (por ejemplo con *Sony ATRAC3 tool* o `at3tool`) y déjalo como
`data/music/Hr_music_newFunky.at3`: si existe, se usa en lugar del MP3. Para que repita sin corte, el AT3 debe
llevar punto de bucle (opción `-loop` de at3tool); si no, se vuelve a empezar al terminar.

## Cómo compilar

Toolchain [pspdev](https://github.com/pspdev/pspdev):

    export PSPDEV=<ruta>/pspdev; export PATH=$PSPDEV/bin:$PATH
    make                # genera EBOOT.PBP
    ./build_dist.sh     # arma dist/PSP/GAME/SUBWAY con el EBOOT y data/

## Instalar

En la Memory Stick: `ms0:/PSP/GAME/SUBWAY/EBOOT.PBP` y `ms0:/PSP/GAME/SUBWAY/data/` (sin `wav/`, que solo sirve
para convertir). Funciona en firmware personalizado (CFW/ARK/PRO) y en PPSSPP.

## Controles

El original se jugaba deslizando el dedo; en la PSP:

| PSP | Acción |
|---|---|
| ← / → (cruceta o stick) | Cambiar de carril |
| ↑ o ✕ | Saltar |
| ↓ o ○ | Rodar |
| □ o △ | Activar la tabla (hoverboard), equivale al doble toque |
| START | Pausa (al reanudar hay cuenta atrás "Starting in 3, 2, 1" como en el original) |
| L / R (primeros 5 s) | Usar *Headstart* 250 / 1000 |
| L + SELECT | Medidor de rendimiento |

En el menú, △ o □ abren la **tienda** con los precios de `Upgrades.cs`: hoverboards (300), Headstart 250 (400),
Headstart 1000 (2000), cajas misteriosas (500) y los 5 niveles de mejora del jetpack, las super zapatillas, el imán
y el multiplicador ×2 (500, 1500, 3000, 10000, 30000). Las cajas misteriosas recogidas en la carrera se abren al
terminar, con las probabilidades de `MysteryBox.Roll()`. La partida se guarda en `save.dat`.

## Rendimiento y pruebas sin PSP

Ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md): cómo medir en PPSSPP si el juego cabe en una PSP real (medidor
interno, configuración del emulador y la prueba automática `tools/autotest.sh`).

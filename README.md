# Subway Surfers para PSP

Port nativo para PSP de **Subway Surfers** clásico (Android, Kiloo/SYBO, 2012): la **1.0** con todo el contenido
hasta la **1.4** integrado. No es un juego "parecido": el código
del juego original (C# de Unity, `Assembly-CSharp.dll`) se ha decompilado y **traducido a C** clase por clase, y
los modelos, texturas, animaciones, sonidos y la estructura de los tramos de vía se **extraen del propio APK**.

> El repositorio **no contiene ningún recurso del juego**. Cada uno genera la carpeta `data/` con su propia copia
> del APK usando `tools/extract.py`.

> **Hecho con ayuda de IA.** Este port se ha desarrollado con la ayuda de **Claude** (Anthropic), un asistente de
> inteligencia artificial, que ha escrito gran parte del código junto al autor. Si no estás de acuerdo con el uso de
> estas herramientas, este proyecto no es para ti.
>
> *Made with AI assistance: this port was developed together with Claude (Anthropic), an AI assistant that wrote
> much of the code alongside the author. If you disagree with the use of these tools, this project is not for you.*

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
| `CharacterModels`, `CharacterScreenModelHandling` | `src/game.c`, `src/hud.c` | Los 15 personajes base: Jake gratis, 4 por fichas de las cajas misteriosas y 10 por monedas (5 de ellos de la 1.4) |
| `Trophies`, `MysteryBox` | `src/game.c`, `src/hud.c` | Los 8 trofeos de las cajas misteriosas y su pantalla |
| `DailyWord`, `DailyLetterPickup` | `src/game.c`, `src/scene.c` | Palabra del día: letras en la vía, siempre la siguiente que falta; completarla da una caja misteriosa |
| `RenderSettings` (niebla) | `src/render.c` | Niebla lineal de 200 a 700 del original, por hardware; lo que queda detrás no se dibuja |
| `TutorialEvent`, `Character.SetBackToCheckPoint` | `src/game.c` | Tramo de tutorial en la primera partida, con la flecha, los mensajes y la vuelta al punto de control al chocar |
| `GameStats` | `src/game.c` | Puntos por metro × multiplicador, monedas, bonus de power-ups |
| `Missions`, `PlayerInfo` | `src/missions.c` | Los 29 grupos de 3 misiones del original; cada grupo completado sube el multiplicador (hasta ×30) |
| `RunnerAnimPlayer`, `CharacterPickupParticles` | `src/game.c` | Sonidos de pasos por fotograma y escala pentatónica de las monedas |
| Componente `Animation` de Unity | `src/anim.c` | `CrossFade`, `PlayQueued`, capas, pesos y *mixing transforms* |
| Shader `Custom/Distorted/*` (mundo curvado) | `src/render.c` | La misma fórmula del shader GLSL, calculada por vértice en la CPU |
| NGUI (fuentes Lilita/Titan, atlas) | `src/hud.c` | Marcador, menú, tienda y misiones con las fuentes y sprites originales (9-slice) |
| Audio (`So`, `AudioStateLoop`) | `src/audio.c` | Efectos con tono/volumen aleatorio como el original; música MP3 por hardware o AT3 |

Los números (velocidad 110→220 en 180 s, cámara a (0, 33, −33), distancia del guardia 10–30, etc.) son los
**guardados en la escena del APK**, no los valores por defecto del código.

### Pendiente

* Las misiones de ganar a amigos necesitan conexión: se dan por completadas.
* Partículas (chispas de la tabla, polvo del jetpack): no se portan para no perder fotogramas.

## Cómo generar los datos

Necesitas Python 3 y:

    pip install -r tools/requirements.txt
    python3 tools/extract.py com.kiloo.subwaysurf_1.0.apk data/ [--apk14 subway-surfers-1.4.apk]

Genera:

| Archivo | Contenido |
|---|---|
| `world.bin` | 6.900 objetos de la escena: tramos, obstáculos, colliders, monedas, personaje, cámara |
| `mesh.bin` | 100 modelos (≈130.000 triángulos), troceados en bloques de 64 unidades para descartar lo que no se ve |
| `tex.bin` | Texturas a 512×512 como máximo (la PSP no admite más): 16 bits para el escenario y los trenes, paleta de 256 colores para el resto |
| `anim.bin` | 95 animaciones muestreadas a 30 fps |
| `sfx.bin` | Efectos en PCM 16 bits 22.050 Hz |
| `ui.bin` | Atlas y fuentes de NGUI, más las fuentes pre-escaladas a los tamaños exactos de la interfaz (texto nítido en la PSP) |
| `music/` | La música original (`Hr_music_newFunky.mp3`) |
| `wav/` | Todos los sonidos en WAV, por si se quieren convertir a AT3 |
| `hw_tex.bin`, `hw_mesh.bin`, `chars14.bin` | Solo con `--apk14`: el contenido de la 1.4 (las mallas de los personajes nuevos se leen al verlos) |
| `ICON0.PNG`, `PIC1.PNG` | Icono y fondo del menú de la PSP, recortados de la pantalla de carga del APK (van dentro del EBOOT) |

### Contenido de la 1.4

Si se pasa también el APK de la **1.4**, el juego integra todo lo que trae hasta esa versión, como los personajes
nuevos. `actualizar.sh` lo usa solo si hay en la carpeta un APK con "1.4" o "1-4" en el nombre. Sin él, el juego
funciona igual con el contenido de la 1.0.

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

La compilación normal es la versión pública: no escribe nada en la Memory Stick salvo la partida. `make DEVLOG=1`
(o `DEVLOG=1 ./build_dist.sh`) genera la versión de pruebas, con el registro `devlog.txt` y el capturador de cuelgues
`crash.txt` (ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md)).

### Actualizar y recompilar después de cada cambio

    ./actualizar.sh subway.apk "/mnt/c/Users/<usuario>/Desktop"

Baja los cambios (`git pull`), vuelve a generar `data/` **solo si cambió el extractor**, compila **solo los archivos
modificados** y copia la carpeta `PSP` al destino que le indiques (opcional). La primera vez regenera `data/`.

## Instalar

En la Memory Stick: `ms0:/PSP/GAME/SUBWAY/EBOOT.PBP` y `ms0:/PSP/GAME/SUBWAY/data/` (sin `wav/`, que solo sirve
para convertir). La versión de pruebas lleva además `exhandler.prx` y `exloader.prx` (capturador de cuelgues, ver
[docs/RENDIMIENTO.md](docs/RENDIMIENTO.md)). Funciona en firmware personalizado (CFW/ARK/PRO) y en PPSSPP.

## Interfaz

Con el APK de la 1.4, los menús son los de la **1.4**: el menú principal con los botones de la tienda y los
personajes, la barra de abajo de la tienda y los personajes (Menú / Mejoras / Personajes), las tarjetas de compra de
la tienda, la pantalla de personajes con el modelo grande, el botón de compra y el carrusel, el fin de carrera con la
pizarra de puntos y el botón verde de jugar, y la pausa con la palabra del día y las misiones; todo con el fondo
desenfocado del original y adaptado a la pantalla apaisada de la PSP. Sin el APK de la 1.4 se usan los sprites de la
1.0 con el escenario oscurecido detrás.

No gasta más memoria: el atlas de la 1.4 (1024×512, más de lo que admite la PSP) se reempaqueta en una hoja de
512×512 solo con los sprites que se usan, igual que la de la 1.0; las fuentes pasan a 8 bits (la mitad de memoria);
y el fondo (64 KB) solo se carga mientras se ve una de esas pantallas. En esas pantallas tampoco se dibuja el
escenario 3D. Con todo desbloqueado el pico medido baja de 15,8 a 15,4 MB.

## Controles

El original se jugaba deslizando el dedo; en la PSP:

| PSP | Acción |
|---|---|
| ← / → (cruceta o stick) | Cambiar de carril |
| ↑ o ✕ | Saltar |
| ↓ o ○ | Rodar |
| □ o △ | Activar la tabla (hoverboard), equivale al doble toque |
| START | Pausa: ✕ o START seguir (con la cuenta atrás "Starting in 3, 2, 1" del original), ○ volver al menú |
| L / R (primeros 5 s) | Usar *Headstart* 250 / 1000 |
| △ (en el menú) | Ver las misiones |
| L / R (en el menú) | Personajes / tienda (dentro de una, L y R pasan a la otra; ○ vuelve al menú) |
| ✕ / ○ / R (fin de carrera) | Jugar otra vez / menú / tienda |
| □ (en el menú) | Trofeos |
| SELECT | Medidor: resumen → memoria y VRAM → gráfica de tiempos → oculto |

**Idioma:** el juego usa el idioma de la consola (*Ajustes → Ajustes del sistema → Idioma*): en español si la PSP
está en español y en inglés con cualquier otro idioma, como el original. En inglés, las misiones y la tienda usan
los textos originales de `Missions.cs` y `Upgrades.cs`.

En el menú, L abre los **personajes**: Jake viene desbloqueado; Tricky, Fresh, Spike y Yutani se consiguen
juntando sus fichas (3, 50, 200 y 500), que salen en las cajas misteriosas, y Lucy, Ninja, Frank, King y Frizzy se
compran con monedas (7.000, 20.000, 40.000, 80.000 y 150.000), como en `CharacterModels.cs`. Con el APK de la 1.4
se añaden los otros 5 personajes base: Tagbot (12.000), Tasha (30.000), Zoe (120.000), Brody (350.000) y Prince K
(980.000), ordenados por precio en la lista. Para no gastar memoria, la malla (~140 KB) y la textura (~260 KB) de
cada uno solo se cargan mientras se ve en la pantalla de personajes o si es el elegido; con todo desbloqueado el
pico medido es de 15,4 MB de los 18,7 MB de una PSP-1000.

La primera partida es el **tutorial** del original (saltar, rodar, cambiar de vía y la tabla), con los mensajes
adaptados a los botones de la PSP; al chocar se vuelve al último punto de control en lugar de morir. Las partidas
guardadas con versiones anteriores lo dan por hecho.

La **palabra del día** sale en el menú. Como en el original, sus letras aparecen en la vía como un power-up más,
siempre la siguiente que falta, y al completarla se gana una caja misteriosa (y cuenta para las misiones de letras y
de retos diarios). El original recibía la palabra de su servidor; aquí sale de una lista según la fecha de la
consola y cambia a medianoche.

Con □ se ven los **trofeos** (`Trophies.cs`): 8 objetos de oro (gema, lingote, reloj, cadena, calavera, auriculares,
disco y casete) que salen en las cajas misteriosas con un 5 % de probabilidad, uno que aún no se tenga; con los 8,
esa casilla da 100 monedas como en el original.

En el menú, R abre la **tienda** con los precios de `Upgrades.cs`: hoverboards (300), Headstart 250 (400),
Headstart 1000 (2000), cajas misteriosas (500) y los 5 niveles de mejora del jetpack, las super zapatillas, el imán
y el multiplicador ×2 (500, 1500, 3000, 10000, 30000), y saltar una misión (1500 + 100 por grupo completado).
Al terminar la carrera las monedas suman 2 × el multiplicador de misiones a la puntuación final. Las cajas misteriosas recogidas en la carrera se abren al
terminar, con las probabilidades de `MysteryBox.Roll()`. La partida se guarda en `save.dat`.

## Rendimiento y pruebas sin PSP

Ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md): el medidor de SELECT, el registro `devlog.txt` que la versión de pruebas
escribe en la Memory Stick, qué datos del emulador valen para la consola real, cómo configurar PPSSPP y la prueba automática
`tools/autotest.sh`.

## Contribuidores

| | Papel |
|---|---|
| **champi12** | Autor del proyecto: idea, pruebas en la consola, coordinación con los testers |
| **Claude** (Anthropic) | Asistente de IA: decompilación y traducción del código, motor gráfico, conversor de recursos y depuración |
| Los testers | Pruebas en PSP real, devlogs, vídeos e informes de fallos |

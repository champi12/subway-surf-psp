# Subway Surfers para PSP

Port nativo para PSP de **Subway Surfers 1.0** (Android, Kiloo/SYBO, 2012). No es un juego "parecido": el código
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
| `CharacterModels`, `CharacterScreenModelHandling` | `src/game.c`, `src/hud.c` | Los 10 personajes: Jake gratis, 4 por fichas de las cajas misteriosas y 5 por monedas |
| `Trophies`, `MysteryBox` | `src/game.c`, `src/hud.c` | Los 8 trofeos de las cajas misteriosas y su pantalla |
| `ThemeAssets`, `HouseKeeper` (1.4) | `src/data.c`, `src/game.c` | Tema de Halloween de la 1.4 clásica y Zombie Jake (ver abajo) |
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
* Partículas (chispas de la tabla, polvo del jetpack) y brillos de las calabazas de Halloween: no se portan para
  no perder fotogramas.

## Cómo generar los datos

Necesitas Python 3 y:

    pip install -r tools/requirements.txt
    python3 tools/extract.py com.kiloo.subwaysurf_1.0.apk data/ [--halloween subway-surfers-1.4.apk]

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
| `hw_tex.bin`, `hw_mesh.bin` | Solo con `--halloween`: el tema de Halloween y Zombie Jake, sacados del APK de la 1.4 |

### Halloween (1.4 clásica)

Si se pasa también el APK de la **1.4** (la actualización de Halloween de 2012), el juego usa su tema del **24 de
octubre al 4 de noviembre** según el reloj de la consola, sin botón: el escenario, los trenes y el tren del logo
pasan a la versión de Halloween, el inspector y el perro son zombis y la niebla se vuelve verde, como en la 1.4
(que lo quitaba el 5 de noviembre). En esas fechas se puede comprar **Zombie Jake** (95.000 monedas); quien lo
compra lo conserva cuando acaba el evento, y si no se compra desaparece de la lista hasta el año siguiente.

Solo se carga una versión de cada textura (la normal o la de Halloween, que ocupan lo mismo), así que la memoria
es la misma; fuera de las fechas ni siquiera se leen las mallas de Halloween. `actualizar.sh` usa el APK de la 1.4
si hay en la carpeta uno con "1.4" o "1-4" en el nombre. Los brillos de las calabazas no se han portado.

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

`make DEVLOG=0` quita el registro `devlog.txt` (ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md)).

### Actualizar y recompilar después de cada cambio

    ./actualizar.sh subway.apk "/mnt/c/Users/<usuario>/Desktop"

Baja los cambios (`git pull`), vuelve a generar `data/` **solo si cambió el extractor**, compila **solo los archivos
modificados** y copia la carpeta `PSP` al destino que le indiques (opcional). La primera vez regenera `data/`.

## Instalar

En la Memory Stick: `ms0:/PSP/GAME/SUBWAY/EBOOT.PBP`, `ms0:/PSP/GAME/SUBWAY/exhandler.prx` y `exloader.prx` (capturador de cuelgues, ver
[docs/RENDIMIENTO.md](docs/RENDIMIENTO.md)) y `ms0:/PSP/GAME/SUBWAY/data/` (sin `wav/`, que solo sirve
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
| △ (en el menú) | Ver las misiones |
| L / R (en el menú) | Personajes / tienda (dentro de una, L y R pasan a la otra) |
| □ (en el menú) | Trofeos |
| SELECT | Medidor: resumen → memoria y VRAM → gráfica de tiempos → oculto |

**Idioma:** el juego usa el idioma de la consola (*Ajustes → Ajustes del sistema → Idioma*): en español si la PSP
está en español y en inglés con cualquier otro idioma, como el original. En inglés, las misiones y la tienda usan
los textos originales de `Missions.cs` y `Upgrades.cs`.

En el menú, L abre los **personajes**: Jake viene desbloqueado; Tricky, Fresh, Spike y Yutani se consiguen
juntando sus fichas (3, 50, 200 y 500), que salen en las cajas misteriosas, y Lucy, Ninja, Frank, King y Frizzy se
compran con monedas (7.000, 20.000, 40.000, 80.000 y 150.000), como en `CharacterModels.cs`.

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

Ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md): el medidor de SELECT, el registro `devlog.txt` que el juego escribe en
la Memory Stick, qué datos del emulador valen para la consola real, cómo configurar PPSSPP y la prueba automática
`tools/autotest.sh`.

## Contribuidores

| | Papel |
|---|---|
| **champi12** | Autor del proyecto: idea, pruebas en la consola, coordinación con los testers |
| **Claude** (Anthropic) | Asistente de IA: decompilación y traducción del código, motor gráfico, conversor de recursos y depuración |
| Los testers | Pruebas en PSP real, devlogs, vídeos e informes de fallos |

# Estado del proyecto y plan (para empezar una conversación nueva)

Resumen de lo hecho, lo que está a medias y lo que queda, para retomar el trabajo sin el historial del chat.
Última actualización: 11 de noviembre de 2026 (1.5.2 y 1.6 terminadas).

## 1. Qué es

Port nativo a PSP de Subway Surfers clásico (Android, Unity). El código C# del juego (`Assembly-CSharp.dll`) se
decompiló y se tradujo a C; modelos, texturas, animaciones, sonidos y tramos se **extraen del APK de cada usuario**
con `tools/extract.py`. El repositorio no lleva ningún recurso del juego.

* Repositorio: `github.com/champi12/subway-surf-psp`, rama de trabajo `claude/charming-faraday-j4xh5e`.
* Base: **1.0** + contenido de la **1.4** (Halloween, personajes, menús) + de la **1.5.2** (tablas, Elf Tricky,
  pantallas, evento de Navidad y su mapa) + de la **1.6** (Tony, tablas Miami, Monster, Skull Fire y Liberty, mapa de
  Nueva York en `data/w16`).
* Hacen falta los APK (1.0, 1.4.0, 1.5.2 y, si se quiere lo de la 1.6, 1.6.0) en la carpeta del proyecto;
  `actualizar.sh` los reconoce por dentro.

## 2. Reglas de trabajo acordadas

* Hablar siempre en **español**.
* **Como el original**: ante cualquier duda, mirar el C# decompilado y hacerlo igual. Si un fallo también está en el
  original, se deja; si una versión posterior lo arregló, se usa ese arreglo (p. ej. el techo de la cámara en los
  túneles).
* **No romper lo que ya funciona.** Los cambios arriesgados de dibujo solo en el mapa de la 1.5.2 (`r_light`); al
  mapa clásico solo se pasa lo que ya está comprobado en la consola.
* **30 FPS estables en la consola real** (PSP-1000/2000/3000). El emulador no sirve para medir tiempos de la consola:
  los datos buenos son los del devlog de la consola.
* Nunca subir recursos del juego (`data/`, APK, capturas). No crear pull requests salvo que se pidan.
* Commits con `git -c user.name="champi12" -c user.email="joanchampi11@gmail.com" commit`, mensaje en español
  explicando el porqué, terminado con las líneas de atribución que indique la sesión. Push a la rama de trabajo.
* Antes de subir: compilar las cuatro variantes (`make`, `make WORLD15=1`, `make WORLD15=1 DEVLOG=1 NOANALOG=1`,
  `make DEVLOG=1`) y probar en el emulador estricto.

## 3. Cómo se compila y se prueba

### El usuario (en su PC, WSL)

    cd ~/subway-surf-psp
    git pull
    DEVLOG=1 NOANALOG=1 ./actualizar.sh subway.apk "/mnt/c/Users/Joan jimenez/Desktop"

* `actualizar.sh`: baja cambios, regenera `data/` solo si cambió el extractor, compila y copia `PSP/` al escritorio.
* El **mapa de la 1.5.2** es el principal (en Halloween se juega el clásico con su tema). `WORLD15=0` deja solo
  el mapa clásico.
* **Contenido por fecha, no por mapa:** los personajes y tablas de temporada salen en cualquier mapa, pero solo en
  sus fechas (si no se compran, desaparecen hasta el año siguiente): Zombie Jake del 24 de octubre al 4 de noviembre,
  Elf Tricky del 18 de diciembre al 1 de enero, Tony y Liberty del 3 al 29 de enero (en la 1.6 empezaban el
  2 de diciembre; aquí tras la Navidad para no pisarla). Los APK no traen fecha de inicio (lo encendía el servidor):
  solo el fin (Halloween 5 nov 00:00, Navidad 2 ene 00:00, Tony/Liberty 1 feb 00:00); el inicio es una semana antes
  de la fiesta (31 de octubre, 25 de diciembre). El mapa de la 1.6 sale también solo en su temporada;
  `WORLD16=1` lo deja siempre (pruebas). La Navidad podría tener más adelante un mapa propio.
* `DEVLOG=1`: versión de pruebas (devlog.txt, crash.txt, medidor con SELECT, capturas con SELECT+R).
* `NOANALOG=1`: sin el stick analógico (la consola del usuario lo tiene estropeado).

### En una sesión de Claude (nube)

El kit de PSP (`pspdev`) y el emulador sin pantalla (`PPSSPPHeadless`, compilado con `tools/ppsspp_estricto.patch`)
se instalaron a mano en una carpeta temporal y **no se conservan** entre sesiones: en una sesión nueva hay que
volver a instalarlos (toolchain de pspdev y PPSSPP compilado como headless con ese parche). Luego:

    export PSPDEV=<ruta>/pspdev PATH=$PSPDEV/bin:$PATH PPSSPP_HEADLESS=<ruta>/PPSSPPHeadless
    WORLD15=1 python3 tools/extract.py <apk 1.0> <datos> --apk14 <apk 1.4> --apk15 <apk 1.5.2>
    PPSSPP_STRICT_FPU=1 PPSSPP_STRICT_ALIGN=1 PPSSPP_STRICT_MEM=1 \
      XFLAGS='-DWORLD15=1 -DAT_NOTUT=1 -DAT_NOPERF=1' SHOT=150 tools/autotest.sh 60 <salida> <datos>

`autotest.sh` compila con `AUTOTEST=1 DEVLOG=1`, juega solo y deja `devlog.txt`, `perf.log` y capturas
(`sheet.png`). Opciones útiles en `XFLAGS`: `AT_XMAS`, `AT_HALLOWEEN`, `AT_NOTUT`, `AT_CHUNK="nombre"` (forzar un
tramo), `AT_JET=N` / `AT_MAGNET=N` (jetpack o imán en el fotograma N), `AT_SLOWLOG=ms` (anota fotogramas lentos),
`AT_STAT1` (triángulos y dibujos por bloque), `AT_RANDHOLD=n` (forzar ambiente de la 1.5.2), `AT_ELF`, `AT_UI`,
`AT_NY` (temporada de Nueva York) con `AT_NYTEST` (compra Tony y recorre las tablas). Con la 1.6: `--apk16 <apk 1.6>`.

## 4. Mapa del código

| Archivo | Qué hace |
|---|---|
| `src/main.c` | Arranque, elección de mundo (clásico / `data/w15` según fecha y `WORLD15`), bucle, `dt`, capturas |
| `src/game.c` | Lógica del juego (Character, Running, Jetpack, power-ups, guardia, cámara, tienda, guardado) |
| `src/scene.c` | Tramos (TrackChunk), selectores, monedas, trenes y monedas en marcha, colocar/quitar tramos |
| `src/render.c` | Motor gráfico (GE): curvatura del mundo, recortes, mipmaps, VRAM, tres imágenes, cronómetro del GE |
| `src/hud.c` | Interfaz (menús de la 1.4/1.5.2, libreta, cajas misteriosas, marcador) |
| `src/data.c` | Carga de `data/` (mallas por bloques, texturas, temas Halloween/Navidad) |
| `src/physics.c`, `anim.c`, `audio.c`, `missions.c`, `spawn.c` | Física, animación, sonido, misiones, power-ups |
| `src/devlog.c` | Devlog: STAT, PERFIL, GE ms, DIBUJO, TIRON, REINICIO... |
| `tools/extract.py` | Conversor de los APK a `data/` (y `data/w15` con `WORLD15=1`) |
| `docs/RENDIMIENTO.md` | Cómo leer el medidor y el devlog |

**Interruptor `r_light`** (`render.c`, se pone en `main.c`): 1 en el mapa de la 1.5.2. Activa solo ahí lo
agresivo: recorte en la niebla (>90 %), monedas lejanas ligeras, sin bilineal a lo lejos, monedas y brillos por
lotes, prioridad de VRAM de la 1.5.2, tramos lejanos colocados por partes, nivel de mipmap de trozos grandes por su
mitad y el techo de cámara de la 1.5.2 en los túneles.

## 5. Lo que ya está hecho (resumen)

* Juego completo de la 1.0 con lo de la 1.4 y la 1.5.2: tablas (con sonidos y humo), Elf Tricky (Navidad, en el
  selector), pantallas de la 1.5.2, misiones y palabra del día por temporada, Halloween (1.4) y Navidad (1.5.2) por
  fecha de la consola, cajas misteriosas.
* **Mapa de la 1.5.2** (`WORLD15=1`): seis ambientes (RandomizerHold), Navidad con sus mallas propias.
* **Rendimiento (resultado en la consola):** el mapa de la 1.5.2 pasó de ~5 tirones por minuto a **0 tirones en
  carrera** en las últimas sesiones (puente, trenes en marcha). Claves:
  * texturas del escenario con paleta en la VRAM, monedas por lotes, distancia de dibujo fija como el original;
  * trozos del escenario de la 1.5.2 agrupados según sus vértices (de ~408 a ~300 dibujos por fotograma);
  * **tres imágenes** y cambio de imagen sin esperar al refresco (`SETBUF_NEXTFRAME`): un fotograma de 34 ms ya no
    baja a 20 FPS;
  * reinicio más ligero (solo se esconden los tramos usados), texturas de la pantalla de fin en segundo plano
    durante la caída, `save.dat` guardado en otro hilo.
* **Probado y descartado:** versiones ligeras (LOD) del escenario (huecos en los túneles) y tiras de triángulos
  (los modelos de la 1.5.2 no comparten vértices: 2,8 por triángulo, no compensa).
* **Mapa clásico:** recibió ya lo comprobado (tres imágenes, reinicio ligero, nivel de mipmap por la CPU: sin las
  líneas de los túneles y paredes rojas) y los trenes con paleta para que quepan en la VRAM con la tercera imagen.
* **Arreglos recientes de lógica (todos los mapas):** imán con las monedas de los trenes en marcha como el original
  (salen desde donde se ven); cámara con el techo de los túneles del original (1.4 en el clásico, 1.5.2 en el suyo).

## 6. Lo que está a medias

* **Puntos blancos en las paredes del mapa clásico** (solo en la consola, no salen en capturas ni en el emulador).
  Posibles causas: rendijas de un píxel entre piezas (ver "Los puntos blancos en el escenario" en
  `docs/RENDIMIENTO.md`) o el nivel de mipmap elegido por la CPU leyendo píxeles vecinos del atlas. Siguiente paso:
  saber si parpadean (rendijas) o están quietos (atlas) y en qué paredes.
* **Reinicio tras carreras largas:** "quitar tramos" llega a 24-30 ms tras ~2 minutos de carrera (muchos tramos
  usados). Idea: esconder cada tramo cuando sale de la vista durante la carrera, poco a poco.
* **Hecho:** el mapa de la 1.5.2 ya es el principal (`WORLD15=1` por defecto en `actualizar.sh`, `build_dist.sh` y
  `main.c`); en Halloween se usa el clásico.

## 6b. Lo de la 1.5.2 comparado con el port (clases nuevas frente a la 1.4)

* **Hecho:** tablas (`Hoverboard*`, `BoardScreen`, sonidos y humo), Elf Tricky, Navidad (mallas, texturas, cielo,
  nieve en la portada, `ChangelogThemePopup`), pantallas de la 1.5.2 (`NotebookScreen`, `GameOverScreen`,
  `IngameScreen`, `UpgradeScreen`, `TrophiesScreen`, `MysteryBoxPopup`, `MissionPopup`), mapa de la 1.5.2.
* **No aplica en PSP (necesitan internet o tienda):** `FriendsOnlineScreen`, `LeaderboardOnlineScreen`,
  `BragPopup`, `EarnCoinsScreen`, `PrivacyPolicy*`, `RestorePurchaseHelper`, `OfflineScreen`.
* **Hecho también:** ajustes (START: sonido y Navidad), lista de premios (cuadrado en la caja, la tienda y el fin),
  reto diario (cuadrado en la portada: palabra, tiempo, premio y racha de 5 días), y el repaso de la libreta
  (número de set y aviso de saltar misiones) y de las cajas ("Ver premios", monedas, "¡NUEVO TROFEO!").

## 7. Plan

1. (Hecho) Versión pública con el mapa de la 1.5.2 por defecto y todo lo de la 1.5.2.
1b. Más adelante: **selector de mapas** en los ajustes para elegir el mapa clásico y el de Halloween como mapas
   independientes (hoy el clásico solo sale en Halloween).
2. Puntos blancos del mapa clásico.
3. Reinicio tras carreras largas.
4. 1.6 terminada: Tony, las 4 tablas (pegatinas NUEVA/LÍMITE y número en ME y Tablas), su mapa por temporada
   (1 tirón en 482 s en la consola), el inspector y el perro de Nueva York en su mapa (mallas y textura de la 1.6
   con el esqueleto de siempre), el aviso "¡Bienvenido a Nueva York!", las misiones "X puntos sin..." con la regla
   de la 1.6 (cada moneda, salto, voltereta o power-up pone la racha a cero), la cuenta de monedas a puntos del fin
   de partida (CountUpCoins) y el aviso "¿Sin tablas?" (3 gratis). Lo demás de la 1.6 es de la interfaz táctil o
   del servidor. El guardia de las demás versiones: 1.0 = 1.4 = 1.5.2 (verde); Halloween (zombi) y Navidad (Papá
   Noel) ya tenían sus mallas y texturas. Queda el fallo de "letras con líneas negras" en el mapa de la 1.4 (falta
   captura).
4b. 1.7 (Río): su mapa en data/w17 (`--apk17`) del 30 de enero al 27 de febrero (tras Nueva York; `WORLD17=1` lo deja
   siempre), sin texturas reducidas (como el original: en Río cambiaban el color de las paredes a lo lejos), con su
   inspector y la interfaz de la 1.5.2 (la de la 1.7 es de Unity 4 y no se lee entera). Carmen y Toucan por fecha
   (temporada de Río), Lumberjack y Daredevil (85000; acelera y abre la cámara), Score Booster
   (+5 al multiplicador una carrera: L al empezar, en la tienda en lugar del headstart pequeño, 3 al empezar),
   aviso de bienvenida de Río y las animaciones nuevas del inspector y el perro (esquivar y rodar) en todos los
   mapas, y las tablas de premios de las cajas de la 1.7 (con el Score Booster). Volver del modo reposo ya no
   congela la consola (se reabren los archivos de la Memory Stick).
4c. 1.8 (Roma): su mapa en data/w18 (`--apk18`) del 28 de febrero al 3 de abril (tras Río; `WORLD18=1` lo deja
   siempre), con su inspector. Por fecha y en todos los mapas: Roberto (95000), Kick-Off (50000), Scoot (35000) y la
   búsqueda de huevos de Pascua (EggHuntManager): sale un huevo como mucho cada 80 s de carrera (en Roma en sus
   SpawnPointEgg; en los demás mapas en los puntos de power-ups donde no ha salido ninguno), contador "N/100" en la
   carrera y con 100 se gana Chicky (y su caja al acabar). Los huevos son de la temporada: cada año se empieza de
   cero. El huevo de los demás mapas lleva solo su trozo de textura (64x128), no la hoja de la 1.8 entera. También en
   todos los mapas: el inspector rueda solo al pasar el personaje por debajo de una barrera (OnRollGuard de la 1.8)
   y las animaciones del jetpack de la 1.8 (al cambiar de carril gira, BarrelRoll, o se inclina, changeLane, al
   azar; ~80 KB). La regla de la 1.7 del Score Booster seguro en la cuarta caja (y otro tres cajas después), que la 1.8
   quitó, queda como cosa de la temporada de Río: solo funciona del 30 de enero al 27 de febrero.
   Coste por mapa ~110 KB de datos; 30 FPS en el clásico, el de la 1.5.2 y el de Roma.
   Roma va sin mipmaps como Río (había errores gráficos a lo lejos; además ahorra ~0,6 MB de RAM).
4e. Fechas de las ciudades como las actualizaciones del original: Nueva York (1.6) del 3 al 29 de enero, Río (1.7)
   del 30 de enero al 27 de febrero, Roma (1.8) del 28 de febrero al 3 de abril (Sídney 1.9 y Tokio 1.10 irían del 4
   de abril al 1 de mayo y del 2 al 29 de mayo, si se añaden). Más de la 1.8 en todos los mapas: sexto nivel de las
   mejoras (60000 monedas; jetpack 25 s y el resto 30 s), el inspector baja en seco cuando el personaje rueda
   (OnRollNoAnimation), las monedas de la carrera suben de 0 en el fin de carrera y los puntos de huevos se ordenan
   con los de power-ups. Revisadas las clases de la 1.5 a la 1.8: lo demás que cambia es interfaz online, avisos o
   cambios de sintaxis de Unity 4.
4f. Pantallas de carga: cada mapa de ciudad (Nueva York, Río, Roma) con la imagen de inicio de su APK (su ciudad),
   con la misma disposición que la de la 1.5.2; el de la 1.5.2 (con su Navidad), Halloween y el clásico con la de la
   1.5.2. Como el original, primero sale la imagen sola y después la libreta con el consejo y la barra.
   Power-ups: el port usaba las probabilidades de la 1.0; desde la 1.4 son 2x 32, imán 32, zapatillas 22, jetpack 14
   y caja 20, y las letras de la palabra del día salen en cada punto mientras quede alguna (CheckLetterSpawnRate).
   PRUEBA: Bouncer, Lowrider y Daredevil desbloqueadas (PRUEBA_TABLAS=1 en build_dist.sh); quitar al terminar.
4g. 1.9 (Sídney): su mapa en data/w19 (`--apk19`) del 4 de abril al 1 de mayo (`WORLD19=1` lo deja siempre), sin
   mipmaps, con su inspector y su perro (en la 1.9 las mallas se llaman como en la 1.0) y su pantalla de carga. Por
   fecha y en todos los mapas: Kim (95000) y Outback (50000, "Especial de Sídney"); Great White (20000) siempre.
   Probabilidades de los power-ups de la 1.9 (2x 29, imán 29, zapatillas 26, jetpack 14, caja 12). La 1.9 no tiene
   búsqueda de huevos (la nuestra solo va en Roma). Medido en el emulador (120 s, misma semilla): 1.5.2 312 llamadas
   de media y 17084 triángulos; Nueva York 311 / 19157; Río 340 / 19008; Roma 219 / 18349; Sídney 226 / 18059. Save
   Me (llaves), de momento solo en el mapa de Sídney: la primera vez sale el aviso de las llaves (ShowKeysPopup) y
   desde entonces las llaves salen en la vía (probabilidad 2) y en las cajas (tablas de premios de la 1.9 con llaves;
   la primera caja y la primera super dan una). Al chocar, 3 s para revivir con X (1 llave, luego 2, 4...; vuelve a 1
   cada carrera) u O para terminar; al revivir se quitan los obstáculos de delante (250 por la velocidad), el
   inspector se va y el personaje sale de un salto, con el sonido de la 1.9. Sin llaves suficientes solo avisa (el
   original abría la compra). Las llaves se ven en la ventana y en el fin de carrera. También de la 1.9, en todos los
   mapas: de frente solo se choca si el golpe es por delante (antes chocaba aunque se tocara de lado o por detrás).
   Volver a jugar tras una carrera larga: la vía se vacía durante el fin de carrera (6 ms por fotograma) y no dos
   veces seguidas al pulsar JUGAR (era el tirón de ~50-67 ms de la intro).
4d. Cámara en los túneles: los túneles son varios colliders "Subway" que se solapan. El original lleva la lista de
   los que toca (Character.subwayColliders) y sigue dentro mientras quede alguno; el port salía al dejar cualquiera y
   la cámara se quedaba sin techo hasta el siguiente (con zapatillas atravesaba el techo). Arreglado en todos los
   mapas, y se vacía al reiniciar y al despegar con el jetpack (ForceLeaveSubway), como el original.
5. Pendientes antiguos: partículas (humo de colores de la tabla, brillos de power-ups), menús con la libreta y cajas
   misteriosas como el original (casi hechos), texturas por paquetes.

## 8. Cómo pasar información a Claude

* El **devlog** de la versión de pruebas (`devlog.txt`, y `devlog_prev.txt` de la sesión anterior) es lo más útil:
  dice dónde y por qué baja (CPU o GE, y en qué bloque).
* Capturas con **SELECT + R** (`captura_NNN.bmp`), salen sin el medidor.
* Decir qué mapa (clásico o 1.5.2), si era Halloween/Navidad y qué se estaba haciendo.

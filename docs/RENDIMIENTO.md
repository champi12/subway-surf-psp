# Rendimiento y memoria: cómo saber si funciona en una PSP real

Hay dos herramientas dentro del propio juego, y las dos funcionan igual en PPSSPP y en la consola:

1. **El medidor en pantalla** (botón **SELECT**): tiempos, RAM, VRAM y una gráfica de fotogramas.
2. **El devlog** (`devlog.txt` en la carpeta del juego): un registro de todo lo que pasa, para leerlo después en el PC.

## 1. El medidor en pantalla (SELECT)

Cada pulsación de SELECT pasa a la página siguiente: **resumen → memoria → gráfica → oculto**.

### Página 1: resumen

| Dato | Qué es |
|---|---|
| `FPS` | Fotogramas por segundo (verde ≥ 30, amarillo 24-30, rojo < 24) |
| `CPU x ms` | Trabajo de la CPU en cada fotograma (lógica + preparar el dibujo) |
| `espera x ms` | Tiempo esperando a que el GE (la GPU) termine y llegue el VBlank |
| `vert / tri / draw / clip` | Vértices transformados por la CPU, triángulos, llamadas de dibujo y triángulos recortados |
| `RAM` / `VRAM` | Memoria ocupada (verde < 75 %, amarillo < 90 %, rojo más) |

### Página 2: memoria

| Línea | Qué mirar |
|---|---|
| **RAM montón: usado / total (pico)** | Toda la memoria que el juego pide con `malloc` (modelos, animaciones, sonidos, texturas). El **total** es lo que la consola le deja al juego. Si el **pico** llega cerca del total, el juego se colgará al cargar algo. |
| **Sistema libre** | Memoria fuera del montón, para los hilos y los módulos de audio. Por debajo de 256 KB sale en rojo. |
| **VRAM: usado / 2048 KB** | La memoria de vídeo de la PSP (2 MB). 816 KB son fijos (2 pantallas de 480×272 + el búfer Z); el resto son las texturas del escenario y de los trenes. |
| **Texturas en RAM / sin sitio en VRAM** | Las que no caben en la VRAM se leen desde la RAM. Funciona, pero gasta más ancho de banda. |
| **Lista GE** | Espacio usado de la lista de comandos del GE. Por encima del 80 % sale en rojo. Si se llenara, el juego deja de dibujar objetos (no se cuelga) y lo apunta en el devlog. |
| **Buffer vert / ind** | Espacio de vértices e índices del fotograma. Si se llena, se pierden dibujos. |
| **Dibujos perdidos** | Tiene que ser **0**. |
| **Tirones** | Fotogramas de más de 50 ms desde que arrancó el juego. |
| **CPU / bus / batería** | Frecuencias reales y estado de la batería (en la consola real, también la temperatura). |

### Página 3: gráfica de tiempos

Muestra los últimos 128 fotogramas: **verde** por debajo de 16,7 ms (60 FPS), **amarillo** por debajo de 33,3 ms
(30 FPS) y **rojo** por encima. Sirve para ver de un vistazo si hay tirones y cuándo aparecen.

## 2. El devlog (`devlog.txt`)

El juego escribe `devlog.txt` en su carpeta: `ms0:/PSP/GAME/SUBWAY/devlog.txt` en la consola. En PPSSPP está en
la carpeta `memstick/PSP/GAME/SUBWAY/` del emulador. Se guardan los **tres últimos**: `devlog.txt` (esta partida),
`devlog_prev.txt` y `devlog_prev2.txt`, para que no se pierda el de un cuelgue aunque se vuelva a abrir el juego
un par de veces.

Lo escribe un hilo de baja prioridad **cada segundo**, abriendo y cerrando el archivo cada vez: si la consola se
cuelga, en la tarjeta queda todo menos el último segundo. Los errores se escriben al momento. Ocupa unos 10 KB
por minuto de juego.

### El vigilante (BLOQUEO)

Un hilo de prioridad alta comprueba cada medio segundo que el bucle principal sigue avanzando. Si lleva **3 s
parado** (10 s durante la carga inicial), escribe al momento una línea así:

    BLOQUEO el bucle principal lleva 3.0 s parado en: r_end_frame: esperando a que el GE termine (sceGuSync) |
    hilo de musica: sceAudioSRCOutputBlocking (avanza) | mezclador de sonidos: sceAudioOutputBlocking (avanza) |
    GE: atascado (stall) | estado CARRERA, 143 m

Dice en qué fase está cada hilo y si avanza, y el estado del GE. Si el juego se recupera, añade `RECUPERADO`.
**Si después de un cuelgue no hay ninguna línea `BLOQUEO`**, la consola entera se detuvo por una excepción de la CPU
(un acceso a memoria inválido o desalineado, o una corrupción de memoria): eso también es una pista.

### El capturador de excepciones (`crash.txt`)

Junto al `EBOOT.PBP` va `exhandler.prx`, un módulo de kernel (solo funciona en CFW). Si la CPU lanza una excepción, en
vez de congelarse la consola se muestra una pantalla con el error y se escribe `crash.txt` (y lo mismo al final de
`devlog.txt`) con:

* el **tipo de excepción** (dirección desalineada, dirección prohibida, instrucción `break`, coprocesador no disponible…),
* **EPC**: la dirección de la instrucción que falló, y **BadVAddr**: la dirección de memoria que intentó usar,
* el **hilo** que falló y la **fase** en la que estaba cada hilo del juego,
* todos los registros y la pila de llamadas.

Para traducir las direcciones a función y línea del código hace falta el `subway.elf` de **la misma compilación**
(`build_dist.sh` lo deja en `dist/subway.elf`):

    tools/crash.sh crash.txt dist/subway.elf

    Fallo en                     0x0880B0E8  r_draw_skinned at src/render.c:863
    Llamado desde (RA)           0x0881D438  game_update at src/game.c:2140

Al arrancar, el devlog dice si el capturador quedó activo (`Capturador de excepciones: activo`). En PPSSPP sale
"NO activo": el emulador no implementa esa parte del kernel.

### Diferencias entre PPSSPP y la consola que pueden colgar el juego

| En la consola real | En PPSSPP (por defecto) |
|---|---|
| Leer un `int`/`float` de una dirección no múltiplo de 4 (o `lv.q` no múltiplo de 16) cuelga la consola | Funciona |
| Leer una dirección inválida cuelga la consola | Se ignora |
| Un hilo de más prioridad puede interrumpir a otro en mitad de cualquier función | Los hilos solo cambian en las llamadas al sistema |

Para la prueba automática hay un parche para PPSSPPHeadless 1.18.1 (`tools/ppsspp_estricto.patch`) que detecta los dos primeros casos (`PPSSPP_STRICT_ALIGN=1
PPSSPP_STRICT_MEM=1 PPSSPP_ARGS=-i`), parcheando el intérprete. El tercero no se puede emular: por eso solo el hilo
principal formatea números decimales (`%f`), ya que la libc de la PSP (newlib) comparte entre hilos la memoria que usa
para formatearlos.

### Qué contiene

```
[    0.00] Firmware: 6.60 (0x06060010)
[    0.00] CPU 333 MHz, bus 166 MHz | bateria 87% 31 C | cargador no
[    0.00] RAM: monton de C 18814 KB (lo que la consola le deja al juego para datos)
[    0.09] CARGA world.bin     686 KB en   85 ms
[    0.71] CARGA mesh.bin     5629 KB en  608 ms
[    1.29] GE iniciado: VRAM framebuffers+Z 816 KB, texturas hasta 1232 KB | VFPU si (autotest correcto)
[    1.37] AUDIO modulos: avcodec 0x00000000 mp3 0x00000000 atrac3 0x00000000 | sceMp3InitResource 0x00000000
[    1.43] Arranque completo en 1.43 s | monton 11730/18814 KB usado | libre fuera del monton 660 KB
[    1.49] TEXTURA main 512x512 5650 512 KB en VRAM (56.0 ms)
[    1.66] ESTADO - -> MENU
[    2.65] TRAMO start2 z=360
[    6.67] STAT fps 59.7/59.3 | cpu 6.9/15.2 ms | ge 10.3 ms | vert 21066 tri 31152 draw 498 clip 334 | heap 13014/18814 KB (pico 13014) sis 660 KB | vram 1840/2048 KB (tex 1024 KB, 2) | ram tex 1284 KB (5) | lista 19% (28%) vert 37% (44%) ind 2% (10%) perd 0 | CARRERA 39 m | 333 MHz bat 87% 31 C
[    9.70] CHOQUE contra barrera en el carril 2 a 82 m (velocidad 114)
[   11.76] FIN DE CARRERA puntos 82 (final 104) monedas 11 metros 82 duracion 9 s
```

| Tipo de línea | Qué dice |
|---|---|
| Cabecera | Firmware, frecuencias, batería, **RAM total que la consola le da al juego**, VFPU, módulos de audio |
| `CARGA` | Cada archivo de `data/`, su tamaño y lo que tardó en leerse de la Memory Stick |
| `TEXTURA` | Cada textura cuando se carga por primera vez: tamaño, formato, si fue a VRAM o a RAM y cuánto tardó |
| `PERFIL` (cada 5 s) | En qué se va la CPU por fotograma: lógica (con la física dentro) y preparar el dibujo, y cuántos vértices transforma la CPU y cuántos el GE |
| `DIBUJO` (cada 5 s) | El dibujo por bloques: vía, personajes, monedas, efectos y transparentes, entorno del menú, cielo e interfaz, y los triángulos recortados por software |
| `STAT` (cada 5 s) | FPS medios y mínimos; CPU media y máxima; espera al GE; máximos de vértices, triángulos y dibujos; memoria; VRAM; ocupación de las listas del GE con su pico; dibujos perdidos; estado, metros, MHz, batería y temperatura |
| `ESTADO`, `TRAMO`, `CHOQUE`, `FIN DE CARRERA`, `MUSICA` | Qué estaba pasando: si el juego se cuelga, las últimas líneas dicen dónde |
| `TIRON` | Fotograma de más de 50 ms, con su CPU, su espera de GE, el tiempo "otros" (fuera del juego: otros hilos, la Memory Stick) y si coincidió con una carga de la Memory Stick |
| `AVISO` | Memoria por encima del 90 %, lista del GE por encima del 80 %, dibujos perdidos, textura sin sitio en VRAM, batería caliente, y cualquier lista interna llena (monedas, objetos en movimiento, colliders, objetos transparentes) |
| `ERROR` | Archivo que falta, sin memoria, fallo de la música |
| `BLOQUEO`, `RECUPERADO` | El vigilante: ver arriba |
| `Alineacion de los datos` | Al arrancar se comprueba que modelos, animaciones y sonidos están alineados en memoria (la CPU real se cuelga si no) |
| `Idioma de la consola` | El idioma del sistema y el de la interfaz |

Para no generarlo (versión final): `make clean && make DEVLOG=0`. El medidor de SELECT sigue funcionando.

## 3. ¿Qué datos del emulador valen para la consola real?

PPSSPP ejecuta el código del juego tal cual, pero en la CPU del PC. Por eso unas medidas son idénticas a las de la
consola y otras solo orientativas:

| Dato | En PPSSPP | Por qué |
|---|---|---|
| **RAM** (montón usado, pico, total) | **Exacto**, con el modelo PSP-1000 | El juego reserva exactamente la misma memoria |
| **VRAM**, texturas, lista del GE, buffers, dibujos perdidos | **Exacto** | Son cuentas del propio juego |
| Vértices, triángulos, llamadas de dibujo | **Exacto** | Igual |
| Errores (archivos, módulos de audio, AT3) | **Casi exacto** | Algún módulo puede comportarse distinto en un CFW concreto |
| **CPU ms** | **Muy por lo bajo** | PPSSPP no imita la caché de 16 KB ni las esperas de la memoria. Medido con el devlog de una PSP real: **unas 5 veces más** que en el emulador a la misma frecuencia |
| **espera / GE ms** | **No vale** | PPSSPP no imita la velocidad real del GE (relleno de píxeles, lectura de texturas). Este dato solo se puede medir en la consola |
| FPS | **No vale** | Depende de lo anterior |
| Tiempos de `CARGA` y `TEXTURA` | **Orientativo** | La Memory Stick real es más lenta, sobre todo en la PSP-1000 |

**Con el emulador puedes saber con seguridad si cabe en memoria.** Para la velocidad solo da una idea: multiplica
la CPU del emulador por 5 (a 333 MHz). Para 30 FPS la consola tiene 33 ms por fotograma, así que en el emulador hay
que estar por debajo de **6 ms**. La prueba definitiva de velocidad (FPS y espera del GE) es el devlog de una
consola real.

Lo que más cuesta en la consola y casi nada en el emulador: recorrer muchos datos en cada fotograma (la caché es de
16 KB) y las llamadas al sistema (leer el reloj, por ejemplo). Por eso las optimizaciones del apartado 7 van sobre
todo a tocar menos memoria por fotograma.

### Medidas actuales en PPSSPP (modelo PSP-1000, 333 MHz)

| Dato | Medido | Límite |
|---|---|---|
| CPU media / máxima en carrera | 5-6 ms / ~20 ms (al reiniciar la vía) | < 6 ms de media (≈ 30 ms en la consola) |
| Vértices por fotograma | 12.000-14.000 por la CPU, 20.000-23.000 por el GE | |
| RAM (montón) | 13,0 MB de 18,4 MB (71 %) | < 90 % |
| VRAM | 1.840 de 2.048 KB | Si se llena, las texturas van a RAM (no falla) |
| Lista del GE / vértices / índices (pico) | 28 % / 45 % / 10 % (con jetpack) | < 80 % |
| Dibujos perdidos | 0 | 0 |

## 4. Configurar PPSSPP para que se parezca a la consola

**Ajustes → Sistema:**

* *Modelo de PSP*: **PSP-1000**. El emulador da solo la RAM de la primera PSP. Si el juego se pasara de memoria,
  fallaría también en el emulador.
* *Frecuencia de la CPU*: **333 MHz**. Para tener margen, prueba también **222 MHz**: si a 222 sigue fluido, en la
  consola a 333 irá sobrado.

**Ajustes → Gráficos:**

* *Modo de renderizado*: **Renderizado por software**. Dibuja exactamente como el GE: si algo parpadea o desaparece aquí,
  pasaría lo mismo en la consola.
* *Saltar fotogramas*: desactivado. *Hacks de velocidad*: desactivados.

**Ajustes → Herramientas → Herramientas de desarrollador:**

* *Mostrar estadísticas de depuración*: dibujos, vértices y texturas por fotograma, vistos desde el GE.
* *Perfilador de fotogramas*: reparte el tiempo emulado entre la CPU y el GE.
* *Depurador del GE*: avanza dibujo a dibujo. Permite ver qué textura usa cada uno y cuántos vértices dibuja, y en la
  pestaña de **VRAM / framebuffers** se ve qué ocupa la memoria de vídeo.
* *Visor de memoria* (menú Depurar, en la versión de Windows): la RAM de la PSP byte a byte.
* *Registro* (Depurar → Registro, o la consola de PPSSPP): mensajes del sistema, por ejemplo si falla un módulo.

## 5. En la consola real

1. Instala el juego como siempre (`ms0:/PSP/GAME/SUBWAY/`).
2. Juega unos minutos, mejor con varias carreras y algún jetpack. Pulsa SELECT para ver las páginas del medidor.
3. Sal con HOME → Salir. Así se escribe el resumen final (`Fin: N tirones`).
4. Conecta la PSP al PC por USB y abre `PSP/GAME/SUBWAY/devlog.txt` con el Bloc de notas.

Qué mirar:

* Cabecera, `RAM: monton de C`: lo que te da tu consola. Una PSP-1000 da ~18.800 KB, una 2000/3000 lo mismo
  (el juego no pide la memoria extra).
* `STAT`: el juego va a **30 FPS fijos** (como el original en Android). Si `fps` está en 30 y `cpu` + `ge` quedan
  por debajo de 33 ms, va bien.
* `PERFIL`: si va lento, dice si el tiempo se va en la lógica, en la física o en el dibujo.
* `BLOQUEO`: si el juego se cuelga, esta línea dice dónde. Después de un cuelgue, envía `devlog.txt`, `devlog_prev.txt`
  y `devlog_prev2.txt`.
* `TIRON`: si aparecen muchos fuera de las cargas, mándame el devlog.
* `AVISO` y `ERROR`: si sale alguno, mándame el archivo completo.

**Avanzado (PSPLink):** con PSPLink instalado y conectado por USB, `pspsh` muestra en el PC los mensajes y,
si el juego se cuelga, la dirección exacta del fallo. No hace falta para usar el devlog.

## 6. Prueba automática (sin mandos)

`tools/autotest.sh` compila una versión que se juega sola. La ejecuta en PPSSPPHeadless y guarda `devlog.txt`,
`perf.log` (los modelos que más vértices gastan) y capturas de pantalla:

    PPSSPP_HEADLESS=/ruta/a/PPSSPPHeadless tools/autotest.sh 60 salida data

Con `XFLAGS=-DAT_PAGES` las capturas muestran las páginas del medidor. Para probar un tramo concreto:
`XFLAGS='-DAT_GOD -DAT_CHUNK=\"bridge_various\"'` (invencible y forzando ese tramo; `AT_CHUNK2` añade un segundo
tramo que se alterna con el primero).

## 7. Optimizaciones del motor

| Técnica | Qué ahorra |
|---|---|
| **CPU y GE en paralelo**: cada fotograma se prepara mientras el GE dibuja el anterior | Casi todo el tiempo de CPU queda oculto detrás del dibujo |
| **Escenario troceado** en bloques de 32 unidades; se descartan los que quedan fuera de cámara, y los bloques pequeños pueden curvarse en el GE | En la consola real el coste va con los vértices que transforma la CPU (~2,5 µs cada uno): de ~12.500 a ~3.500 por fotograma |
| **Camino rápido** sin recorte para los bloques que caben enteros en la banda de guarda del GE | Comprobaciones por vértice |
| **VFPU** para transformar vértices, comprobada al arrancar contra la versión en C | Cálculo por vértice |
| **Curvatura linealizada en el GE** para objetos pequeños y bloques lejanos (error < 1 píxel) | ~17.000 vértices por fotograma |
| **Skinning por hardware** (lotes de ≤ 8 huesos) para Jake, el inspector y el perro | Toda la piel de los personajes |
| Texturas del escenario y los trenes en **VRAM**; el resto con paleta de 256 colores y *swizzled* | Ancho de banda de texturas |
| Buffers del GE ajustados a lo medido con el devlog (pico × 2) | 3,6 MB más de RAM libre |
| **Curvatura en el GE según el error real**: cada bloque va al GE si el error de aproximar la curvatura (`k·r²/d`) es menor de 0,75 píxeles | ~40 % menos vértices en la CPU |
| **Física solo cerca del personaje**: solo los tramos entre 80 unidades detrás y 250 delante, y en cada movimiento se preseleccionan las cajas cercanas una vez (no en cada subpaso) | Miles de lecturas de memoria por fotograma |
| Componentes de dibujo y de colisión de cada nodo calculados al arrancar; las ramas sin nada que dibujar o chocar se saltan | Recorrer la escena |
| Sin lecturas del reloj por cada trozo de modelo (en la PSP real cada una es una llamada al sistema) | Varios ms por fotograma en la consola |
| **30 FPS fijos**, como el original | Ritmo estable en vez de saltar entre 60, 30 y 20 |
| Texto: una llamada de dibujo por frase (no por letra); centro de cada trozo calculado una vez | Llamadas de dibujo y cálculo por trozo |
| Banda de guarda de 6 (el GE admite hasta 8,5 en horizontal) | Menos triángulos recortados por software |

## 8. Si en la consola real fuera lento

Por orden de impacto (nada de esto cambia la jugabilidad):

1. Bajar la distancia de dibujo (`r_far` en `src/render.c`; el original usa 1000).
2. Simplificar los edificios lejanos (el conversor puede generar versiones con menos polígonos).
3. Reducir el número de llamadas de dibujo juntando bloques pequeños del escenario.

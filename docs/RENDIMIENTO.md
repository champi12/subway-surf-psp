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
la carpeta `memstick/PSP/GAME/SUBWAY/` del emulador. El de la partida anterior se guarda como `devlog_prev.txt`, por si el
juego se colgó y lo volviste a abrir.

Lo escribe un hilo de baja prioridad cada 5 segundos, abriendo y cerrando el archivo cada vez. Así, si el juego se
cuelga, en la tarjeta queda todo menos los últimos segundos. Los errores se escriben al momento. Ocupa unos
8 KB por minuto de juego.

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
| `STAT` (cada 5 s) | FPS medios y mínimos; CPU media y máxima; espera al GE; máximos de vértices, triángulos y dibujos; memoria; VRAM; ocupación de las listas del GE con su pico; dibujos perdidos; estado, metros, MHz, batería y temperatura |
| `ESTADO`, `TRAMO`, `CHOQUE`, `FIN DE CARRERA`, `MUSICA` | Qué estaba pasando: si el juego se cuelga, las últimas líneas dicen dónde |
| `TIRON` | Fotograma de más de 50 ms, con su CPU y espera de GE, y si coincidió con una carga de la Memory Stick |
| `AVISO` | Memoria por encima del 90 %, lista del GE por encima del 80 %, dibujos perdidos, textura sin sitio en VRAM, batería caliente, pool de monedas agotado |
| `ERROR` | Archivo que falta, sin memoria, fallo de la música |

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
| **CPU ms** | **Orientativo, por lo bajo** | PPSSPP no imita los fallos de caché ni las esperas de la memoria. En la consola real cuenta con **1,5 a 2,5 veces más** |
| **espera / GE ms** | **No vale** | PPSSPP no imita la velocidad real del GE (relleno de píxeles, lectura de texturas). Este dato solo se puede medir en la consola |
| FPS | **No vale** | Depende de lo anterior |
| Tiempos de `CARGA` y `TEXTURA` | **Orientativo** | La Memory Stick real es más lenta, sobre todo en la PSP-1000 |

**Con el emulador puedes saber con seguridad si cabe en memoria.** Para la velocidad da una estimación con
margen: si la CPU va por debajo de **12-15 ms** en el emulador, en la consola debería ir bien a 30 FPS. La prueba
definitiva de velocidad (FPS y espera del GE) es el devlog de una consola real.

### Medidas actuales en PPSSPP (modelo PSP-1000, 333 MHz)

| Dato | Medido | Límite |
|---|---|---|
| CPU media / máxima en carrera | 6-8 ms / ~19 ms (al reiniciar la vía) | < 15 ms de media |
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
* `STAT`: si `fps` está en 30 o más y `cpu` + `ge` quedan por debajo de 33 ms, va bien.
* `TIRON`: si aparecen muchos fuera de las cargas, mándame el devlog.
* `AVISO` y `ERROR`: si sale alguno, mándame el archivo completo.

**Avanzado (PSPLink):** con PSPLink instalado y conectado por USB, `pspsh` muestra en el PC los mensajes y,
si el juego se cuelga, la dirección exacta del fallo. No hace falta para usar el devlog.

## 6. Prueba automática (sin mandos)

`tools/autotest.sh` compila una versión que se juega sola. La ejecuta en PPSSPPHeadless y guarda `devlog.txt`,
`perf.log` (los modelos que más vértices gastan) y capturas de pantalla:

    PPSSPP_HEADLESS=/ruta/a/PPSSPPHeadless tools/autotest.sh 60 salida data

Con `XFLAGS=-DAT_PAGES` las capturas muestran las páginas del medidor.

## 7. Optimizaciones del motor

| Técnica | Qué ahorra |
|---|---|
| **CPU y GE en paralelo**: cada fotograma se prepara mientras el GE dibuja el anterior | Casi todo el tiempo de CPU queda oculto detrás del dibujo |
| **Escenario troceado** en bloques de 64 unidades; se descartan los que quedan fuera de cámara | ~40 % de los vértices |
| **Camino rápido** sin recorte para los bloques que caben enteros en la banda de guarda del GE | Comprobaciones por vértice |
| **VFPU** para transformar vértices, comprobada al arrancar contra la versión en C | Cálculo por vértice |
| **Curvatura linealizada en el GE** para objetos pequeños y bloques lejanos (error < 1 píxel) | ~17.000 vértices por fotograma |
| **Skinning por hardware** (lotes de ≤ 8 huesos) para Jake, el inspector y el perro | Toda la piel de los personajes |
| Texturas del escenario y los trenes en **VRAM**; el resto con paleta de 256 colores y *swizzled* | Ancho de banda de texturas |
| Buffers del GE ajustados a lo medido con el devlog (pico × 2) | 3,6 MB más de RAM libre |

## 8. Si en la consola real fuera lento

Por orden de impacto (nada de esto cambia la jugabilidad):

1. Bajar la distancia de dibujo (`r_far` en `src/render.c`; el original usa 1000).
2. Simplificar los edificios lejanos (el conversor puede generar versiones con menos polígonos).
3. Fijar 30 FPS (sincronizar cada 2 VBlank) para que el ritmo sea estable.

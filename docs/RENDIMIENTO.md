# ¿Funcionará en una PSP real? Cómo medirlo sin tener la consola

## Por qué los FPS de PPSSPP no bastan

PPSSPP ejecuta el juego en el procesador del PC (recompilado con JIT), que es decenas de veces más rápido que el
MIPS de la PSP. Lo que **sí** imita es el *reloj interno* de la PSP: cada instrucción que ejecuta el juego avanza el
tiempo emulado, como si fuera una CPU a 333 MHz. Pero **no** reproduce:

* los fallos de caché y las esperas de la memoria (la PSP real pierde muchos ciclos ahí),
* la latencia real de la FPU y la VFPU,
* el ancho de banda real del GE (relleno de píxeles, texturas leídas desde la RAM en lugar de la VRAM).

Por eso un juego puede ir a 60 FPS en PPSSPP y a 20 en la consola. La forma fiable de estimarlo es medir el
**trabajo** que hace el juego en cada fotograma y compararlo con lo que sabemos que la PSP aguanta.

## El medidor interno (L + SELECT)

El juego trae un medidor que se activa con **L + SELECT** (en la compilación de prueba sale siempre):

| Dato | Qué es | Límite razonable para la PSP real a 30 FPS |
|---|---|---|
| `CPU x ms` | Tiempo de la CPU por fotograma: lógica + transformar los vértices (la curvatura del mundo se calcula por software) | **< 15 ms emulados** (deja margen ×2 para los fallos de caché reales; el presupuesto a 30 FPS son 33 ms) |
| `GPU x ms` | Espera al GE + sincronismo vertical | Si CPU + GPU > 33 ms, no llega a 30 FPS |
| `vert` | Vértices transformados por la CPU en ese fotograma | < 40.000 |
| `tri` | Triángulos enviados al GE | < 30.000 |
| `draw` | Llamadas de dibujo | < 600 |
| `clip` | Triángulos recortados por software (cerca de la cámara) | Normalmente < 400 |
| `RAM libre` | Memoria ocupada por el juego (montón de C) | La PSP-1000 tiene 24 MB para juegos: hay que quedar **por debajo de ~18 MB** |

Medidas actuales en el emulador (tramos normales):

    CPU 6-9 ms   vert CPU 17.000-27.000 (+17.000 por hardware)   tri 23.000-30.000   draw 380-470   memoria 13,2 MB

La memoria está bien para una PSP-1000 (24 MB).

## Optimizaciones que lleva el motor

| Técnica | Qué ahorra |
|---|---|
| **CPU y GE en paralelo**: la lista de dibujo de cada fotograma se prepara mientras el GE dibuja el anterior (listas y búferes de vértices duplicados) | Casi todo el tiempo de CPU queda oculto detrás del dibujo |
| **Escenario troceado** en bloques de 64 unidades: se descarta cada bloque fuera de cámara | ~40 % de los vértices |
| **Camino rápido** sin recorte para los bloques que caben enteros en la banda de guarda del GE | Comprobaciones por vértice y por triángulo |
| **VFPU**: la transformación de vértices usa la matriz 4×4 de la unidad vectorial (`vhtfm4.q`), comprobada al arrancar contra la versión en C | Cálculo por vértice |
| **Curvatura linealizada en el GE** para objetos pequeños (monedas, power-ups, barreras) y bloques a más de 550 unidades: error < 1 píxel | ~17.000 vértices por fotograma que la CPU ya no toca |
| **Skinning por hardware**: Jake, el inspector y el perro se dividen en lotes de ≤ 8 huesos y los deforma el GE | Toda la piel de los personajes |
| Texturas del escenario y los trenes (16 bits) en la **VRAM**; el resto con paleta de 256 colores y *swizzled* | Ancho de banda de texturas |

Antes de estas optimizaciones la CPU iba a ~15 ms por fotograma con 37.000–50.000 vértices transformados por
software.

## Configurar PPSSPP para que se parezca a la consola

En **Ajustes → Sistema**:

* *Modelo de PSP*: **PSP-1000**. Así el emulador solo da 24 MB de RAM como la primera PSP; si el juego se pasara
  de memoria, fallaría también en el emulador.
* *Frecuencia de la CPU*: **333 MHz** (la que pide el juego). Para tener margen, prueba también a **222 MHz**:
  si a 222 MHz sigue yendo fluido, en la consola real a 333 MHz irá sobrado.

En **Ajustes → Gráficos**:

* *Modo de renderizado*: **Renderizado por software** (más lento en el PC, pero dibuja exactamente como el GE de la
  PSP: si algo desaparece o parpadea aquí, pasaría lo mismo en la consola).
* *Saltar fotogramas*: desactivado. *Hacks de velocidad*: desactivados.

En **Ajustes → Herramientas → Herramientas de desarrollador**:

* *Mostrar estadísticas de depuración*: cuenta de llamadas de dibujo, vértices y texturas por fotograma (lo mismo
  que el medidor interno, visto desde el GE).
* *Mostrar FPS*: el porcentaje de "velocidad" indica si el **PC** aguanta la emulación; no es la velocidad de la PSP.
* *Perfilador de fotogramas* (*Frame profiler*): reparte el tiempo emulado entre la CPU y el GE.
* *Depurador del GE* (*GE debugger*): permite avanzar dibujo a dibujo y ver qué textura y cuántos vértices usa cada
  uno; sirve para encontrar qué objeto es el más caro.

## Prueba automática (sin mandos)

`tools/autotest.sh` compila una versión que se juega sola (cambia de carril, salta y rueda al azar), la ejecuta en
PPSSPPHeadless y guarda:

* `perf.log`: cada 300 fotogramas, FPS, ms de CPU, vértices, triángulos, memoria, distancia recorrida y los 8
  modelos que más vértices gastan.
* `shotNNNNNN.png` y `sheet.png`: capturas de pantalla.

Ejemplo:

    PPSSPP_HEADLESS=/ruta/a/PPSSPPHeadless tools/autotest.sh 60 salida data

## Si en la consola real fuera lento

Por orden de impacto (todo se puede hacer sin cambiar la jugabilidad):

1. Bajar la distancia de dibujo (`r_far` en `src/render.c`, el original usa 1000).
2. Reducir polígonos de los edificios lejanos (el conversor puede generar versiones simplificadas).
3. Pasar la transformación de vértices a la VFPU.
4. Bajar a 30 FPS fijos (sincronizar cada 2 VBlank).

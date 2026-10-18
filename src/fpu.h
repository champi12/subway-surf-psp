/* fpu.h - excepciones de la FPU
 *
 * En la PSP real la FPU puede levantar una excepcion de la CPU con la division por cero, las operaciones invalidas
 * (sqrt de un negativo, 0/0, inf*0) o el desbordamiento, y eso cuelga toda la consola. PPSSPP no las emula.
 * Con -ffast-math el compilador adelanta divisiones y raices antes del "if" que las protege (p. ej. 1/l2 con l2 == 0
 * en la capsula del personaje al rodar) aunque luego descarte el resultado: en la consola eso era un cuelgue.
 * Cada hilo tiene su propio registro de control (FCR31): hay que llamar a fpu_no_exceptions() al empezar cada hilo.
 * Se quitan los bits de activacion (7-11) y de causa (12-17); se conservan el redondeo y el bit de condicion.
 */
#ifndef FPU_H
#define FPU_H

static inline unsigned int fpu_get_fcr31(void)
{
    unsigned int v;
    __asm__ volatile("cfc1 %0, $31" : "=r"(v));
    return v;
}

static inline void fpu_no_exceptions(void)
{
    unsigned int v = fpu_get_fcr31() & ~0x0003FF80u;
    __asm__ volatile("ctc1 %0, $31" : : "r"(v));
}

#endif

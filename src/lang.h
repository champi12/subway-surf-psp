/* lang.h - idioma de la interfaz: el de la consola (espanol si la PSP esta en espanol; si no, ingles como el original) */
#ifndef LANG_H
#define LANG_H

extern int lang_es;
void lang_init(void);
#define TR(es, en) (lang_es ? (es) : (en))

#endif

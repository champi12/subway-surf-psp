/* lang.c - lee el idioma del sistema (Ajustes del sistema -> Idioma) */
#include <psputility.h>
#include "lang.h"
#include "devlog.h"

int lang_es = 1;

void lang_init(void)
{
    int l = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
    int r = sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &l);
    lang_es = r >= 0 && l == PSP_SYSTEMPARAM_LANGUAGE_SPANISH;
#ifdef AT_ES
    lang_es = 1;   /* prueba: interfaz en espanol aunque el emulador este en ingles */
#endif
    dlog("Idioma de la consola: %d -> interfaz en %s", l, lang_es ? "espanol" : "ingles");
}

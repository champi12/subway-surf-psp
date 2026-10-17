#!/bin/bash
# Traduce las direcciones de crash.txt (lo escribe exhandler.prx cuando la PSP lanza una excepcion) a funcion y
# linea del codigo. Hace falta el subway.elf de LA MISMA compilacion que el EBOOT que se colgo
# (build_dist.sh lo deja en dist/subway.elf).
#   tools/crash.sh crash.txt [subway.elf]
set -e
CRASH=${1:-crash.txt}
ELF=${2:-dist/subway.elf}
[ -f "$ELF" ] || ELF=subway.elf
[ -n "$PSPDEV" ] || PSPDEV=$HOME/pspdev
A2L=$PSPDEV/bin/psp-addr2line
OBJDUMP=$PSPDEV/bin/psp-objdump
if [ ! -f "$CRASH" ] || [ ! -f "$ELF" ]; then echo "Uso: tools/crash.sh crash.txt [subway.elf]"; exit 1; fi
show() {   # $1 = etiqueta, $2 = direccion
    printf "%-28s 0x%s  " "$1" "$2"
    "$A2L" -f -C -p -e "$ELF" "0x$2" 2>/dev/null | sed 's# at .*/src/# at src/#'
}
grep -E "^Tipo|^BadVAddr|^Hilo|^Fase" "$CRASH" | tr -d '\r'
echo
EPC=$(grep -o "EPC (instruccion que fallo): 0x[0-9A-F]*" "$CRASH" | tail -1 | grep -o "[0-9A-F]*$")
RA=$(grep -o "RA (volver a): 0x[0-9A-F]*" "$CRASH" | tail -1 | grep -o "[0-9A-F]*$")
show "Fallo en" "$EPC"
show "Llamado desde (RA)" "$RA"
echo "Pila de llamadas:"
grep -E "^  0x[0-9A-F]+ -> 0x" "$CRASH" | tr -d '\r' | while read a arrow f; do
    show "  llamada en" "${a#0x}"
done
echo
echo "Instrucciones alrededor del fallo:"
E=$((16#$EPC))
"$OBJDUMP" -d --start-address=$((E - 24)) --stop-address=$((E + 12)) "$ELF" | grep -E "^ +[0-9a-f]+:" | \
    sed "s/^\( *$(printf '%x' $E):\)/>>\1/"

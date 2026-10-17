#!/bin/bash
# Prueba automatica en PPSSPP sin pantalla: compila con AUTOTEST (el juego se juega solo),
# guarda capturas cada AT_SHOT fotogramas y el registro de rendimiento perf.log.
#   tools/autotest.sh <segundos> <carpeta_salida> <carpeta_data>
# Variables: PPSSPP_HEADLESS (ruta a PPSSPPHeadless), SHOT (fotogramas entre capturas)
set -e
cd "$(dirname "$0")/.."
SECS=${1:-60}; OUT=$(realpath -m ${2:-autotest_out}); DATA=$(realpath ${3:-data})
PPSSPP_HEADLESS=${PPSSPP_HEADLESS:-PPSSPPHeadless}
make clean >/dev/null
make -j4 AUTOTEST=1 EXTRA="-DAT_SHOT=${SHOT:-150} $XFLAGS" 2>&1 | grep -E "error" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cp EBOOT.PBP "$OUT/"; ln -s "$DATA" "$OUT/data"
make clean >/dev/null   # los .o de prueba no deben mezclarse con la compilacion normal
(cd "$OUT" && timeout $((SECS * 20)) "$PPSSPP_HEADLESS" $PPSSPP_ARGS "$OUT/EBOOT.PBP" --graphics=${GFX:-software} --timeout=$SECS -l 2>&1 \
    | tail -40 > "$OUT/log.txt") || true
python3 - "$OUT" <<'PY' || true
import sys, glob, os
import numpy as np
from PIL import Image, ImageDraw
d = sys.argv[1]
fs = sorted(glob.glob(d + '/shot*.raw'))
for f in fs:
    a = np.frombuffer(open(f, 'rb').read(), '<u2').reshape(272, 480)
    r = ((a & 31) << 3).astype(np.uint8); g = (((a >> 5) & 63) << 2).astype(np.uint8); b = (((a >> 11) & 31) << 3).astype(np.uint8)
    Image.fromarray(np.dstack([r, g, b])).save(f[:-4] + '.png')
sel = fs[::max(1, len(fs) // 12)][:12]
if sel:
    sheet = Image.new('RGB', (480 * 3, 286 * ((len(sel) + 2) // 3)))
    dr = ImageDraw.Draw(sheet)
    for i, f in enumerate(sel):
        im = Image.open(f[:-4] + '.png')
        x, y = (i % 3) * 480, (i // 3) * 286
        sheet.paste(im, (x, y + 14)); dr.text((x + 3, y + 1), os.path.basename(f), fill=(255, 255, 0))
    sheet.save(d + '/sheet.png'); print(len(fs), 'capturas ->', d + '/sheet.png')
PY

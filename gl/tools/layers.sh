#!/bin/sh
# layers.sh - DOS-GL's GL layer (src/gl, src/dgl) includes no card's headers:
# only the HAL's platform ones (sys, fp, prof, serial, types) and the device
# level (rg/device.h). The Matrox code is the Matrox device's
# (hal/src/rg/mga*.c) and back end's (src/backend/mga); B1's check
# (docs/b1.md), run in RetroGPU's host tier.
cd "$(dirname "$0")/.." || exit 2
bad=$(grep -n '#include *["<]\(mga\|rg\)/' src/gl/*.[ch] src/dgl/*.[ch] |
      grep -v '"mga/\(sys\|fp\|prof\|serial\|types\)\.h"\|"rg/device\.h"')
if [ -n "$bad" ]; then
    echo "layers: card headers in DOS-GL's GL layer (docs/b1.md):"
    echo "$bad"
    exit 1
fi
echo "layers: the GL layer includes platform headers and rg/device.h only"

#!/bin/bash
# Compila as ferramentas da placa (estaticas, sem dependencias).
# Saida: out/pandora-coin
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/toolchain/armv7-eabihf--glibc--stable-2018.02-2"
export PATH="$TC/bin:$PATH"

CFLAGS="-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O2 -Wall -Wextra"

mkdir -p "$ROOT/out"
arm-linux-gcc $CFLAGS -static -o "$ROOT/out/pandora-coin" "$ROOT/tools/pandora-coin/pandora-coin.c"
arm-linux-strip "$ROOT/out/pandora-coin"
ls -la "$ROOT/out/pandora-coin"

# Core-trampolim: o RetroArch de fabrica carrega, ele abre o RetroArch novo.
arm-linux-gcc $CFLAGS -shared -fPIC \
    -I"$ROOT/src/retroarch/libretro-common/include" \
    -o "$ROOT/out/ra_trampolim_libretro.so" "$ROOT/tools/ra-trampolim/ra_trampolim.c" -ldl
arm-linux-strip "$ROOT/out/ra_trampolim_libretro.so"
ls -la "$ROOT/out/ra_trampolim_libretro.so"

# Relay de conquistas, lado da placa (CP2102 pelo usbfs).
arm-linux-gcc $CFLAGS -static -o "$ROOT/out/pandora-relay" "$ROOT/tools/ra-relay/pandora-relay.c"
arm-linux-strip "$ROOT/out/pandora-relay"
ls -la "$ROOT/out/pandora-relay"

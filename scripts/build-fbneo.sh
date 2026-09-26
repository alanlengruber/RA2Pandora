#!/bin/bash
# Compila o core FBNeo (libretro) para a Pandora Box DX (Cortex-A7, NEON).
# Saida: out/fbneo_libretro.so
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/toolchain/armv7-eabihf--glibc--stable-2018.02-2"
SRC="$ROOT/src/fbneo"

export PATH="$TC/bin:$PATH"

cd "$SRC/src/burner/libretro"
# O perfil rpi2 e o mesmo processador do RK3128 (Cortex-A7, neon-vfpv4, hard float).
make -j"$(nproc)" platform=unix-rpi2 \
    CC=arm-linux-gcc CXX=arm-linux-g++ AR=arm-linux-ar \
    "$@"

mkdir -p "$ROOT/out"
cp fbneo_libretro.so "$ROOT/out/fbneo_libretro.so"
arm-linux-strip "$ROOT/out/fbneo_libretro.so"
ls -la "$ROOT/out/fbneo_libretro.so"

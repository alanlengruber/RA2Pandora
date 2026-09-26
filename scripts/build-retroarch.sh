#!/bin/bash
# Compila o RetroArch para a Pandora Box DX (RK3128, Mali-400, glibc 2.26).
# Saida: out/retroarch
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/toolchain/armv7-eabihf--glibc--stable-2018.02-2"
SR="$TC/arm-buildroot-linux-gnueabihf/sysroot"
SRC="$ROOT/src/retroarch"

export PATH="$TC/bin:$PATH"
export PKG_CONFIG_SYSROOT_DIR="$SR"
export PKG_CONFIG_LIBDIR="$SR/usr/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export PKG_CONF_PATH=pkg-config

CPU_FLAGS="-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
# Headers do EGL sem X11 (a placa nao tem X11)
EGL_FLAGS="-DMESA_EGL_NO_X11_HEADERS -DEGL_NO_X11"
export CFLAGS="$CPU_FLAGS -O2 -pipe $EGL_FLAGS"
export CXXFLAGS="$CPU_FLAGS -O2 -pipe $EGL_FLAGS"
# rpath-link resolve as dependencias indiretas (libmali -> libwayland, libffi...)
export LDFLAGS="-Wl,-rpath-link,$SR/usr/lib -Wl,-rpath-link,$SR/lib"

cd "$SRC"

# Patches locais (aplica so os que ainda nao estao aplicados)
for p in "$ROOT"/patches/retroarch/*.patch; do
  if git apply --check "$p" 2>/dev/null; then
    git apply "$p" && echo "patch aplicado: $(basename "$p")"
  elif ! git apply --check -R "$p" 2>/dev/null; then
    echo "patch nao aplica: $(basename "$p")" >&2; exit 1
  fi
done

./configure \
    --host=arm-linux \
    --disable-x11 --disable-wayland --disable-sdl --disable-sdl2 --disable-qt \
    --disable-ffmpeg --disable-freetype --disable-pulse --disable-oss --disable-jack \
    --disable-vulkan --disable-opengl1 --disable-opengl_core --disable-cg \
    --disable-v4l2 --disable-libusb --disable-systemd --disable-discord \
    --disable-translate --disable-accessibility --disable-slang --disable-glslang \
    --disable-spirv_cross --disable-crtswitchres --disable-cdrom --disable-parport \
    --enable-opengl --enable-opengles --enable-egl --enable-kms \
    --enable-udev --enable-alsa --enable-zlib --enable-threads \
    --enable-networking --enable-cheevos --enable-ssl --enable-builtinmbedtls \
    --enable-command --enable-neon --enable-floathard \
    --enable-plain_drm --enable-networkgamepad

make -j"$(nproc)"

mkdir -p "$ROOT/out"
cp retroarch "$ROOT/out/retroarch"
arm-linux-strip "$ROOT/out/retroarch"
ls -la "$ROOT/out/retroarch"

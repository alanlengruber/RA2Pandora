#!/bin/bash
# Prepara o ambiente de compilacao para a Pandora Box DX (RK3128, Mali-400,
# glibc 2.26):
#   1. toolchain Bootlin armv7 (GCC 6.4 + glibc 2.26, igual ao da placa)
#   2. sysroot: bibliotecas da propria placa + headers dos pacotes Debian 9
#   3. codigo-fonte do RetroArch e do FBNeo nas versoes testadas
#
# Precisa de sysroot/rootfs.tar.gz: a copia do sistema da placa que o script
# Diagnostico2 grava no pendrive (diagnostico/rootfs.tar.gz).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC_NOME="armv7-eabihf--glibc--stable-2018.02-2"
TC="$ROOT/toolchain/$TC_NOME"
SR="$TC/arm-buildroot-linux-gnueabihf/sysroot"
BOARD="$ROOT/sysroot/board"

RETROARCH_TAG="v1.22.2"
FBNEO_COMMIT="aceeebed9e7edc8a28652365a064baee9a16e274"

DEBIAN="http://archive.debian.org/debian"
DEBS="libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev libudev-dev libasound2-dev zlib1g-dev mesa-common-dev"

if [ ! -f "$ROOT/sysroot/rootfs.tar.gz" ]; then
  echo "falta sysroot/rootfs.tar.gz (rode o Diagnostico2 na placa e copie diagnostico/rootfs.tar.gz)" >&2
  exit 1
fi

echo "== toolchain"
if [ ! -x "$TC/bin/arm-linux-gcc" ]; then
  mkdir -p "$ROOT/toolchain"
  curl -fsSL "https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/tarballs/$TC_NOME.tar.bz2" \
    | tar -xj -C "$ROOT/toolchain"
fi
"$TC/bin/arm-linux-gcc" --version | head -1

echo "== sistema da placa"
if [ ! -d "$BOARD/usr/lib" ]; then
  mkdir -p "$BOARD"
  tar -xzf "$ROOT/sysroot/rootfs.tar.gz" -C "$BOARD" 2>/dev/null || true
fi

if [ ! -f "$SR/.ra2pandora" ]; then
  echo "== headers (Debian 9 armhf)"
  DEB_DIR="$ROOT/sysroot/debs"
  mkdir -p "$DEB_DIR/x"
  curl -fsSL "$DEBIAN/dists/stretch/main/binary-armhf/Packages.gz" | gunzip > "$DEB_DIR/Packages"
  for p in $DEBS; do
    f=$(awk -v p="$p" '$1=="Package:"{x=($2==p)} x&&$1=="Filename:"{print $2; exit}' "$DEB_DIR/Packages")
    curl -fsSL -o "$DEB_DIR/$(basename "$f")" "$DEBIAN/$f"
  done
  for d in "$DEB_DIR"/*.deb; do
    t=$(mktemp -d)
    (cd "$t" && ar x "$d" && tar -xf data.tar.*)
    cp -a "$t/usr" "$DEB_DIR/x/"
    rm -rf "$t"
  done
  # Sem sobrescrever os headers da glibc do toolchain.
  cp -an "$DEB_DIR/x/usr/include/." "$SR/usr/include/" 2>/dev/null || true

  echo "== bibliotecas da placa no sysroot"
  for f in "$BOARD"/usr/lib/*.so* "$BOARD"/lib/*.so*; do
    b=$(basename "$f")
    [ -e "$SR/usr/lib/$b" ] || [ -e "$SR/lib/$b" ] || cp -a "$f" "$SR/usr/lib/"
  done
  # Links que apontavam para fora da pasta (ex.: ../../lib/libudev.so.1.6.3).
  (cd "$SR/usr/lib" && for l in $(find . -maxdepth 1 -xtype l); do
     t=$(basename "$(readlink "$l")"); [ -e "$t" ] && ln -sf "$t" "$l"; done)

  echo "== pkg-config"
  PC="$SR/usr/lib/pkgconfig"
  mkdir -p "$PC"
  for pc in "$DEB_DIR"/x/usr/lib/arm-linux-gnueabihf/pkgconfig/*.pc; do
    sed -e 's#/arm-linux-gnueabihf##g' "$pc" > "$PC/$(basename "$pc")"
  done
  # EGL, GLES e GBM vem do libmali da placa (sem X11, ao contrario do Mesa).
  for n in egl:EGL:1.4 glesv2:GLESv2:2.0 gbm:gbm:13.0.6; do
    IFS=: read -r name lib ver <<< "$n"
    cat > "$PC/$name.pc" <<EOF
prefix=/usr
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: $name
Description: $lib via libmali (Mali-400 r7p0)
Version: $ver
Libs: -L\${libdir} -l$lib
Cflags: -I\${includedir} -DMESA_EGL_NO_X11_HEADERS -DEGL_NO_X11
EOF
  done
  rm -f "$PC/wayland-egl.pc" "$PC/dri.pc"
  touch "$SR/.ra2pandora"
fi

echo "== codigo-fonte"
mkdir -p "$ROOT/src"
if [ ! -d "$ROOT/src/retroarch/.git" ]; then
  git clone -q --depth 1 --branch "$RETROARCH_TAG" https://github.com/libretro/RetroArch.git "$ROOT/src/retroarch"
fi
if [ ! -d "$ROOT/src/fbneo/.git" ]; then
  git init -q "$ROOT/src/fbneo"
  git -C "$ROOT/src/fbneo" fetch -q --depth 1 https://github.com/libretro/FBNeo.git "$FBNEO_COMMIT"
  git -C "$ROOT/src/fbneo" checkout -q FETCH_HEAD
fi
echo "RetroArch: $(git -C "$ROOT/src/retroarch" describe --tags)  FBNeo: $(git -C "$ROOT/src/fbneo" rev-parse --short HEAD)"
echo "pronto. Agora: scripts/build-retroarch.sh, scripts/build-fbneo.sh e scripts/build-tools.sh"

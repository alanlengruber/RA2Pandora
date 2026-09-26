#!/bin/bash
# Segunda coleta: como a placa pode falar com o PC (USB gadget, usbfs, UART).
# Grava em <pendrive>/diagnostico/info2.txt. Somente leitura.

ROOT=""
for d in /media/usb0 /tmp/udisk /udisk "$(dirname "$0")/../.."; do
  if [ -d "$d/roms_pandory" ]; then ROOT="$d"; break; fi
done
[ -z "$ROOT" ] && ROOT="/tmp"

OUT="$ROOT/diagnostico"
mkdir -p "$OUT"
exec > "$OUT/info2.txt" 2>&1
set -x

# USB gadget (placa se apresentando como dispositivo USB para o PC)
ls -la /sys/class/udc
cat /proc/filesystems
ls -la /sys/kernel/config
ls /proc/config.gz && zcat /proc/config.gz | grep -E "GADGET|CONFIGFS|USB_F_|USB_G_|DWC2|USB_SERIAL|USB_NET|USB_ACM|FTDI|CH341|CP210|PL2303|PPP|TUN|SLIP|DEVMEM"
find /proc/device-tree -name dr_mode | while read f; do echo "$f: $(cat "$f")"; done
ls /sys/bus/platform/devices | grep -iE "usb|otg"

# Acesso USB direto do userspace (usbfs)
ls -la /dev/bus /dev/bus/usb /dev/bus/usb/*

# UARTs internas
cat /proc/tty/driver/serial
cat /proc/tty/drivers
for t in /sys/class/tty/ttyS*; do echo "$t -> $(readlink -f "$t/device")"; done

# Kernel: mensagens de USB/UART
dmesg | grep -iE "usb|dwc|otg|gadget|udc|uart|ttyS|serial|fiq"

# Suporte de rede no kernel (PPP, TUN, SLIP)
ls -la /dev/net /dev/ppp
ls /sys/class/misc
cat /proc/net/protocols

# Como o Pandory chama o RetroArch
cat /usr/bin/start_game.sh
cat /usr/bin/start.sh
cat /tmp/retroarch
cat /usr/retroarch/retroarch.cfg
ps

# Bibliotecas relevantes para compilar um RetroArch novo
ls -la /lib/ld-* /lib/libc.so* /lib/libc-*
ls -la /usr/lib | grep -iE "mali|egl|gles|drm|gbm|wayland|asound|udev|z\.so|ssl|crypto"

set +x
sync

# Copia do sistema (somente leitura), para montar o ambiente de compilacao.
# Pode levar varios minutos.
rm -f "$OUT/concluido.txt"
cd / && tar -cf - bin sbin lib usr etc oem 2> "$OUT/rootfs_erros.txt" | gzip > "$OUT/rootfs.tar.gz"
ls -la "$OUT" > "$OUT/concluido.txt"
sync

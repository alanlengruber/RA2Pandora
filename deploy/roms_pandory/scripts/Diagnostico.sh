#!/bin/bash
# Coleta informacoes da placa e grava em <pendrive>/diagnostico/info.txt
# Somente leitura: nao altera nada no sistema da placa.

ROOT=""
for d in /media/usb0 /tmp/udisk /udisk "$(dirname "$0")/../.."; do
  if [ -d "$d/roms_pandory" ]; then ROOT="$d"; break; fi
done
[ -z "$ROOT" ] && ROOT="/tmp"

OUT="$ROOT/diagnostico"
mkdir -p "$OUT"
exec > "$OUT/info.txt" 2>&1
set -x

id
uname -a
cat /proc/cmdline
cat /proc/cpuinfo
head -5 /proc/meminfo

# RetroArch: versao e recursos compilados (cheevos, network commands)
/usr/bin/retroarch --version
/usr/bin/retroarch --features
ls -la /usr/bin /usr/lib/libretro /usr/retroarch
cat /tmp/retroarch.cfg

# Rede: interfaces e drivers disponiveis (USB-Ethernet, Wi-Fi, USB-serial)
ls /sys/class/net
ifconfig -a
cat /proc/net/dev
lsmod
find /lib/modules -name '*.ko'
ls /sys/bus/usb/drivers
cat /sys/kernel/debug/usb/devices
lsusb

# Ferramentas disponiveis (nc, telnetd, httpd, dropbear...)
busybox --list
which dropbear sshd telnetd nc socat python python3

ls /dev
mount
df
ps
ls /etc/init.d
cat /etc/inittab

set +x
sync

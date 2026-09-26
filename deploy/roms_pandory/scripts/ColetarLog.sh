#!/bin/bash
# Copia para o pendrive os registros do Pandory sobre o ultimo jogo aberto.
# Somente leitura: nao altera nada na placa.
# Saida: <pendrive>/pandora-ra/logs-pandory/

ROOT=""
for d in /media/usb0 /tmp/udisk /udisk "$(dirname "$0")/../.."; do
  if [ -d "$d/roms_pandory" ]; then ROOT="$d"; break; fi
done
[ -z "$ROOT" ] && exit 1

OUT="$ROOT/pandora-ra/logs-pandory"
mkdir -p "$OUT"

# "cp" falha ao gravar no pendrive nesta placa; "cat" funciona.
for f in pandory.txt pandory_rom pandory_root retro_tmp retroarch.cfg pandory-options.cfg retroarch retroarch_debug.cfg; do
  [ -f "/tmp/$f" ] && cat "/tmp/$f" > "$OUT/$f"
done
hexdump -C /tmp/pipe3 > "$OUT/pipe3.txt" 2>&1
ls -la /tmp /tmp/bin > "$OUT/tmp-ls.txt" 2>&1
mount > "$OUT/mount.txt" 2>&1
sync

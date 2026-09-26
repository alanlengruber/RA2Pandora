#!/bin/bash
# Teste do cabo com respostas grandes: baixa 3 vezes a lista de jogos de
# Arcade do RetroAchievements (~67 KB) pelo CP2102 -> Raspberry Pi e confere
# tamanho e conteudo. A tela fica parada por ate ~2 minutos.
# Log em <pendrive>/pandora-ra/logs/cabo.txt

ROOT=""
for d in /media/usb0 /tmp/udisk /udisk "$(dirname "$0")/../.."; do
  if [ -d "$d/roms_pandory" ]; then ROOT="$d"; break; fi
done
[ -z "$ROOT" ] && exit 1

RA="$ROOT/pandora-ra"
LOG="$RA/logs/cabo.txt"
mkdir -p "$RA/logs"
echo "== teste grande, inicio t=$(awk '{print $1}' /proc/uptime)" >> "$LOG"

ifconfig lo 127.0.0.1 up
"$RA/pandora-relay" -u -p 8080 >> "$LOG" 2>&1 &
RELAY=$!
sleep 2

for i in 1 2 3; do
  t0=$(awk '{printf "%d", $1 * 100}' /proc/uptime)
  rm -f /tmp/lista.json
  wget -q -O /tmp/lista.json "http://127.0.0.1:8080/dorequest.php?r=gameslist&c=27" 2>> "$LOG"
  t1=$(awk '{printf "%d", $1 * 100}' /proc/uptime)
  echo "tentativa $i: $(wc -c < /tmp/lista.json 2>/dev/null) bytes em $(( (t1 - t0) / 100 )).$(( (t1 - t0) % 100 ))s md5=$(md5sum /tmp/lista.json 2>/dev/null | cut -d' ' -f1)" >> "$LOG"
  sync
done

kill "$RELAY" 2>/dev/null
echo "== fim" >> "$LOG"
sync

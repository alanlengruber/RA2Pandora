#!/bin/bash
# Teste do cabo com respostas grandes: baixa 3 vezes a lista de jogos de
# Arcade do RetroAchievements (~67 KB) pelo CP2102 -> Raspberry Pi e confere
# tamanho e conteudo; depois mais 3 vezes com os 4 nucleos ocupados, como
# num jogo. A tela fica parada por ate ~3 minutos.
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

baixar() {
  t0=$(awk '{printf "%d", $1 * 100}' /proc/uptime)
  rm -f /tmp/lista.json
  wget -q -O /tmp/lista.json "http://127.0.0.1:8080/dorequest.php?r=gameslist&c=27" 2>> "$LOG"
  t1=$(awk '{printf "%d", $1 * 100}' /proc/uptime)
  echo "tentativa $1: $(wc -c < /tmp/lista.json 2>/dev/null) bytes em $(printf '%d.%02d' $(( (t1 - t0) / 100 )) $(( (t1 - t0) % 100 )))s md5=$(md5sum /tmp/lista.json 2>/dev/null | cut -d' ' -f1)" >> "$LOG"
  sync
}

for i in 1 2 3; do baixar "$i"; done

# Carga nos 4 nucleos, como o jogo faz.
CARGA=""
trap 'kill $CARGA "$RELAY" 2>/dev/null' EXIT
for n in 1 2 3 4; do
  ( while :; do :; done ) &
  CARGA="$CARGA $!"
done
for i in 1 2 3; do baixar "$i com carga"; done
kill $CARGA 2>/dev/null

kill "$RELAY" 2>/dev/null
echo "== fim" >> "$LOG"
sync

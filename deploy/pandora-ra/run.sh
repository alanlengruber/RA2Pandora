#!/bin/bash
# Roda um jogo no RetroArch novo.
# Uso: run.sh <core.so> <rom> [opcoes extras do retroarch]
#
# Tudo que o RetroArch grava fica dentro desta pasta. Durante o jogo a CPU
# fica na frequencia maxima e o pandora-coin repassa as moedas; ao sair, tudo
# volta ao estado anterior.

RA="$(cd "$(dirname "$0")" && pwd)"
CORE="$1"
ROM="$2"
shift 2

mkdir -p "$RA/logs" "$RA/config" "$RA/saves" "$RA/states" "$RA/system" "$RA/playlists" "$RA/cache"

export HOME="$RA"                  # le $RA/.asoundrc (dispositivo "pandora")
export XDG_CONFIG_HOME="$RA/config"
export XDG_RUNTIME_DIR=/tmp

# Segurar Start sai do jogo, entao o FBNeo nao pode usar o mesmo gesto para
# abrir o menu de servico do jogo. ROMs modificadas ("patched") ficam
# desligadas: o modo hardcore do RetroAchievements pausa as conquistas se
# essa opcao estiver ligada (padrao do FBNeo).
cat > "$RA/core-options.cfg" <<EOF
fbneo-samplerate = "44100"
fbneo-diagnostic-input = "None"
fbneo-allow-patched-romsets = "disabled"
EOF

# Samples (sons gravados, como o pulo do Donkey Kong): o FBNeo procura em
# system/fbneo/samples; os do pendrive ficam em roms/samples. O exFAT nao tem
# links simbolicos, entao montamos a pasta durante o jogo.
SAMPLES_SRC="$RA/../roms/samples"
SAMPLES_DST="$RA/system/fbneo/samples"
SAMPLES_MONTADO=""
if [ -d "$SAMPLES_SRC" ]; then
  mkdir -p "$SAMPLES_DST"
  if ! mountpoint -q "$SAMPLES_DST"; then
    mount -o bind "$SAMPLES_SRC" "$SAMPLES_DST" && SAMPLES_MONTADO=1
  fi
fi

GOV=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
GOV_ORIGINAL=$(cat "$GOV")
COIN=""
RELAY=""
restore() {
  [ -n "$COIN" ] && kill "$COIN" 2>/dev/null
  [ -n "$RELAY" ] && kill "$RELAY" 2>/dev/null
  echo "$GOV_ORIGINAL" > "$GOV"
  [ -n "$SAMPLES_MONTADO" ] && umount "$SAMPLES_DST"
}
trap restore EXIT
echo performance > "$GOV"

# O loopback (127.0.0.1) vem desligado nesta placa; sem ele as moedas do
# pandora-coin nao chegam ao RetroArch. Volta ao normal ao desligar a placa.
ifconfig lo 127.0.0.1 up

# Conquistas: so com pandora-ra/conta.cfg (login do RetroAchievements). O
# relay leva as chamadas do RetroArch pelo CP2102 ate o Raspberry Pi.
# RA_APPEND (opcional) e mais um config por cima. O RetroArch so aceita um
# --appendconfig, entao os dois vao juntos, separados por "|".
APPEND=""
if [ -f "$RA/conta.cfg" ]; then
  # Copia sem "\r": o arquivo pode ter sido criado no Windows (CRLF).
  tr -d '\r' < "$RA/conta.cfg" > /tmp/conta.cfg
  APPEND=/tmp/conta.cfg
  "$RA/pandora-relay" -u > "$RA/logs/relay.txt" 2>&1 &
  RELAY=$!
fi
# Jogos verticais: proporcao 3:4 (vertical.cfg).
if grep -qxF "$(basename "$ROM")" "$RA/verticais.txt" 2>/dev/null; then
  APPEND="${APPEND:+$APPEND|}$RA/vertical.cfg"
fi
[ -n "$RA_APPEND" ] && APPEND="${APPEND:+$APPEND|}$RA_APPEND"
EXTRA_CFG=""
[ -n "$APPEND" ] && EXTRA_CFG="--appendconfig=$APPEND"

"$RA/retroarch" --verbose -c "$RA/retroarch.cfg" $EXTRA_CFG -L "$CORE" "$ROM" "$@" > "$RA/logs/retroarch.txt" 2>&1 &
PID=$!

"$RA/pandora-coin" -w "$PID" > "$RA/logs/coin.txt" 2>&1 &
COIN=$!

wait "$PID"
RC=$?
echo "== saida: $RC" >> "$RA/logs/retroarch.txt"
sync
exit "$RC"

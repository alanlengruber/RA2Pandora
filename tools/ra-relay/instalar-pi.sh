#!/bin/bash
# Instala o ra-relay-pi no Raspberry Pi 3 (Raspberry Pi OS).
# Rode no Pi, nesta pasta:  sudo ./instalar-pi.sh
#
# 1. Libera a UART dos pinos 8 e 10 (GPIO14/15): no Pi 3 ela fica com o
#    Bluetooth; "dtoverlay=disable-bt" a devolve como /dev/ttyAMA0.
# 2. Tira o console do Linux dessa UART (senao ele escreve no cabo).
# 3. Instala o relay como servico systemd, que sobe sozinho no boot.
# Faz copia de seguranca de cada arquivo do sistema que altera (*.antes-ra).
set -euo pipefail

if [ "$(id -u)" != 0 ]; then
  echo "rode com sudo: sudo $0" >&2
  exit 1
fi
AQUI="$(cd "$(dirname "$0")" && pwd)"

# Bookworm usa /boot/firmware; versoes anteriores, /boot.
BOOT=/boot/firmware
[ -f "$BOOT/config.txt" ] || BOOT=/boot
CONFIG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"

backup() { [ -f "$1.antes-ra" ] || cp -a "$1" "$1.antes-ra"; }

echo "== UART (pinos 8/10) em $CONFIG"
backup "$CONFIG"
grep -q '^enable_uart=1' "$CONFIG" || echo 'enable_uart=1' >> "$CONFIG"
grep -q '^dtoverlay=disable-bt' "$CONFIG" || echo 'dtoverlay=disable-bt' >> "$CONFIG"
systemctl disable --now hciuart.service 2>/dev/null || true

echo "== console serial fora da UART ($CMDLINE)"
backup "$CMDLINE"
sed -i -E 's/ ?console=(serial0|ttyAMA0|ttyS0),[0-9]+//g' "$CMDLINE"
systemctl disable --now serial-getty@ttyAMA0.service serial-getty@serial0.service 2>/dev/null || true

echo "== servico ra-relay"
install -m 755 "$AQUI/ra-relay-pi.py" /usr/local/bin/ra-relay-pi
cat > /etc/systemd/system/ra-relay.service <<'EOF'
[Unit]
Description=Relay de conquistas da Pandora Box (serial -> RetroAchievements)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 /usr/local/bin/ra-relay-pi --porta /dev/ttyAMA0 --baud 921600
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable ra-relay.service

echo
echo "Pronto. Reinicie o Pi (sudo reboot) para a UART ser liberada."
echo "Depois: 'systemctl status ra-relay' e 'journalctl -u ra-relay -f' mostram o relay."

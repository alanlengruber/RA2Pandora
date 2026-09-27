#!/bin/bash
# Gera deploy/pandora-ra/retroarch.cfg: config de fabrica da placa (controles,
# audio) + deploy/pandora-ra/override.cfg + caminhos dentro de pandora-ra.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STOCK="$ROOT/sysroot/board/usr/retroarch/retroarch.cfg"
OVERRIDE="$ROOT/deploy/pandora-ra/override.cfg"
OUT="$ROOT/deploy/pandora-ra/retroarch.cfg"
RA="/media/usb0/pandora-ra"

PATHS=$(cat <<EOF
savefile_directory = "$RA/saves"
savestate_directory = "$RA/states"
system_directory = "$RA/system"
assets_directory = "$RA/assets"
playlist_directory = "$RA/playlists"
cache_directory = "$RA/cache"
rgui_config_directory = "$RA"
core_options_path = "$RA/core-options.cfg"
history_list_enable = "false"
EOF
)

# Chaves redefinidas saem do config de fabrica, para nao haver duplicatas.
KEYS=$( { grep -E '^[a-z0-9_]+ *=' "$OVERRIDE"; echo "$PATHS"; } | sed -E 's/ *=.*//' | sort -u)
{
  grep -vE "^($(echo "$KEYS" | paste -sd'|')) *=" "$STOCK"
  echo
  grep -E '^[a-z0-9_]+ *=' "$OVERRIDE"
  echo "$PATHS"
} > "$OUT"

echo "gerado: $OUT ($(wc -l < "$OUT") linhas)"

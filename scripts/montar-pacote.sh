#!/bin/bash
# Monta pacote/, com a mesma estrutura da raiz do pendrive da Pandora:
#   pandora-ra/            RetroArch novo, FBNeo, relay, moedas e configs
#   pandory/cores/         o core-trampolim
#   pandory/pandory.xml    o seu pandory.xml com as regras dos jogos
#   roms_pandory/          atalhos "(RA)" e scripts de diagnostico
# Depois e so copiar o conteudo de pacote/ para a raiz do pendrive.
#
# Antes: scripts de build, cruzar-roms.py, e o seu pandory.xml original em
# deploy/pandory/pandory.xml.original.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/pacote"

falta() { echo "falta $1 ($2)" >&2; exit 1; }
for f in retroarch fbneo_libretro.so pandora-coin pandora-relay ra_trampolim_libretro.so; do
  [ -f "$ROOT/out/$f" ] || falta "out/$f" "rode os scripts de build"
done
[ -f "$ROOT/data/cruzamento-roms.tsv" ] || falta "data/cruzamento-roms.tsv" "rode scripts/cruzar-roms.py"
[ -f "$ROOT/deploy/pandory/pandory.xml.original" ] || \
  falta "deploy/pandory/pandory.xml.original" "copie o pandory.xml do seu pendrive"

"$ROOT/scripts/make-config.sh"
python3 "$ROOT/scripts/gerar-regras.py"
python3 "$ROOT/scripts/gerar-atalhos.py"

rm -rf "$P"
mkdir -p "$P/pandora-ra/cores" "$P/pandora-ra/raspberry-pi" "$P/pandory/cores" "$P/roms_pandory/scripts"

install -m 755 "$ROOT/out/retroarch" "$ROOT/out/pandora-coin" "$ROOT/out/pandora-relay" "$P/pandora-ra/"
install -m 644 "$ROOT/out/fbneo_libretro.so" "$P/pandora-ra/cores/"
D="$ROOT/deploy/pandora-ra"
install -m 755 "$D/run.sh" "$P/pandora-ra/"
install -m 644 "$D/retroarch.cfg" "$D/.asoundrc" "$D/vertical.cfg" "$D/verticais.txt" \
  "$D/jogos.txt" "$D/conta.cfg.exemplo" "$P/pandora-ra/"
# Lado do Raspberry Pi, para levar pelo pendrive.
install -m 644 "$ROOT/tools/ra-relay/ra-relay-pi.py" "$ROOT/tools/ra-relay/instalar-pi.sh" \
  "$ROOT/tools/ra-relay/PROTOCOLO.md" "$P/pandora-ra/raspberry-pi/"

install -m 644 "$ROOT/out/ra_trampolim_libretro.so" "$P/pandory/cores/"
install -m 644 "$ROOT/deploy/pandory/pandory.xml" "$P/pandory/"

cp -a "$ROOT/deploy/roms_pandory/conquistas" "$P/roms_pandory/"
for s in ColetarLog Diagnostico Diagnostico2 TesteCabo; do
  install -m 755 "$ROOT/deploy/roms_pandory/scripts/$s.sh" "$P/roms_pandory/scripts/"
done

echo "pacote pronto em $P ($(find "$P" -type f | wc -l) arquivos)"

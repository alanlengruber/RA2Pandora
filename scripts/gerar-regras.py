#!/usr/bin/env python3
"""Gera deploy/pandory/pandory.xml e deploy/pandora-ra/jogos.txt.

Parte do pandory.xml original (deploy/pandory/pandory.xml.original) e manda
para o RetroArch novo (core ra_trampolim) os jogos de data/cruzamento-roms.tsv
que sao compativeis com o FBNeo atual e tem conquistas oficiais. Os jogos
verticais tambem entram: o driver de video "drm" (com patch) gira a imagem.
"""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORIGINAL = ROOT / "deploy/pandory/pandory.xml.original"
SAIDA_XML = ROOT / "deploy/pandory/pandory.xml"
SAIDA_JOGOS = ROOT / "deploy/pandora-ra/jogos.txt"
SAIDA_VERTICAIS = ROOT / "deploy/pandora-ra/verticais.txt"
CRUZAMENTO = ROOT / "data/cruzamento-roms.tsv"
CORE = "ra_trampolim"

linhas = [r for r in csv.DictReader(CRUZAMENTO.open(), delimiter="\t")
          if r["fbneo"] == "OK" and r["conquistas"] == "sim"]
jogos = sorted(r["rom"] for r in linhas)
verticais = sorted(r["rom"] for r in linhas if r["vertical"] == "sim")

xml = ORIGINAL.read_text()
existentes = set()

def com_core(m):
    tag, nome = m.group(0), m.group(1)
    if nome not in jogos_set:
        return tag
    existentes.add(nome)
    if re.search(r'\bcore="[^"]*"', tag):
        return re.sub(r'\bcore="[^"]*"', f'core="{CORE}"', tag)
    return tag.replace(f'name="{nome}"', f'name="{nome}" core="{CORE}"', 1)

jogos_set = set(jogos)
xml = re.sub(r'<rom\s+name="([^"]+)"[^>]*/>', com_core, xml)

novos = [j for j in jogos if j not in existentes]
bloco = ("\n        <!-- RetroArch novo (pandora-ra), para as conquistas do RetroAchievements.\n"
         "             Gerado por scripts/gerar-regras.py -->\n"
         + "".join(f'        <rom name="{j}" core="{CORE}" />\n' for j in novos))
assert xml.count("    </roms>\n") == 1
xml = xml.replace("    </roms>\n", bloco + "    </roms>\n")

# Secao "RetroAchievements" do menu: os atalhos de roms_pandory/conquistas.
EMULADOR = f'        <emulator suffix="RA" folder="conquistas" core="{CORE}" />\n'
assert xml.count("    </emulators>\n") == 1
xml = xml.replace("    </emulators>\n", EMULADOR + "    </emulators>\n")

SAIDA_XML.write_text(xml)
SAIDA_JOGOS.write_text("".join(j + "\n" for j in jogos))
# O run.sh aplica o vertical.cfg (proporcao 3:4) a estes jogos.
SAIDA_VERTICAIS.write_text("".join(j + "\n" for j in verticais))
print(f"{len(jogos)} jogos: {len(existentes)} regras existentes ajustadas, {len(novos)} regras novas")

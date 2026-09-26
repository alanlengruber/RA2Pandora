#!/usr/bin/env python3
"""Gera os atalhos de deploy/roms_pandory/conquistas/ (a secao
"RetroAchievements" do menu do Pandory).

Cada atalho e um arquivo pequeno com o titulo do jogo no RetroAchievements e o
conteudo "retroachievements:<romset>.zip"; o ra_trampolim le o conteudo e abre
a ROM real de <pendrive>/roms/.

Uso: gerar-atalhos.py [romset.zip ...]   (sem argumentos: todos de jogos.txt)
"""
import csv
import re
import shutil
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PASTA = ROOT / "deploy/roms_pandory/conquistas"
JOGOS = ROOT / "deploy/pandora-ra/jogos.txt"
CRUZAMENTO = ROOT / "data/cruzamento-roms.tsv"

titulos = {r["rom"]: r["ra_titulo"] for r in csv.DictReader(CRUZAMENTO.open(), delimiter="\t")}
roms = sys.argv[1:] or JOGOS.read_text().split()

def nome_de_arquivo(titulo):
    # A fonte do menu da placa so tem ASCII: "Astérix" -> "Asterix".
    t = unicodedata.normalize("NFKD", titulo).encode("ascii", "ignore").decode()
    # exFAT nao aceita \ / : * ? " < > |
    t = t.replace(": ", " - ").replace(":", " -").replace("/", "-")
    t = re.sub(r'[\\*?"<>|]', "", t)
    return re.sub(r"\s+", " ", t).strip().rstrip(".")

if PASTA.exists():
    shutil.rmtree(PASTA)
PASTA.mkdir(parents=True)

usados = {}
for rom in sorted(roms, key=lambda r: titulos.get(r, r).lower()):
    nome = nome_de_arquivo(titulos.get(rom) or rom[:-4])
    # Titulos repetidos (versoes do mesmo jogo): diferencia pelo romset.
    if nome.lower() in usados:
        nome = f"{nome} ({rom[:-4]})"
    usados[nome.lower()] = rom
    (PASTA / f"{nome}.zip").write_text(f"retroachievements:{rom}\n")

print(f"{len(usados)} atalhos em {PASTA}")

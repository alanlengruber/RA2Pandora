#!/usr/bin/env python3
"""Cruza as ROMs de arcade do pendrive com o FBNeo atual e o RetroAchievements.

Para cada zip que o FBNeo reconhece, diz se o romset e compativel (todos os
arquivos exigidos presentes, pelo CRC), se o jogo tem conquistas oficiais no
RetroAchievements e se e vertical. O resultado (data/cruzamento-roms.tsv) e a
entrada do gerar-regras.py e do gerar-atalhos.py.

Uso:
  cruzar-roms.py --roms /caminho/para/roms          (le os zips direto)
  cruzar-roms.py --zips zips.tsv                    (lista do listar-zips.ps1)

Precisa do codigo do FBNeo em src/fbneo (scripts/preparar-ambiente.sh).
"""
import argparse
import collections
import csv
import hashlib
import json
import re
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RA_API = "https://retroachievements.org/dorequest.php?r={}&c=27"  # 27 = Arcade


def crcs_da_pasta(pasta):
    zips = collections.defaultdict(set)
    for z in sorted(Path(pasta).glob("*.zip")):
        try:
            with zipfile.ZipFile(z) as f:
                for info in f.infolist():
                    zips[z.stem.lower()].add(f"{info.CRC:08x}")
        except (zipfile.BadZipFile, OSError):
            pass
    return zips


def crcs_do_tsv(arquivo):
    zips = collections.defaultdict(set)
    for linha in open(arquivo, encoding="utf-8", errors="replace"):
        campos = linha.rstrip("\n").split("\t")
        if len(campos) == 4 and campos[0].lower().endswith(".zip"):
            zips[campos[0][:-4].lower()].add(campos[2].lower())
    return zips


def dados_do_ra(cache, sem_rede):
    """hashlibrary, officialgameslist e gameslist de Arcade (APIs publicas)."""
    dados = {}
    for nome in ("hashlibrary", "officialgameslist", "gameslist"):
        arquivo = cache / f"ra-{nome}.json"
        if not sem_rede:
            req = urllib.request.Request(RA_API.format(nome), headers={"User-Agent": "RA2Pandora"})
            arquivo.write_bytes(urllib.request.urlopen(req, timeout=60).read())
        dados[nome] = json.loads(arquivo.read_text())
    return (dados["hashlibrary"]["MD5List"], dados["officialgameslist"]["Response"],
            dados["gameslist"]["Response"])


def opcionais_e_verticais(fbneo):
    """CRCs marcados BRF_OPT/BRF_NODUMP e jogos verticais, direto dos drivers."""
    opcionais, verticais = set(), set()
    rom = re.compile(r'\{\s*"[^"]+"\s*,\s*0x[0-9a-fA-F]+\s*,\s*0x([0-9a-fA-F]+)\s*,([^}]*)\}')
    driver = re.compile(r"struct\s+BurnDriver\w*\s+\w+\s*=\s*\{(.*?)\};", re.S)
    for arquivo in (fbneo / "src/burn/drv").rglob("*.cpp"):
        texto = arquivo.read_text(errors="replace")
        for m in rom.finditer(texto):
            if "BRF_OPT" in m.group(2) or "BRF_NODUMP" in m.group(2):
                opcionais.add(m.group(1).lower().zfill(8))
        for m in driver.finditer(texto):
            nome = re.search(r'"([^"]+)"', m.group(1))
            if nome and "BDF_ORIENTATION_VERTICAL" in m.group(1):
                verticais.add(nome.group(1))
    return opcionais, verticais


def main():
    ap = argparse.ArgumentParser(description="Cruza ROMs com o FBNeo e o RetroAchievements.")
    fonte = ap.add_mutually_exclusive_group(required=True)
    fonte.add_argument("--roms", help="pasta com os zips (ex.: /mnt/h/roms)")
    fonte.add_argument("--zips", help="TSV gerado pelo listar-zips.ps1")
    ap.add_argument("--fbneo", default=str(ROOT / "src/fbneo"), help="codigo do FBNeo")
    ap.add_argument("--saida", default=str(ROOT / "data/cruzamento-roms.tsv"))
    ap.add_argument("--sem-rede", action="store_true", help="reusa os JSON do RA ja baixados")
    args = ap.parse_args()

    saida = Path(args.saida)
    saida.parent.mkdir(parents=True, exist_ok=True)
    fbneo = Path(args.fbneo)

    zips = crcs_da_pasta(args.roms) if args.roms else crcs_do_tsv(args.zips)
    hashes, oficiais, titulos = dados_do_ra(saida.parent, args.sem_rede)
    opcionais, verticais = opcionais_e_verticais(fbneo)
    dat = ET.parse(fbneo / "dats/FinalBurn Neo (ClrMame Pro XML, Arcade only).dat").getroot()
    jogos = {g.get("name"): g for g in dat.iter("game")}

    def faltando(nome):
        """Arquivos exigidos que nao estao no zip, no pai nem na BIOS."""
        g = jogos[nome]
        presentes, bios, vistos = set(zips[nome]), None, {nome}
        for chave in ("cloneof", "romof"):
            n = g.get(chave)
            while n and n in jogos and n not in vistos:
                vistos.add(n)
                if jogos[n].get("isbios") == "yes":
                    bios = n
                presentes |= zips.get(n, set())
                n = jogos[n].get("cloneof") or jogos[n].get("romof")
        falta = []
        for r in g.findall("rom"):
            crc = (r.get("crc") or "").lower()
            if not crc or r.get("status") == "nodump" or crc in opcionais or crc in presentes:
                continue
            # Variantes de BIOS (Neo Geo etc.): basta o zip da BIOS existir.
            if bios and r.get("merge") and bios in zips and any(
                    x.get("name") == r.get("merge") for x in jogos[bios].findall("rom")):
                continue
            falta.append(r.get("name"))
        return falta

    linhas = []
    for nome in sorted(zips):
        if nome not in jogos:
            continue
        ra_id = hashes.get(hashlib.md5(nome.encode()).hexdigest())
        falta = faltando(nome)
        linhas.append({
            "rom": nome + ".zip",
            "jogo": jogos[nome].findtext("description"),
            "fbneo": "OK" if not falta else "faltam: " + " ".join(falta[:4]),
            "ra_id": ra_id or "",
            "conquistas": "sim" if ra_id is not None and str(ra_id) in oficiais
                          else ("sem conjunto" if ra_id else "nao"),
            "ra_titulo": titulos.get(str(ra_id), "") if ra_id else "",
            "vertical": "sim" if nome in verticais else "nao",
        })

    with saida.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0]), delimiter="\t")
        w.writeheader()
        w.writerows(linhas)

    ok = [l for l in linhas if l["fbneo"] == "OK"]
    alvo = [l for l in ok if l["conquistas"] == "sim"]
    print(f"{len(zips)} zips; {len(linhas)} reconhecidos pelo FBNeo; {len(ok)} compativeis; "
          f"{len(alvo)} compativeis e com conquistas ({sum(l['vertical'] == 'sim' for l in alvo)} verticais)")
    print(f"resultado: {saida}")


if __name__ == "__main__":
    main()

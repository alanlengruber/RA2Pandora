#!/usr/bin/env python3
"""Lado do Raspberry Pi do relay de conquistas (ver PROTOCOLO.md).

Recebe pela serial as requisicoes HTTP que o RetroArch da Pandora fez ao
pandora-relay, repassa ao RetroAchievements por HTTPS e devolve a resposta.

Uso: ra-relay-pi.py [--porta /dev/ttyAMA0] [--baud 921600]
So usa a biblioteca padrao do Python 3.
"""
import argparse
import collections
import http.client
import logging
import os
import select
import struct
import termios
import tty
import zlib

PEDIDO, RESPOSTA, PING, PONG = 1, 2, 3, 4
PEDIDO_PEDACOS, TAMANHO, LER, PEDACO = 5, 6, 7, 8
HEADER = struct.Struct("<2sBBHI")  # magia, versao, tipo, id, tamanho
MAX_PAYLOAD = 4 * 1024 * 1024
# Respostas ate esse tamanho vao num quadro so, mesmo em PEDIDO_PEDACOS.
PEDACO_MAX = 1024
OFFSET_DESCONHECIDO = 0xFFFFFFFF

API_HOST = "retroachievements.org"
MEDIA_HOST = "media.retroachievements.org"
# Cabecalhos repassados ao servidor. O User-Agent do RetroArch e o que
# identifica o cliente para o RetroAchievements (e o modo hardcore).
REPASSAR = ("user-agent", "content-type", "accept")

BAUDS = {115200: termios.B115200, 230400: termios.B230400,
         460800: termios.B460800, 921600: termios.B921600}

log = logging.getLogger("ra-relay-pi")


def abrir_serial(porta, baud):
    fd = os.open(porta, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[2] &= ~termios.CRTSCTS
    attrs[4] = attrs[5] = BAUDS[baud]
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def montar_quadro(tipo, ident, conteudo=b""):
    cab = HEADER.pack(b"RA", 1, tipo, ident, len(conteudo))
    return cab + conteudo + struct.pack("<I", zlib.crc32(cab + conteudo))


def quadro_em(buf, pos):
    """Quadro em buf[pos]: (1, total) valido, (0, 0) incompleto, (-1, 0) invalido."""
    if len(buf) - pos < 2:
        return 0, 0
    if buf[pos:pos + 2] != b"RA":
        return -1, 0
    if len(buf) - pos < HEADER.size:
        return 0, 0
    _, versao, _, _, tamanho = HEADER.unpack_from(buf, pos)
    if versao != 1 or tamanho > MAX_PAYLOAD:
        return -1, 0
    total = HEADER.size + tamanho + 4
    if len(buf) - pos < total:
        return 0, 0
    (crc,) = struct.unpack_from("<I", buf, pos + HEADER.size + tamanho)
    if zlib.crc32(bytes(buf[pos:pos + HEADER.size + tamanho])) != crc:
        return -1, 0
    return 1, total


def extrair_quadros(buf):
    """Tira quadros validos do inicio de buf (bytearray); devolve a lista."""
    quadros = []
    while True:
        r, total = quadro_em(buf, 0)
        if r < 0:
            del buf[0]
            continue
        if r == 0:
            # Um falso cabecalho (ruido) pode anunciar um tamanho enorme e
            # prender os quadros de verdade que vem depois: se ja houver um
            # quadro completo e valido mais adiante, o lixo antes dele sai.
            j = buf.find(b"RA", 1)
            while j != -1 and quadro_em(buf, j)[0] != 1:
                j = buf.find(b"RA", j + 1)
            if j == -1:
                break
            del buf[:j]
            continue
        _, _, tipo, ident, tamanho = HEADER.unpack_from(buf)
        quadros.append((tipo, ident, bytes(buf[HEADER.size:HEADER.size + tamanho])))
        del buf[:total]
    return quadros


def escrever(fd, dados):
    while dados:
        dados = dados[os.write(fd, dados):]


def resposta_http(status, motivo, corpo, tipo="text/plain"):
    cab = (f"HTTP/1.0 {status} {motivo}\r\nContent-Type: {tipo}\r\n"
           f"Content-Length: {len(corpo)}\r\nConnection: close\r\n\r\n")
    return cab.encode("latin-1") + corpo


def atender(pedido):
    """Faz a chamada real ao RetroAchievements e devolve a resposta HTTP."""
    cabecalho, _, corpo = pedido.partition(b"\r\n\r\n")
    linhas = cabecalho.decode("latin-1").split("\r\n")
    try:
        metodo, caminho, _ = linhas[0].split(" ", 2)
    except ValueError:
        return resposta_http(400, "Bad Request", b"pedido invalido\n")

    cabecalhos = {}
    for linha in linhas[1:]:
        nome, _, valor = linha.partition(":")
        if nome.strip().lower() in REPASSAR:
            cabecalhos[nome.strip()] = valor.strip()

    # API em retroachievements.org; imagens (/Badge, /UserPic...) no media.
    host = API_HOST if caminho.startswith("/dorequest.php") else MEDIA_HOST
    try:
        conn = http.client.HTTPSConnection(host, timeout=20)
        conn.request(metodo, caminho, body=corpo or None, headers=cabecalhos)
        r = conn.getresponse()
        dados = r.read()
        tipo = r.getheader("Content-Type", "application/octet-stream")
        conn.close()
    except (OSError, http.client.HTTPException) as e:
        log.warning("%s %s: falhou (%s)", metodo, caminho.split("?")[0], e)
        return resposta_http(502, "Bad Gateway", f"relay: {e}\n".encode())

    log.info("%s %s -> %d (%d bytes)", metodo, caminho.split("?")[0], r.status, len(dados))
    return resposta_http(r.status, r.reason, dados, tipo)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--porta", default="/dev/ttyAMA0")
    ap.add_argument("--baud", type=int, default=921600, choices=sorted(BAUDS))
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")

    fd = abrir_serial(args.porta, args.baud)
    log.info("escutando em %s a %d baud", args.porta, args.baud)
    buf = bytearray()
    # Ultimas respostas, por (id, crc do pedido). A placa reenvia um pedido
    # quando a resposta se perde no cabo; o reenvio recebe a mesma resposta,
    # sem refazer a chamada (uma conquista nao e enviada duas vezes). Os
    # pedacos de uma resposta grande tambem saem daqui.
    recentes = collections.OrderedDict()

    def resposta_para(ident, conteudo):
        chave = (ident, zlib.crc32(conteudo))
        if chave in recentes:
            log.info("pedido %d repetido: reenviando a resposta", ident)
            recentes.move_to_end(chave)
        else:
            recentes[chave] = atender(conteudo)
            while len(recentes) > 16:
                recentes.popitem(last=False)
        return recentes[chave]

    def por_id(ident):
        # A mais recente com esse id (o mesmo id pode sobrar de uma execucao
        # anterior da placa; a do pedido atual acabou de ir para o fim).
        for (i, _), resposta in reversed(recentes.items()):
            if i == ident:
                return resposta
        return None

    ultimo_ler = None
    while True:
        select.select([fd], [], [])
        buf += os.read(fd, 65536)
        if len(buf) > 2 * MAX_PAYLOAD:
            del buf[:]
        for tipo, ident, conteudo in extrair_quadros(buf):
            if tipo == PING:
                escrever(fd, montar_quadro(PONG, ident))
            elif tipo == PEDIDO:
                escrever(fd, montar_quadro(RESPOSTA, ident, resposta_para(ident, conteudo)))
            elif tipo == PEDIDO_PEDACOS:
                resposta = resposta_para(ident, conteudo)
                if len(resposta) <= PEDACO_MAX:
                    escrever(fd, montar_quadro(RESPOSTA, ident, resposta))
                else:
                    escrever(fd, montar_quadro(TAMANHO, ident, struct.pack(
                        "<II", len(resposta), zlib.crc32(resposta))))
            elif tipo == LER and len(conteudo) == 6:
                offset, n = struct.unpack("<IH", conteudo)
                resposta = por_id(ident)
                if resposta is None or offset > len(resposta):
                    log.warning("pedido %d: pedaco de uma resposta que nao existe mais", ident)
                    escrever(fd, montar_quadro(PEDACO, ident, struct.pack("<I", OFFSET_DESCONHECIDO)))
                    continue
                if ultimo_ler == (ident, offset):
                    log.info("pedido %d: pedaco %d pedido de novo", ident, offset // PEDACO_MAX)
                ultimo_ler = (ident, offset)
                escrever(fd, montar_quadro(PEDACO, ident,
                                           struct.pack("<I", offset) + resposta[offset:offset + n]))


if __name__ == "__main__":
    main()

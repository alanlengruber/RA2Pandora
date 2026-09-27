#!/usr/bin/env python3
"""Gera o som de conquista (unlock.wav) que o RetroArch toca ao desbloquear.

Um "ding-ding" sintetizado aqui mesmo, para nao depender de nenhum som com
direitos autorais. Para usar outro som, troque pandora-ra/assets/sounds/unlock.wav
no pendrive por um .wav ou .ogg com o mesmo nome (unlock).

Uso: gerar-som-conquista.py <saida.wav>
"""
import math
import struct
import sys
import wave

TAXA = 44100          # a mesma do audio da placa
PICO = 0.5            # ~ -6 dBFS: soma com o som do jogo sem estourar

# (inicio em s, frequencia em Hz): duas notas subindo, como um sino.
NOTAS = ((0.00, 1318.51), (0.11, 1975.53))  # Mi6, Si6
DURACAO = 1.0


def sino(t, freq):
    """Nota de sino: harmonicos que somem depressa, ataque de 4 ms."""
    if t < 0:
        return 0.0
    ataque = min(1.0, t / 0.004)
    som = (math.sin(2 * math.pi * freq * t) * math.exp(-t / 0.30)
           + 0.35 * math.sin(2 * math.pi * 2 * freq * t) * math.exp(-t / 0.12)
           + 0.12 * math.sin(2 * math.pi * 3 * freq * t) * math.exp(-t / 0.06))
    return ataque * som


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    n = int(DURACAO * TAXA)
    amostras = [sum(sino(i / TAXA - inicio, f) for inicio, f in NOTAS) for i in range(n)]
    # Fim suave nos ultimos 50 ms, para nao estalar.
    for i in range(int(0.05 * TAXA)):
        amostras[n - 1 - i] *= i / (0.05 * TAXA)
    escala = PICO / max(abs(a) for a in amostras)
    with wave.open(sys.argv[1], "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(TAXA)
        w.writeframes(b"".join(struct.pack("<h", round(a * escala * 32767)) for a in amostras))


if __name__ == "__main__":
    main()

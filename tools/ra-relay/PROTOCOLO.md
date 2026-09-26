# Protocolo serial do relay

Liga o RetroArch da Pandora ao RetroAchievements passando por um cabo serial
(CP2102 na placa, UART do Raspberry Pi do outro lado):

```
RetroArch --HTTP--> pandora-relay (127.0.0.1:8080) ==serial==> ra-relay-pi.py --HTTPS--> retroachievements.org
```

O RetroArch usa `cheevos_custom_host = "http://127.0.0.1:8080"`. Com servidor
proprio, ele pede tudo a esse endereco: a API (`/dorequest.php`) e as imagens
(`/Badge/...`, `/UserPic/...`). O lado do Pi manda `/dorequest.php` para
`https://retroachievements.org` e o resto para `https://media.retroachievements.org`.

## Quadro

Todos os campos numericos sao little-endian.

| Campo    | Tamanho | Conteudo                                      |
|----------|---------|-----------------------------------------------|
| magia    | 2       | `R` `A` (0x52 0x41)                           |
| versao   | 1       | 1                                             |
| tipo     | 1       | 1 = PEDIDO, 2 = RESPOSTA, 3 = PING, 4 = PONG  |
| id       | 2       | numero do pedido; a resposta repete o mesmo   |
| tamanho  | 4       | bytes do conteudo (maximo 4 MiB)              |
| conteudo | tamanho | ver abaixo                                    |
| crc32    | 4       | CRC-32 (zlib) do cabecalho e do conteudo      |

- **PEDIDO** (placa -> Pi): a requisicao HTTP inteira, como o RetroArch mandou.
- **RESPOSTA** (Pi -> placa): a resposta HTTP inteira, pronta para devolver ao
  RetroArch (`HTTP/1.0`, com `Content-Length` e `Connection: close`).
- **PING / PONG**: sem conteudo; usados para testar o cabo.

Quem recebe um quadro com magia, versao ou CRC errados descarta o byte
inicial e procura a proxima magia. A placa espera a RESPOSTA por ate 30 s;
sem resposta, devolve `502` ao RetroArch, que tenta de novo mais tarde.

Serial: 921600 baud, 8N1, sem controle de fluxo.

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
| tipo     | 1       | 1 a 8, ver abaixo                             |
| id       | 2       | numero do pedido; a resposta repete o mesmo   |
| tamanho  | 4       | bytes do conteudo (maximo 4 MiB)              |
| conteudo | tamanho | ver abaixo                                    |
| crc32    | 4       | CRC-32 (zlib) do cabecalho e do conteudo      |

| Tipo | Nome           | Sentido     | Conteudo                                        |
|------|----------------|-------------|-------------------------------------------------|
| 1    | PEDIDO         | placa -> Pi | a requisicao HTTP inteira, como o RetroArch mandou |
| 2    | RESPOSTA       | Pi -> placa | a resposta HTTP inteira (`HTTP/1.0`, com `Content-Length` e `Connection: close`) |
| 3    | PING           | placa -> Pi | nada; testa o cabo                              |
| 4    | PONG           | Pi -> placa | nada                                            |
| 5    | PEDIDO_PEDACOS | placa -> Pi | como PEDIDO, mas a resposta grande vem em pedacos |
| 6    | TAMANHO        | Pi -> placa | u32 tamanho da resposta, u32 CRC-32 da resposta |
| 7    | LER            | placa -> Pi | u32 offset, u16 quantidade de bytes             |
| 8    | PEDACO         | Pi -> placa | u32 offset, e os bytes da resposta a partir dele |

Quem recebe um quadro com magia, versao ou CRC errados descarta o byte
inicial e procura a proxima magia.

### Um pedido

A placa usa PEDIDO_PEDACOS. O Pi faz a chamada e, se a resposta tiver ate
1024 bytes, devolve uma RESPOSTA; se for maior, devolve so o TAMANHO. A placa
entao pede a resposta aos pedacos, com LER, um de cada vez, e confere o CRC-32
da resposta inteira no fim.

Por que pedacos: com um jogo rodando, a placa perde bytes de rajadas longas (o
controlador USB da porta do CP2102 depende da CPU), e uma resposta de 21 KB
num quadro so nunca chegava inteira. Com LER, nunca ha mais que um pedaco a
caminho, e um pedaco perdido e pedido de novo sozinho.

- Sem RESPOSTA ou TAMANHO em 8 s, a placa reenvia o pedido com o mesmo id. O
  Pi guarda as ultimas 16 respostas por (id, CRC-32 do pedido) e devolve a
  mesma, sem refazer a chamada: uma conquista nunca e enviada duas vezes.
- Um pedaco que nao chega em 400 ms, ou que comeca a chegar e para por 60 ms,
  e pedido de novo. Pedacos comecam com 1024 bytes, caem pela metade a cada
  perda (ate 128) e dobram de novo depois de 8 seguidos sem perda.
- Se o Pi nao tiver mais a resposta (reiniciou), responde ao LER com um
  PEDACO de offset `0xFFFFFFFF`.
- Sem resposta em 30 s, ou sem nenhum pedaco novo em 15 s, a placa devolve
  `502` ao RetroArch, que tenta de novo mais tarde.

O PEDIDO simples (resposta num quadro so) continua aceito pelo Pi, para
versoes antigas do `pandora-relay`.

Serial: 921600 baud, 8N1, sem controle de fluxo.

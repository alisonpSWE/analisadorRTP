# analisadorRTP

Um analisador de pacotes RTP de linha de comando, escrito em C do zero. Ele escuta
uma porta UDP, decodifica os cabeçalhos conforme a RFC 3550 e mostra perda,
reordenação, duplicação e jitter do fluxo.

Fiz esse projeto pra entender RTP de verdade, implementando em vez de só ler a RFC.
A ideia era manter tudo simples o bastante pra eu conseguir explicar qualquer linha,
então não tem dependência nenhuma além da biblioteca padrão e dos sockets.

## O que faz

- Decodifica os 12 bytes fixos do cabeçalho RTP (version, padding, extension, CSRC
  count, marker, payload type, sequence number, timestamp, SSRC) direto dos bytes
  crus, com máscara e deslocamento. Sem bitfield de struct, porque a ordem em que o
  compilador empacota os bits não é portável.
- Detecta perda, reordenação e duplicação pelo número de sequência, tratando o
  wraparound de 16 bits (65535 → 0).
- Usa um bitmap circular de 1024 posições pra separar duplicata de verdade de
  pacote que só chegou fora de ordem.
- Calcula o jitter de chegada com a fórmula da RFC 3550, seção 6.4.1.
- `Ctrl+C` fecha a captura e imprime um resumo.

## Compilando

Precisa do MinGW com GCC. O `make` do MinGW se chama `mingw32-make`:

```sh
mingw32-make
```

Sai um `analisador.exe`, compilado com `-Wall -Wextra -std=c99` e linkado com
`-lws2_32`. Pra limpar:

```sh
mingw32-make clean
```

> [!NOTE]
> Usa Winsock2, não sockets POSIX — `sys/socket.h` e `arpa/inet.h` não existem no
> MinGW. A diferença que mais dá trabalho é que o `recvfrom` do Windows não acorda
> com `Ctrl+C`, então tem um timeout de recepção de 500 ms só pro laço conseguir
> perceber que é hora de sair.

## Uso

Roda sem argumento nenhum:

```sh
analisador.exe
```

Ele abre um menu no terminal:

```
=========================
 ANALISADOR RTP
=========================
porta atual...: 5004
clock atual...: 90000 Hz
modo verboso..: desligado
-------------------------
1 - mudar a porta
2 - mudar o clock
3 - ligar/desligar o modo verboso
4 - comecar a capturar
5 - sair
-------------------------
escolha uma opcao:
```

As opções 1, 2 e 3 só mexem na configuração e voltam pro menu. A opção 4 abre o
socket e fica escutando até você apertar `Ctrl+C`, e aí imprime o resumo e volta
pro menu — dá pra rodar várias capturas seguidas sem fechar o programa. Cada
captura começa com os contadores zerados.

O clock só muda a conversão do jitter pra milissegundos. 90000 é o padrão de
vídeo (H.264, por exemplo); áudio costuma ser 8000 ou 48000.

### Modo verboso

Com o modo verboso ligado cada pacote vira uma linha, com os campos decodificados
e os contadores até ali:

```
127.0.0.1:64689 V=2 P=0 X=0 CC=0 M=1 PT=96 seq=100 ts=1000 ssrc=0xDEADBEEF len=12 | total=1 perdidos=0 fora_ordem=0 dup=0 tarde=0 ignorados=0 jitter=0.000ms jitter_max=0.000ms
```

Pacote que não passa na validação aparece com o motivo:

```
127.0.0.1:64689 descartado (8 bytes): pacote menor que 12 bytes (cabecalho incompleto)
127.0.0.1:64689 descartado (12 bytes): version diferente de 2
```

Com o modo verboso desligado ele fica quieto recebendo e só imprime o resumo no fim.

### Resumo final

```
=========================
 resumo
=========================
duracao: 23.86s
ssrc: 0xA5312B7D
Pacotes recebidos: 96
esperados: 96
lost: 0 (0.00%)
Fora de ordem: 0
duplicados: 0
tarde demais (janela 1024): 0
ignorados (outro ssrc): 0
descartados: 1
jitter medio: 3.828ms
jitter max: 20.543ms
-----
fim.
```

O `esperados` vem de `(última seq) - (primeira) + 1`, e o `lost` é
`esperados - recebidos únicos`. Contar a perda assim no fim, em vez de incrementar
um contador a cada buraco, faz um pacote atrasado se corrigir sozinho — se fosse
incrementando, cada atrasado obrigaria a decrementar e uma hora a conta ficaria
negativa.

## Testando com ffmpeg

Não precisa de arquivo de vídeo, o `lavfi` gera um sintético:

```sh
ffmpeg -re -f lavfi -i "testsrc=size=320x240:rate=30" -t 10 -an ^
       -c:v libx264 -preset ultrafast -tune zerolatency ^
       -f rtp rtp://127.0.0.1:5004
```

Em outro terminal:

```sh
analisador.exe
```

No menu, opção 3 pra ligar o modo verboso e opção 4 pra começar a capturar
(a porta 5004 já é a padrão).

Pra usar um vídeo real, troca `-f lavfi -i "testsrc=..."` por `-i video.mp4`.

> [!WARNING]
> O ffmpeg manda RTP na porta que você passou e **RTCP na porta seguinte** —
> `rtp://127.0.0.1:5004` usa 5004 pra RTP e 5005 pra RTCP. Escutar a porta errada
> faz o programa tentar ler RTCP como RTP: os pacotes até passam na validação
> (`version=2`), mas com `PT=200` e estatísticas sem sentido nenhum.

> [!TIP]
> Pra conferir campo a campo, captura no Wireshark e usa `Decode As → RTP` na porta.
> No Windows, capturar loopback só funciona com o Npcap instalado com a opção
> "Adapter for loopback traffic capture".

## Como funciona

```
rtp.h / rtp.c      struct do cabeçalho e a função de parse (sem estado)
stats.h / stats.c  janela de sequência, contadores e jitter
main.c             menu, socket, laço principal, sinal
```

Separei assim de propósito: `rtp.c` não sabe nada de rede, `stats.c` não sabe nada
de socket nem inclui header do Windows, e só o `main.c` conhece os dois lados.

Três detalhes concentram a maior parte da lógica.

**Wraparound da sequência.** Comparar `seq > max_seq` como inteiro quebra na virada
de 65535 pra 0, que parece um retrocesso enorme em vez de um avanço de 1. A
subtração modular em 16 bits dá o sinal certo dos dois lados:

```c
dif = (int16_t)(h->seq - s->max_seq);
```

**Campos multibyte.** Um cast tipo `*(uint16_t*)(pkt + 2)` é acesso desalinhado e
viola strict aliasing. O parse usa `memcpy` nos bytes crus e só então converte com
`ntohs`/`ntohl`.

**Limpar a janela de bits.** Quando a sequência avança, as posições puladas precisam
ter o bit zerado antes de marcar a nova. Sem isso a seq 1524 encontra o bit que a
500 deixou (as duas caem no mesmo slot, `% 1024`) e vira duplicata em vez de
reordenação. É o tipo de bug que só aparece depois de mais de mil pacotes, então
demora a dar as caras.

## Limitações

Coisas que deixei de fora de propósito, pra não complicar:

- **Um SSRC por vez.** O primeiro pacote válido trava o SSRC do fluxo; se aparecer
  um segundo fluxo junto (áudio e vídeo do mesmo ffmpeg, por exemplo) ele cai em
  `ignorados` em vez de ser analisado à parte.
- **Alcance da janela.** Pacote que chega mais de 1024 números atrasado vai pra
  `tarde demais`, porque a essa altura não dá mais pra saber se é duplicata ou
  reordenação.
- **Sem thread e sem select/epoll.** Um `recvfrom` bloqueante por vez.
- **Só o cabeçalho fixo.** Extensão (`X=1`) é sinalizada, mas não decodificada.

// analisadorRTP
// por enquanto so escuta uma porta udp e imprime o cabecalho dos pacotes que chegam

// winsock2 antes de windows.h, senao vem o winsock 1 junto e sockaddr_in briga
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtp.h"

#define PORTA_PADRAO 5004
#define TAM_BUFFER   65536   // datagrama maximo, no winsock buffer curto faz recvfrom falhar em vez de truncar

// usei isso pra debugar no comeco
void dump_bytes(const unsigned char *p, int n)
{
    int i;
    for (i = 0; i < n && i < 16; i++)
        printf("%02X ", p[i]);
    printf("\n");
}

// =================================================
// captura: socket UDP e loop recvfrom -> rtp_parse -> print
// =================================================
static void capturar(int porta)
{
    SOCKET s;
    struct sockaddr_in local;
    static unsigned char buf[TAM_BUFFER];   // static pra nao estourar a pilha

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        printf("erro: socket() falhou (%d)\n", WSAGetLastError());
        return;
    }

    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_port = htons((unsigned short)porta);   // htons, porta vai em network byte order
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&local, sizeof local) == SOCKET_ERROR) {
        printf("erro: bind na porta %d falhou (%d)\n", porta, WSAGetLastError());
        closesocket(s);
        return;
    }

    printf("escutando UDP na porta %d\n", porta);
    fflush(stdout);

    // TODO: sair do loop com ctrl+c em vez de fechar a janela no berro
    while (1) {
        struct sockaddr_in remoto;
        int tam = sizeof remoto;
        struct rtp_header h;
        int n, r;

        n = recvfrom(s, (char *)buf, TAM_BUFFER, 0,
                     (struct sockaddr *)&remoto, &tam);
        if (n == SOCKET_ERROR) {
            printf("erro: recvfrom falhou (%d)\n", WSAGetLastError());
            break;
        }

        r = rtp_parse(buf, (size_t)n, &h);

        // inet_ntoa porque inet_ntop nao ta declarado neste mingw
        if (r == 0) {
            printf("%s:%d V=%u P=%u X=%u CC=%u M=%u PT=%u seq=%u ts=%lu ssrc=0x%08lX len=%d\n",
                   inet_ntoa(remoto.sin_addr), ntohs(remoto.sin_port),
                   h.version, h.padding, h.extension, h.cc, h.marker, h.pt,
                   h.seq, (unsigned long)h.ts, (unsigned long)h.ssrc, n);
        } else {
            printf("%s:%d descartado (%d bytes): %s\n",
                   inet_ntoa(remoto.sin_addr), ntohs(remoto.sin_port),
                   n, rtp_erro_str(r));
        }
        fflush(stdout);
    }

    closesocket(s);

    // TODO: contar perda, duplicata e jitter e imprimir um resumo no final
}

int main(void)
{
    WSADATA wsa;

    // winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("erro: WSAStartup falhou\n");
        return 1;
    }

    capturar(PORTA_PADRAO);

    WSACleanup();
    return 0;
}

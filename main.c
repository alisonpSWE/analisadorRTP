// analisadorRTP
// escuta uma porta udp, decodifica cabecalho RTP e conta perda/reordenacao/jitter
// menu no terminal: escolhe a porta, o clock, o modo verboso e manda capturar
// durante a captura, ctrl+c encerra e imprime o resumo

// =================================================
// includes, defines e estado global
// =================================================

// winsock2 antes de windows.h, senao vem o winsock 1 junto e sockaddr_in briga
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "rtp.h"
#include "stats.h"

#define PORTA_PADRAO 5004
#define CLOCK_PADRAO 90000
#define TAM_BUFFER   65536   // datagrama maximo, no winsock buffer curto faz recvfrom falhar em vez de truncar

static volatile sig_atomic_t g_rodando = 1;

// usei isso pra debugar no comeco
void dump_bytes(const unsigned char *p, int n)
{
    int i;
    for (i = 0; i < n && i < 16; i++)
        printf("%02X ", p[i]);
    printf("\n");
}

// =================================================
// sinal: Ctrl+C derruba o loop de captura
// =================================================
static void on_sigint(int sinal) {
    (void)sinal;
    g_rodando = 0;
}

// =================================================
// leitura do teclado: scanf + limpeza do buffer
// =================================================
// o scanf deixa o que sobrou digitado no buffer, entao jogo fora ate a quebra de linha
static void limpar_entrada(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

// fica pedindo ate o usuario digitar um numero de verdade
static int ler_inteiro(const char *mensagem)
{
    int valor;

    while (1) {
        printf("%s", mensagem);
        fflush(stdout);

        if (scanf("%d", &valor) == 1) {
            limpar_entrada();
            return valor;
        }

        limpar_entrada();
        printf("valor invalido, digite so numeros.\n");
    }
}

// =================================================
// menu na tela
// =================================================
static void mostrar_menu(int porta, int clock_rate, int verboso)
{
    printf("\n");
    printf("=========================\n");
    printf(" ANALISADOR RTP\n");
    printf("=========================\n");
    printf("porta atual...: %d\n", porta);
    printf("clock atual...: %d Hz\n", clock_rate);
    printf("modo verboso..: %s\n", verboso ? "ligado" : "desligado");
    printf("-------------------------\n");
    printf("1 - mudar a porta\n");
    printf("2 - mudar o clock\n");
    printf("3 - ligar/desligar o modo verboso\n");
    printf("4 - comecar a capturar\n");
    printf("5 - sair\n");
    printf("-------------------------\n");
}

// =================================================
// captura: socket UDP, loop recvfrom -> rtp_parse -> stats_update -> print
// =================================================
static void capturar(int porta, int clock_rate, int verboso)
{
    SOCKET s;
    struct sockaddr_in local;
    static unsigned char buf[TAM_BUFFER];   // static pra nao estourar a pilha
    struct rtp_stats stats;
    LARGE_INTEGER qpc_freq, qpc_inicio, qpc_fim;
    unsigned long descartados = 0;
    DWORD timeout_ms = 500;

    stats_init(&stats, (uint32_t)clock_rate);

    QueryPerformanceFrequency(&qpc_freq);   // substituto do gettimeofday, que nao existe aqui

    // socket
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

    // no windows o recvfrom bloqueado nao acorda com ctrl+c, entao timeout de 500ms
    // pro loop conseguir olhar g_rodando. aqui SO_RCVTIMEO e um DWORD em ms, nao timeval
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout_ms, sizeof timeout_ms) == SOCKET_ERROR) {
        printf("erro: setsockopt(SO_RCVTIMEO) falhou (%d)\n", WSAGetLastError());
        closesocket(s);
        return;
    }

    g_rodando = 1;
    signal(SIGINT, on_sigint);

    printf("\nescutando UDP na porta %d (verbose=%s) - Ctrl+C para parar\n",
           porta, verboso ? "sim" : "nao");
    fflush(stdout);

    QueryPerformanceCounter(&qpc_inicio);

    while (g_rodando) {
        struct sockaddr_in remoto;
        int tam = sizeof remoto;
        int n;

        n = recvfrom(s, (char *)buf, TAM_BUFFER, 0,
                     (struct sockaddr *)&remoto, &tam);
        if (n == SOCKET_ERROR) {
            int erro = WSAGetLastError();
            if (erro == WSAETIMEDOUT)
                continue;   // nao e erro, so a deixa pra rechecar g_rodando
            printf("erro: recvfrom falhou (%d)\n", erro);
            break;
        }

        {
            LARGE_INTEGER agora;
            uint32_t arrival;
            struct rtp_header h;
            int r;

            // pega a hora antes de decodificar, quanto mais perto da chegada melhor pro jitter
            QueryPerformanceCounter(&agora);
            arrival = (uint32_t)(((double)agora.QuadPart / (double)qpc_freq.QuadPart)
                                  * clock_rate);

            // parse do cabecalho
            r = rtp_parse(buf, (size_t)n, &h);

            // estatisticas
            if (r == 0)
                stats_update(&stats, &h, arrival);
            else
                descartados++;

            if (verboso) {
                // inet_ntoa porque inet_ntop nao ta declarado neste mingw
                if (r == 0) {
                    printf("%s:%d V=%u P=%u X=%u CC=%u M=%u PT=%u "
                           "seq=%u ts=%lu ssrc=0x%08lX len=%d"
                           " | total=%lu perdidos=%ld fora_ordem=%lu"
                           " dup=%lu tarde=%lu ignorados=%lu"
                           " jitter=%.3fms jitter_max=%.3fms\n",
                           inet_ntoa(remoto.sin_addr), ntohs(remoto.sin_port),
                           h.version, h.padding, h.extension, h.cc, h.marker, h.pt,
                           h.seq, (unsigned long)h.ts, (unsigned long)h.ssrc, n,
                           stats.recebidos, stats_perdidos(&stats),
                           stats.fora_de_ordem, stats.duplicados,
                           stats.tarde_demais, stats.ignorados,
                           stats_jitter_ms(&stats), stats_jitter_max_ms(&stats));
                } else {
                    printf("%s:%d descartado (%d bytes): %s\n",
                           inet_ntoa(remoto.sin_addr), ntohs(remoto.sin_port),
                           n, rtp_erro_str(r));
                }
                fflush(stdout);
            }
        }
    }

    QueryPerformanceCounter(&qpc_fim);

    closesocket(s);
    signal(SIGINT, SIG_DFL);   // fora da captura, Ctrl+C volta a fechar o programa

    // =================================================
    // resumo final: totais, perda %, jitter e duracao
    // =================================================
    {
        unsigned long esperados = stats_esperados(&stats);
        long lost = stats_perdidos(&stats);
        double perc = (esperados > 0) ? (100.0 * (double)lost / (double)esperados) : 0.0;
        double secs = (double)(qpc_fim.QuadPart - qpc_inicio.QuadPart) / (double)qpc_freq.QuadPart;

        printf("\n=========================\n");
        printf(" resumo\n");
        printf("=========================\n");
        printf("duracao: %.2fs\n", secs);
        if (stats.iniciado)
            printf("ssrc: 0x%08lX\n", (unsigned long)stats.ssrc);
        printf("Pacotes recebidos: %lu\n", stats.recebidos);
        printf("esperados: %lu\n", esperados);
        printf("lost: %ld (%.2f%%)\n", lost, perc);
        printf("Fora de ordem: %lu\n", stats.fora_de_ordem);
        printf("duplicados: %lu\n", stats.duplicados);
        printf("tarde demais (janela %d): %lu\n", STATS_WIN_BITS, stats.tarde_demais);
        printf("ignorados (outro ssrc): %lu\n", stats.ignorados);
        printf("descartados: %lu\n", descartados);
        printf("jitter medio: %.3fms\n", stats_jitter_ms(&stats));
        printf("jitter max: %.3fms\n", stats_jitter_max_ms(&stats));
        printf("-----\n");
    }
}

// =================================================
// main: liga o winsock e roda o loop do menu
// =================================================
int main(void)
{
    int porta = PORTA_PADRAO;
    int clock_rate = CLOCK_PADRAO;
    int verboso = 0;
    int opcao = 0;
    WSADATA wsa;

    // winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("erro: WSAStartup falhou\n");
        return 1;
    }

    while (opcao != 5) {
        mostrar_menu(porta, clock_rate, verboso);
        opcao = ler_inteiro("escolha uma opcao: ");

        if (opcao == 1) {
            int nova = ler_inteiro("digite a porta (1 a 65535): ");
            if (nova < 1 || nova > 65535)
                printf("porta invalida, continua %d.\n", porta);
            else
                porta = nova;
        } else if (opcao == 2) {
            int novo = ler_inteiro("digite o clock em Hz (90000 video, 8000 audio): ");
            if (novo < 1)
                printf("clock invalido, continua %d.\n", clock_rate);
            else
                clock_rate = novo;
        } else if (opcao == 3) {
            verboso = !verboso;
            printf("modo verboso agora esta %s.\n", verboso ? "ligado" : "desligado");
        } else if (opcao == 4) {
            capturar(porta, clock_rate, verboso);
        } else if (opcao == 5) {
            printf("fim.\n");
        } else {
            printf("opcao inexistente, tente de novo.\n");
        }
    }

    WSACleanup();
    return 0;
}

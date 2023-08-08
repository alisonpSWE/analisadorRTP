#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include "rtp.h"

// =================================================
// estado do fluxo: seq, bitmap da janela, contadores e jitter
// =================================================
// janela de seq que da pra rastrear, fixa
#define STATS_WIN_BITS 1024

// estado de um fluxo so, trava no primeiro ssrc que aparecer
struct rtp_stats {
    int      iniciado;
    uint32_t ssrc;

    uint16_t base_seq;
    uint16_t max_seq;
    uint32_t cycles;    // voltas de 65536 que a seq ja deu

    uint32_t seen[STATS_WIN_BITS / 32];   // bitmap, 1 bit por seq % 1024

    unsigned long recebidos;
    unsigned long duplicados;
    unsigned long fora_de_ordem;
    unsigned long tarde_demais;   // atrasou mais que a janela, nao da pra classificar
    unsigned long ignorados;      // outro ssrc

    // jitter
    uint32_t clock_rate;
    int32_t  transit_ant;
    double   jitter;
    double   jitter_max;
};

// =================================================
// API do modulo de estatisticas
// =================================================
void stats_init(struct rtp_stats *s, uint32_t clock_rate);

// arrival ja vem convertido pra ticks do clock_rate, quem faz isso e o main
void stats_update(struct rtp_stats *s, const struct rtp_header *h, uint32_t arrival);

unsigned long stats_esperados(const struct rtp_stats *s);
long stats_perdidos(const struct rtp_stats *s);

double stats_jitter_ms(const struct rtp_stats *s);
double stats_jitter_max_ms(const struct rtp_stats *s);

#endif

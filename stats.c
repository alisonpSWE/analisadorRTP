// stats.c - contadores de perda, reordenacao, duplicata e jitter
// nao inclui nada de winsock, so recebe o cabecalho ja parseado

#include "stats.h"
#include <string.h>

#define WIN_WORDS (STATS_WIN_BITS / 32)

// =================================================
// bitmap da janela: marca quais seq ja chegaram (get/set/clear)
// =================================================
// i / 32 acha o uint32_t certo, i % 32 acha o bit dentro dele

static int bit_get(const uint32_t seen[WIN_WORDS], uint16_t seq)
{
    unsigned i = seq % STATS_WIN_BITS;
    return (int)((seen[i / 32] >> (i % 32)) & 1u);
}

static void bit_set(uint32_t seen[WIN_WORDS], uint16_t seq)
{
    unsigned i = seq % STATS_WIN_BITS;
    seen[i / 32] |= (uint32_t)1u << (i % 32);
}

static void bit_clear(uint32_t seen[WIN_WORDS], uint16_t seq)
{
    unsigned i = seq % STATS_WIN_BITS;
    seen[i / 32] &= ~((uint32_t)1u << (i % 32));
}


// =================================================
// init: zera os contadores e grava o clock rate
// =================================================
void stats_init(struct rtp_stats *s, uint32_t clock_rate)
{
    memset(s, 0, sizeof *s);
    s->clock_rate = clock_rate;
}

// =================================================
// update por pacote: jitter (RFC 3550) + perda/duplicata/reordenacao pela seq
// =================================================
void stats_update(struct rtp_stats *s, const struct rtp_header *h, uint32_t arrival)
{
    int16_t dif;
    int32_t trans;

    // primeiro pacote
    if (!s->iniciado) {
        s->iniciado = 1;
        s->ssrc = h->ssrc;
        s->base_seq = h->seq;
        s->max_seq = h->seq;
        s->cycles = 0;
        s->recebidos = 1;
        bit_set(s->seen, h->seq);
        s->transit_ant = (int32_t)(arrival - h->ts);   // so guarda a referencia, ainda nao da pra calcular jitter
        return;
    }

    if (h->ssrc != s->ssrc) {
        s->ignorados++;
        return;
    }

    s->recebidos++;

    // jitter
    // RFC 3550 6.4.1. se arrival < ts a subtracao da a volta em 2^32, tudo bem,
    // so a diferenca entre dois trans seguidos importa
    trans = (int32_t)(arrival - h->ts);
    {
        int32_t jit = trans - s->transit_ant;
        if (jit < 0)
            jit = -jit;
        s->jitter += ((double)jit - s->jitter) / 16.0;   // media movel de ganho 1/16
        if (s->jitter > s->jitter_max)
            s->jitter_max = s->jitter;
    }
    s->transit_ant = trans;

    // seq e perda
    dif = (int16_t)(h->seq - s->max_seq);   // int16_t pra virada de 65535 pra 0 dar +1 e nao -65535

    if (dif == 0) {
        s->duplicados++;
    } else if (dif > 0) {
        int qtd = dif;

        if (h->seq < s->max_seq)
            s->cycles += 65536;   // so cai aqui se a seq deu a volta

        if (qtd >= STATS_WIN_BITS) {
            memset(s->seen, 0, sizeof s->seen);   // pulou a janela inteira, tudo que ta marcado e lixo
        } else {
            int k;
            // limpa as posicoes puladas, senao um atrasado que preencha o buraco vira "duplicado"
            for (k = 1; k < qtd; k++)
                bit_clear(s->seen, (uint16_t)(s->max_seq + k));
        }

        bit_set(s->seen, h->seq);
        s->max_seq = h->seq;
    } else {
        int atraso = -dif;

        if (atraso >= STATS_WIN_BITS) {
            s->tarde_demais++;
        }
        else if (bit_get(s->seen, h->seq)) {
            s->duplicados++;
        }
        else {
            s->fora_de_ordem++;
            bit_set(s->seen, h->seq);
        }
    }
}


// =================================================
// calculos derivados: pacotes esperados, perdidos e jitter em ms
// =================================================
unsigned long stats_esperados(const struct rtp_stats *s)
{
    if (!s->iniciado)
        return 0;
    return (unsigned long)(s->cycles + s->max_seq) - (unsigned long)s->base_seq + 1;
}

long stats_perdidos(const struct rtp_stats *s)
{
    unsigned long esperados = stats_esperados(s);
    unsigned long unicos = s->recebidos - s->duplicados;
    return (long)esperados - (long)unicos;
}

double stats_jitter_ms(const struct rtp_stats *s)
{
    return (s->jitter / s->clock_rate) * 1000.0;
}

double stats_jitter_max_ms(const struct rtp_stats *s)
{
    return (s->jitter_max / s->clock_rate) * 1000.0;
}

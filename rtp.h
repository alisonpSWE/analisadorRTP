#ifndef RTP_H
#define RTP_H

#include <stdint.h>
#include <stddef.h>

// =================================================
// struct do cabecalho RTP ja em host byte order
// =================================================
// cabecalho fixo do RTP, ja convertido pra ordem de bytes do host
struct rtp_header {
    uint8_t  version;
    uint8_t  padding;
    uint8_t  extension;
    uint8_t  cc;          // quantos CSRC vem depois dos 12 bytes fixos
    uint8_t  marker;
    uint8_t  pt;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    uint16_t header_len;  // 12 + 4*cc, onde comeca o payload
};

// =================================================
// codigos de erro e prototipos
// =================================================
// rtp_parse devolve 0 ou um destes
#define RTP_ERR_CURTO      -1
#define RTP_ERR_VERSAO     -2
#define RTP_ERR_CSRC_CURTO -3

int rtp_parse(const unsigned char *buf, size_t len, struct rtp_header *h);
const char *rtp_erro_str(int codigo);

#endif

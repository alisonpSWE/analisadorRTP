// rtp.c - parse do cabecalho fixo de 12 bytes (RFC 3550, 5.1)
//
//   byte 0: V V P X C C C C
//   byte 1: M T T T T T T T
//   2-3 seq | 4-7 timestamp | 8-11 ssrc
//
// sem bitfield de struct aqui, a ordem que o compilador poe os bits nao e portavel

#include "rtp.h"
#include <string.h>
#include <winsock2.h>   // ntohs/ntohl

// =================================================
// parse RTP: le os 12 bytes fixos do cabecalho e valida tamanho/versao
// =================================================
int rtp_parse(const unsigned char *pkt, size_t tam, struct rtp_header *cab)
{
    unsigned char b0, b1;
    uint16_t tmp;
    uint32_t tmp32;

    if (tam < 12)
        return RTP_ERR_CURTO;

    b0 = pkt[0];
    b1 = pkt[1];

    // byte 0
    cab->version   = (uint8_t)((b0 >> 6) & 0x03);   // >> 6 porque version sao os 2 bits mais altos

    if (cab->version != 2)
        return RTP_ERR_VERSAO;

    cab->padding   = (uint8_t)((b0 >> 5) & 0x01);
    cab->extension = (uint8_t)((b0 >> 4) & 0x01);
    cab->cc        = (uint8_t)(b0 & 0x0F);          // & 0x0F pega so os 4 bits de baixo

    cab->header_len = (uint16_t)(12 + 4 * cab->cc); // 12 fixos, resto e csrc + payload
    if (tam < cab->header_len)
        return RTP_ERR_CSRC_CURTO;

    // byte 1
    cab->marker = (uint8_t)((b1 >> 7) & 0x01);      // >> 7 porque o marker e o bit mais alto
    cab->pt = (uint8_t)(b1 & 0x7F);                 // & 0x7F pega os 7 bits do payload type

    // campos multibyte
    // memcpy em vez de cast de ponteiro, pkt+2 pode estar desalinhado
    memcpy(&tmp, pkt + 2, sizeof tmp);
    cab->seq = ntohs(tmp);                          // ntohs pra nao vir invertido

    memcpy(&tmp32, pkt + 4, sizeof tmp32);
    cab->ts = ntohl(tmp32);

    memcpy(&tmp32, pkt + 8, sizeof tmp32);
    cab->ssrc = ntohl(tmp32);

    return 0;
}


// =================================================
// erros do parse: codigo -> texto
// =================================================
const char *rtp_erro_str(int codigo)
{
    switch (codigo) {
    case RTP_ERR_CURTO:      return "pacote menor que 12 bytes (cabecalho incompleto)";
    case RTP_ERR_VERSAO:     return "version diferente de 2";
    case RTP_ERR_CSRC_CURTO: return "CC indica CSRCs que nao cabem no pacote recebido";
    default:                 return "erro desconhecido";
    }
}

/*-
  parse.c -- find block boundraries

  Copyright (C) 2011, 2012 Mikolaj Izdebski

  This file is part of lbzip2.

  lbzip2 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  lbzip2 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "common.h"             /* OK */

#include <arpa/inet.h>          /* htonl() */

#include "main.h"               /* Trace() */
#include "decode.h"             /* bits_need() */

#include "scantab.h"


#define bits_need(bs,n)                         \
  ((n) <= (bs)->live                            \
   ?                                            \
   OK                                           \
   :                                            \
   unlikely ((bs)->data == (bs)->limit)         \
   ?                                            \
   (bs)->eof                                    \
   ?                                            \
   FINISH                                       \
   :                                            \
   MORE                                         \
   :                                            \
   ((bs)->buff |= (uint64_t) ntohl              \
    (*(bs)->data) << (32u - (bs)->live),        \
    (bs)->data++,                               \
    (bs)->live += 32u,                          \
    OK))

#define bits_peek(bs,n) ((bs)->buff >> (64u - (n)))

#define bits_dump(bs,n)                         \
  ((bs)->buff <<= (n),                          \
   (bs)->live -= (n),                           \
   (void) 0)

#define bits_align(bs)                          \
  (bits_dump (bs, (bs)->live % 8u),             \
   (void) 0)

#define bits_consume(bs)                        \
  (bits_dump (bs, (bs)->live),                  \
   (bs)->data = (bs)->limit,                    \
   (void) 0)


enum {
  STREAM_MAGIC_1, STREAM_MAGIC_2, BLOCK_MAGIC_1, BLOCK_MAGIC_2, BLOCK_MAGIC_3,
  BLOCK_CRC_1, BLOCK_CRC_2, EOS_2, EOS_3, EOS_CRC_1, EOS_CRC_2,
};


void
parser_init(struct parser_state *ps, int my_bs100k)
{
  ps->state = BLOCK_MAGIC_1;
  ps->prev_bs100k = my_bs100k;
  ps->bs100k = -1;
  ps->computed_crc = 0u;
}


int
parse(struct parser_state *restrict ps, struct header *restrict hd,
      struct bitstream *bs, unsigned *garbage)
{
  assert(ps->state != ACCEPT);
  if (ps->state == ACCEPT) {
    return FINISH;
  }

  while (OK == bits_need(bs, 16)) {
    unsigned word = bits_peek(bs, 16);

    bits_dump(bs, 16);

    switch (ps->state) {
    case STREAM_MAGIC_1:
      if (0x425Au != word) {
        hd->bs100k = -1;
        hd->crc = 0;
        ps->state = ACCEPT;
        *garbage = 16;
        return FINISH;
      }
      ps->state = STREAM_MAGIC_2;
      continue;

    case STREAM_MAGIC_2:
      if (0x6839u < word || 0x6831 > word) {
        hd->bs100k = -1;
        hd->crc = 0;
        ps->state = ACCEPT;
        *garbage = 32;
        return FINISH;
      }
      ps->prev_bs100k = word & 15u;
      ps->bs100k = -1;
      ps->state = BLOCK_MAGIC_1;
      continue;

    case BLOCK_MAGIC_1:
      if (0x1772u == word) {
        ps->state = EOS_2;
        continue;
      }
      if (0x3141u != word)
        return ERR_HEADER;
      ps->state = BLOCK_MAGIC_2;
      continue;

    case BLOCK_MAGIC_2:
      if (0x5926u != word)
        return ERR_HEADER;
      ps->state = BLOCK_MAGIC_3;
      continue;

    case BLOCK_MAGIC_3:
      if (0x5359u != word)
        return ERR_HEADER;
      ps->state = BLOCK_CRC_1;
      continue;

    case BLOCK_CRC_1:
      ps->stored_crc = word;
      ps->state = BLOCK_CRC_2;
      continue;

    case BLOCK_CRC_2:
      hd->crc = (ps->stored_crc << 16) | word;
      hd->bs100k = ps->prev_bs100k;
      ps->computed_crc =
          (ps->computed_crc << 1) ^ (ps->computed_crc >> 31) ^ hd->crc;
      ps->prev_bs100k = ps->bs100k;
      ps->state = BLOCK_MAGIC_1;
      return OK;

    case EOS_2:
      if (0x4538u != word)
        return ERR_HEADER;
      ps->state = EOS_3;
      continue;

    case EOS_3:
      if (0x5090u != word)
        return ERR_HEADER;
      ps->state = EOS_CRC_1;
      continue;

    case EOS_CRC_1:
      ps->stored_crc = word;
      ps->state = EOS_CRC_2;
      continue;

    case EOS_CRC_2:
      ps->stored_crc = (ps->stored_crc << 16) | word;
      if (ps->stored_crc != ps->computed_crc)
        return ERR_STRMCRC;
      ps->computed_crc = 0u;
      bits_align(bs);
      ps->state = STREAM_MAGIC_1;
      continue;

    default:
      break;
    }

    assert(0);
  }

  if (FINISH != bits_need(bs, 16))
    return MORE;

  if (ps->state == STREAM_MAGIC_1) {
    ps->state = ACCEPT;
    *garbage = 0;
    return FINISH;
  }
  if (ps->state == STREAM_MAGIC_2) {
    ps->state = ACCEPT;
    *garbage = 16;
    return FINISH;
  }

  return ERR_EOF;
}


int
scan(struct bitstream *bs, unsigned skip)
{
  unsigned state = 0;
  const uint32_t *data, *limit;

  if (skip > bs->live) {
    skip -= bs->live;
    bits_dump(bs, bs->live);
    skip = (skip + 31u) / 32u;
    if (bs->limit - bs->data < skip)
      bs->data = bs->limit;
    else
      bs->data += skip;
  }

again:
  assert(state < ACCEPT);
  while (bs->live > 0) {
    unsigned bit = bits_peek(bs, 1);

    bits_dump(bs, 1);
    state = mini_dfa[state][bit];

    if (state == ACCEPT) {
      if (bits_need(bs, 32) == OK) {
        bits_dump(bs, 32);
        return OK;
      }
      else {
        bits_consume(bs);
        return MORE;
      }
    }
  }

  data = bs->data;
  limit = bs->limit;

  while (data < limit) {
    unsigned bt_state = state;
    uint32_t word = *data;

    word = ntohl(word);
    state = big_dfa[state][word >> 24];
    state = big_dfa[state][(uint8_t)(word >> 16)];
    state = big_dfa[state][(uint8_t)(word >> 8)];
    state = big_dfa[state][(uint8_t)word];

    if (unlikely(state == ACCEPT)) {
      state = bt_state;
      bs->data = data;
      (void)bits_need(bs, 1u);
      goto again;
    }

    data++;
  }

  bs->data = data;
  return MORE;
}

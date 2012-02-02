/*-
  encode.c -- low-level compressor

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>      /* free() */

#include "xalloc.h"      /* xmalloc() */
#include "divsufsort.h"  /* cyclic_divbwt() */

#include "encode.h"


/* return ninuse */
static SInt
make_map_e(Byte *cmap, const Byte *inuse)
{
  SInt i, j;

  j = 0;

  for (i = 0; i < 256; i++)
  {
    int k = inuse[i];
    cmap[i] = j;
    j += k;
  }

  return j;
}


/*---------------------------------------------------*/
/* returns nmtf */
static Int
do_mtf(
  Short *mtfv,
  Int *mtffreq,
  Byte *cmap,
  SInt nblock,
  SInt EOB)
{
  Byte order[255];
  SInt i;
  SInt k;
  SInt t;
  Byte c;
  Byte u;
  Int *bwt = (Int *)mtfv;
  const Short *mtfv0 = mtfv;


  for (i = 0; i <= EOB; i++)
    mtffreq[i] = 0;

  k = 0;
  u = 0;
  for (i = 0; i < 255; i++)
    order[i] = i+1;

#define RUN()                                   \
  if (unlikely(k))                              \
    do {                                        \
      mtffreq[*mtfv++ = --k & 1]++;             \
      k >>= 1;                                  \
    } while (k);                                \

#define MTF()                                   \
  {                                             \
    Byte *p = order;                            \
    t  = *p;                                    \
    *p = u;                                     \
    for (;;)                                    \
    {                                           \
      if (c == t) {u=t;break;}                  \
      u  = *++p;                                \
      *p = t;                                   \
      if (c == u) break;                        \
      t  = *++p;                                \
      *p = u;                                   \
    }                                           \
    t = p - order + 2;                          \
    *mtfv++ = t;                                \
    mtffreq[t]++;                               \
  }

  for (i = 0; i < nblock; i++)
  {
    if ((c = cmap[*bwt++]) == u)
    { k++; continue; }
    RUN(); MTF();
  }

  RUN();

  *mtfv++ = EOB;
  mtffreq[EOB]++;

  return mtfv - mtfv0;

#undef RUN
#undef MTF
}

size_t
YBenc_work(YBenc_t *s, YBcrc_t *crc)
{
  Int cost;
  Int pk;
  Int i;
  const Byte *sp;  /* selector pointer */
  Byte *smp;       /* selector MTFV pointer */
  Byte c;  /* value before MTF */
  Byte j;  /* value after MTF */
  Int p;   /* MTF state */
  Int EOB;
  Byte cmap[256];


  /* Finalize initial RLE. */
  if (s->rle_state >= 4) {
    assert(s->nblock < s->max_block_size);
    s->block[s->nblock++] = s->rle_state-4;
    s->cmap[s->rle_state-4] = 1;
  }
  assert(s->nblock > 0);

  EOB = make_map_e(cmap, s->cmap) + 1;
  assert(EOB >= 2);
  assert(EOB < 258);

  /* Sort block. */
  s->mtfv = xmalloc((s->nblock + 50) * sizeof(Int));
  s->bwt_idx = cyclic_divbwt(s->block, s->mtfv, s->nblock);
  free(s->block);
  s->nmtf = do_mtf(s->mtfv, s->lookup[0], cmap, s->nblock, EOB);
  s->mtfv = xrealloc(s->mtfv, (s->nmtf + 50) * sizeof(Short));

  cost =
    + 48  /* header */
    + 32  /* crc */
    +  1  /* rand bit */
    + 24  /* bwt index */
    + 00  /* {cmap} */
    +  3  /* nGroups */
    + 15  /* nSelectors */
    + 00  /* {sel} */
    + 00  /* {tree} */
    + 00; /* {mtfv} */

  cost += YBpriv_prefix(s, s->mtfv, s->nmtf);

  sp = s->selector;
  smp = s->selectorMTF;

  /* A trick that allows to do MTF without branching, using arithmetical
     and logical operations only.  The whole MTF state packed into one
     32-bit integer.
  */

  /* Set up initial MTF state. */
  p = 0x543210;

  while ((c = *sp) != MAX_TREES)
  {
    Int v,z,l,h;

    assert(c < s->num_trees);
    assert((size_t)(sp - s->selector) < s->num_selectors);

    v = p ^ (0x111111 * c);
    z = (v + 0xEEEEEF) & 0x888888;
    l = z ^ (z-1);
    h = ~l;
    p = (p | l) & ((p << 4) | h | c);
#if GNUC_VERSION >= 30406
    j = (__builtin_ctz(h) >> 2) - 1;
#else
    h &= -h;
    j = !!(h & 0x01010100);
    h |= h >> 4;
    j |= h >> 11;
    j |= h >> 18;
    j &= 7;
#endif
    sp++;
    *smp++ = j;
    cost += j+1;
  }

  /* Add zero to seven dummy selectors in order to make block size
     multiply of 8 bits. */
  j = cost & 0x7;
  j = (8 - j) & 0x7;
  s->num_selectors += j;
  cost += j;

  assert(j <= 7);
  assert(cost % 8 == 0);

  while (j--)
    *smp++ = 0;

  /* Calculate the cost of transmitting character map. */
  for (i = 0; i < 16; i++)
  {
    pk = 0;
    for (j = 0; j < 16; j++)
      pk |= s->cmap[16*i+j];
    cost += pk << 4;
  }
  cost += 16;  /* Big bucket costs 16 bits on its own. */

  /* Convert cost from bits to bytes. */
  assert(cost % 8 == 0);
  cost >>= 3;

  s->out_expect_len = cost;

  *crc = s->block_crc;

  return cost;
}

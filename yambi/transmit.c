/*-
  Copyright (C) 2011 Mikolaj Izdebski.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "encode.h"

#define SEND(n,v)                               \
  b = (b << (n)) | (v);                         \
  if ((k += n) >= 32) {                         \
    Int w = (Int)(b >> (k -= 32));              \
    p[0]=w>>24; p[1]=w>>16; p[2]=w>>8; p[3]=w;  \
    p += 4;                                     \
  }

void
YBenc_transmit(YBenc_t *s, void *buf)
{
  Long b = 0;
  Int k = 0;
  Byte *sp;
  Int t, v;
  Short *mtfv;
  Byte *p = buf;
  Int ns, as;
  Int gr;
  Int nmtf = s->nmtf;

  SEND(24, 0x314159);
  SEND(24, 0x265359);
  SEND(32, s->block_crc ^ 0xFFFFFFFF);

  SEND(1, 0);               /* non-rand */
  SEND(24, s->bwt_idx);     /* bwt primary index */

  /* cmap */
  {
    Int pack[16], pk, i, j, big = 0;

    for (i = 0; i < 16; i++)
    {
      pk = 0;
      for (j = 0; j < 16; j++)
        pk = (pk << 1) | s->cmap[16*i+j];
      pack[i] = pk;
      big = (big << 1) | !!pk;
    }

    SEND(16,big);
    for (i = 0; i < 16; i++)
      if (pack[i]) { SEND(16,pack[i]); }
  }

  /* selectors */
  assert(s->num_trees >= 2 && s->num_trees <= 6);
  SEND(3, s->num_trees);
  ns = s->num_selectors;
  SEND(15, ns);
  sp = s->selectorMTF;
  while (ns--)
  {
    v = 1 + *sp++;
    assert(v >= 1 && v <= 6);
    SEND(v, (1<<v)-2);
  }

  mtfv = (Short *)s->ptr;
  as = mtfv[nmtf-1]+1;
  ns = (nmtf + GROUP_SIZE - 1) / GROUP_SIZE;

  /* trees */
  for (t = 0; t < s->num_trees; t++)
  {
    SInt a, c;
    Byte *len = s->length[t];
    a = len[0];
    assert(a >= 1 && a <= 20);
    SEND(6,a<<1);
    for (v = 1; v < as; v++)
    {
      c = len[v];
      assert(c >= 1 && c <= 20);
      while (a < c) { SEND(2,2); a++; }
      while (a > c) { SEND(2,3); a--; }
      SEND(1,0);
    }
  }

  for (gr = 0; gr < ns; gr++)
  {
    Long c;
    Byte l;
    Short mv;
    Int *L;
    Byte *B;

    t = s->selector[gr];
    L = s->lookup[t];
    B = s->length[t];
#define ITER(nn) mv = mtfv[nn]; c = L[mv]; l = B[mv]; SEND(l,c)
    ITER( 0); ITER( 1); ITER( 2); ITER( 3); ITER( 4);
    ITER( 5); ITER( 6); ITER( 7); ITER( 8); ITER( 9);
    ITER(10); ITER(11); ITER(12); ITER(13); ITER(14);
    ITER(15); ITER(16); ITER(17); ITER(18); ITER(19);
    ITER(20); ITER(21); ITER(22); ITER(23); ITER(24);
    ITER(25); ITER(26); ITER(27); ITER(28); ITER(29);
    ITER(30); ITER(31); ITER(32); ITER(33); ITER(34);
    ITER(35); ITER(36); ITER(37); ITER(38); ITER(39);
    ITER(40); ITER(41); ITER(42); ITER(43); ITER(44);
    ITER(45); ITER(46); ITER(47); ITER(48); ITER(49);
#undef ITER
    mtfv += GROUP_SIZE;
  }

  assert(k % 8 == 0);
  while (k > 0)
    *p++ = (Byte)(b >> (k -= 8));

  assert(p == (Byte *)buf + s->out_expect_len);
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

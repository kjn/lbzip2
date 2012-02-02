/*-
  emit.c -- RLE decoder

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

#include <stdlib.h>  /* abort() */

#include "decode.h"


#define M1 0xFFFFFFFFu


/* Returns bytes still remaining in output buffer (zero if all bytes were
   exhausted).  Returns (Int)-1 if data error was detected.
*/
static Int
emit_data(YBdec_t *state, Byte *b, Int m)
{
  Int p;    /* ibwt linked list pointer */
  Int a;    /* available input bytes */
  Int s;    /* crc checksum */
  Byte c;   /* current character */
  Byte d;   /* next character */
  Int r;    /* run length */
  Int *t = state->tt;

  assert(b);

  if (unlikely(state->rle_state == 0xDEAD))
    return m;

  s = state->rle_crc;
  p = state->rle_index;
  a = state->rle_avail;
  c = state->rle_char;
  d = state->rle_prev;


  if (a == M1)
    return m;

  /*=== UNRLE FINITE STATE AUTOMATON ===*/
  /* There are 6 states, numbered from 0 to 5. */

  /* Excuse me, but the following is a write-only code.  It wasn't written
     for readability or maintainability, but rather for high efficiency. */
  switch (state->rle_state) {
  default:
    abort();
  case 1:
    if (unlikely(!m--)) break;
    s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d) break;
    if (unlikely(!a--)) break;
    c = p = t[p >> 8];
  case 2:
    if (unlikely(!m--)) { state->rle_state = 2; break; }
    s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d) break;
    if (unlikely(!a--)) break;
    c = p = t[p >> 8];
  case 3:
    if (unlikely(!m--)) { state->rle_state = 3; break; }
    s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
    if (c != d) break;
    if (unlikely(!a--)) return M1;
    c = p = t[p >> 8];
  case 4:
    if (unlikely(m < (r = c))) {
      c -= m;
      while (m--)
        s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = d)];
      state->rle_state = 4; break;
    }
    m -= r;
    while (r--)
      s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = d)];
  case 0:
    if (unlikely(!a--)) break;
    c = p = t[p >> 8];
  case 5:
    if (unlikely(!m--)) { state->rle_state = 5; break; }
    s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
  }

  if (likely(a != M1 && m != M1)) {
    for (;;) {
      if (unlikely(!a--)) break;
      d = c;
      c = p = t[p >> 8];
      if (unlikely(!m--)) { state->rle_state = 1; break; }
      s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
      if (likely(c != d)) {
        if (unlikely(!a--)) break;
        d = c;
        c = p = t[p >> 8];
        if (unlikely(!m--)) { state->rle_state = 1; break; }
        s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
        if (likely(c != d)) {
          if (unlikely(!a--)) break;
          d = c;
          c = p = t[p >> 8];
          if (unlikely(!m--)) { state->rle_state = 1; break; }
          s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
          if (likely(c != d)) {
            if (unlikely(!a--)) break;
            d = c;
            c = p = t[p >> 8];
            if (unlikely(!m--)) { state->rle_state = 1; break; }
            s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
            if (c != d) continue;
          }
        }
      }
      if (unlikely(!a--)) break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) { state->rle_state = 2; break; }
      s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
      if (c != d) continue;
      if (unlikely(!a--)) break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) { state->rle_state = 3; break; }
      s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
      if (c != d) continue;
      if (unlikely(!a--)) return M1;
      if (m < (r = (p = t[p >> 8]) & 0xff)) {
        c = r - m;
        while (m--)
          s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = d)];
        state->rle_state = 4;
        break;
      }
      m -= r;
      while (r--)
        s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = d)];
      if (unlikely(!a--)) break;
      c = p = t[p >> 8];
      if (unlikely(!m--)) { state->rle_state = 5; break; }
      s = (s << 8) ^ YB_crc_table[(s >> 24) ^ (*b++ = c)];
    }
  }

  /* Exactly one of `a' and `m' is equal to M1. */
  assert((a == M1) != (m == M1));

  state->rle_avail = a;
  if (m == M1) {
    assert(a != M1);
    state->rle_index = p;
    state->rle_char = c;
    state->rle_prev = d;
    state->rle_crc = s;
    assert(state->rle_state != 0xDEAD);
    return 0;
  }

  /* The 0xDEAD magic number indicates that UnRLE has finished. */
  assert(a == M1);
  state->rle_crc = s ^ 0xFFFFFFFF;
  state->rle_state = 0xDEAD;
  return m;
}


/* This function is basically a wrapper around emit_data().  */
int
YBdec_emit(YBdec_t *dec, void *buf, size_t *buf_sz, YBcrc_t *crc)
{
  Int rv;

  assert(dec != 0);
  assert(buf != 0);
  assert(buf_sz && *buf_sz > 0);

  rv = emit_data(dec, buf, *buf_sz);
  *crc = dec->rle_crc;

  if (rv == M1)
  {
    return YB_ERR_RUNLEN;
  }

  if (dec->rle_state == 0xDEAD)
  {
    if (dec->rle_crc != dec->expect_crc)
    {
      return YB_ERR_BLKCRC;
    }

    *buf_sz = rv;
    return YB_OK;
  }

  assert(rv == 0);
  *buf_sz = 0;
  return YB_UNDERFLOW;
}

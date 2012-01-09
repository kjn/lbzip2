/*-
  collect.c -- initial RLE encoder

  Copyright (C) 2011 Mikolaj Izdebski

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

#include <stdlib.h>  /* free() */

#include "xalloc.h"  /* xmalloc() */

#include "encode.h"


#define CRC(x) crc = (crc << 8) ^ YB_crc_table[(crc >> 24) ^ (x)]

#define MAX_RUN_LENGTH (4+255)
#define RUN_DONE      (-1)


YBobs_t *
YBobs_init(unsigned long max_block_size, void *buf)
{
  YBobs_t *obs = xmalloc(sizeof(YBobs_t));
  Byte *p = buf;
  unsigned long bs100k = (max_block_size + 100000 - 1) / 100000;

  *p++ = 0x42;
  *p++ = 0x5A;
  *p++ = 0x68;
  *p++ = 0x30 + bs100k;

  obs->crc = 0;

  return obs;
}

void
YBobs_destroy(YBobs_t *obs)
{
  free(obs);
}

void
YBobs_join(YBobs_t *obs, const YBcrc_t *crc)
{
  obs->crc = ((obs->crc << 1) ^ (obs->crc >> 31) ^ *crc ^ 0xFFFFFFFF)
    & 0xFFFFFFFF;
}

void
YBobs_finish(YBobs_t *obs, void *buf)
{
  Byte *p = buf;
  *p++ = 0x17;
  *p++ = 0x72;
  *p++ = 0x45;
  *p++ = 0x38;
  *p++ = 0x50;
  *p++ = 0x90;
  *p++ = obs->crc >> 24;
  *p++ = (obs->crc >> 16) & 0xFF;
  *p++ = (obs->crc >> 8) & 0xFF;
  *p++ = obs->crc & 0xFF;
}


YBenc_t *
YBenc_init(unsigned long max_block_size,
           unsigned shallow_factor,
           unsigned prefix_factor)
{
  int i;
  YBenc_t *s = xmalloc(sizeof(YBenc_t));

  /* Using assertions to guard parameters is fine because the documentation
     states explicitly states that passing illegal arguments causes undefined
     behaviour. */
  assert(s != 0);
  assert(max_block_size > 0 && max_block_size <= 900000);
  assert(shallow_factor <= 65535);
  assert(prefix_factor > 0 && prefix_factor <= 65535);

  s->rle_state = 0;
  s->nblock = 0;
  s->max_block_size = max_block_size;
  s->block_crc = 0xFFFFFFFF;
  s->shallow_factor = shallow_factor;
  s->prefix_factor = prefix_factor;

  s->cmap = xmalloc(256 * sizeof(Byte));
  s->selector = xmalloc((18000+1+1) * sizeof(Byte));
  s->selectorMTF = xmalloc((18000+1+7) * sizeof(Byte));
  s->block = xmalloc((max_block_size+1) * sizeof(Byte));


  for (i = 0; i < 256; i++)
    s->cmap[i] = 0;

  return s;
}


void
YBenc_destroy(YBenc_t *enc)
{
  assert(enc != 0);

  free(enc->cmap);
  free(enc->selector);
  free(enc->selectorMTF);
  free(enc->mtfv);

  free(enc);
}


/* Return number of bytes consumed. */
static Int
collect_data(YBenc_t *s, const Byte *inbuf, Int avail)
{
  /* Cache some often used member variables for faster access. */
  const Byte *p = inbuf;
  const Byte *pLim = p + avail;
  Byte *q = s->block + s->nblock;
  Byte *qMax = s->block + s->max_block_size - 1;
  Int crc = s->block_crc;
  Byte ch, last;
  Int run;
  Byte *cmap = s->cmap;

  /* State can't be equal to MAX_RUN_LENGTH because the run would have
     already been dumped by the previous function call. */
  assert(s->rle_state >= 0 && s->rle_state < MAX_RUN_LENGTH);

  /* Finish any existing runs before starting a new one. */
  if (unlikely(s->rle_state != 0))
  {
    ch = s->rle_character;
    goto finish_run;
  }

state0:
  /*=== STATE 0 ===*/
  if (unlikely(q > qMax))
  {
    s->rle_state = RUN_DONE;
    goto done;
  }
  if (unlikely(p == pLim))
  {
    s->rle_state = 0;
    goto done;
  }
  ch = *p++;
  CRC(ch);

#define S1                                      \
  cmap[ch] = 1;                                 \
  *q++ = ch;                                    \
  if (unlikely(q > qMax))                       \
  {                                             \
    s->rle_state = RUN_DONE;                    \
    goto done;                                  \
  }                                             \
  if (unlikely(p == pLim))                      \
  {                                             \
    s->rle_state = 1;                           \
    s->rle_character = ch;                      \
    goto done;                                  \
  }                                             \
  last = ch;                                    \
  ch = *p++;                                    \
  CRC(ch);                                      \
  if (unlikely(ch == last))                     \
    goto state2

state1:
  /*=== STATE 1 ===*/
  S1; S1; S1; S1;
  goto state1;

state2:
  /*=== STATE 2 ===*/
  *q++ = ch;
  if (unlikely(q > qMax))
  {
    s->rle_state = RUN_DONE;
    goto done;
  }
  if (unlikely(p == pLim))
  {
    s->rle_state = 2;
    s->rle_character = ch;
    goto done;
  }
  ch = *p++;
  CRC(ch);
  if (ch != last)
    goto state1;

  /*=== STATE 3 ===*/
  *q++ = ch;

  if (unlikely(q >= qMax &&
               (q > qMax || (p < pLim && *p == last))))
  {
    s->rle_state = RUN_DONE;
    goto done;
  }
  if (unlikely(p == pLim))
  {
    s->rle_state = 3;
    s->rle_character = ch;
    goto done;
  }
  ch = *p++;
  CRC(ch);
  if (ch != last)
    goto state1;

  /*=== STATE 4+ ===*/
  assert(q < qMax);
  *q++ = ch;

  /* While the run is shorter than MAX_RUN_LENGTH characters,
     keep trying to append more characters to it. */
  for (run = 4; run < MAX_RUN_LENGTH; run++)
  {
    /* Check for end of input buffer. */
    if (unlikely(p == pLim))
    {
      s->rle_state = run;
      s->rle_character = ch;
      goto done;
    }

    /* Fetch the next character. */
    ch = *p++;
    s->block_crc = crc;
    CRC(ch);

    /* If the character does not match, terminate
       the current run and start a fresh one. */
    if (ch != last)
    {
      *q++ = run-4;
      cmap[run-4] = 1;
      if (likely(q <= qMax))
        goto state1;

      /* There is no space left to begin a new run.
         Unget the last character and finish. */
      p--;
      crc = s->block_crc;
      s->rle_state = RUN_DONE;
      goto done;
    }
  }

  /* The run has reached maximal length,
     so it must be ended prematurely. */
  *q++ = MAX_RUN_LENGTH-4;
  cmap[MAX_RUN_LENGTH-4] = 1;
  goto state0;

finish_run:
  /* There is an unfinished run from the previous call, try to finish it. */
  if (q >= qMax &&
      (q > qMax || (s->rle_state == 3 && p < pLim && *p == ch)))
  {
    s->rle_state = RUN_DONE;
    goto done;
  }

  /* We have run out of input bytes before finishing the run. */
  if (p == pLim)
    goto done;

  /* If the run is at least 4 characters long, treat it specifically. */
  if (s->rle_state >= 4)
  {
    /* Make sure we really have a long run. */
    assert(s->rle_state >= 4);
    assert(q <= qMax);

    while (p < pLim)
    {
      /* Lookahead the next character. Terminate current run
         if lookahead character doesn't match. */
      if (*p != ch)
      {
        *q++ = s->rle_state-4;
        cmap[s->rle_state-4] = 1;
        goto state0;
      }

      /* Lookahead character turned out to be continuation of the run.
         Consume it and increase run length. */
      p++;
      CRC(ch);
      s->rle_state++;

      /* If the run has reached length of MAX_RUN_LENGTH,
         we have to terminate it prematurely (i.e. now). */
      if (s->rle_state == MAX_RUN_LENGTH)
      {
        *q++ = MAX_RUN_LENGTH-4;
        cmap[MAX_RUN_LENGTH-4] = 1;
        goto state0;
      }
    }

    /* We have ran out of input bytes before finishing the run. */
    goto done;
  }

  /* Lookahead the next character. Terminate current run
     if lookahead character does not match. */
  if (*p != ch)
    goto state0;

  /* Append the character to the run. */
  p++;
  CRC(ch);
  s->rle_state++;
  *q++ = ch;

  /* We haven't finished the run yet, so keep going. */
  goto finish_run;

done:
  s->nblock = q - s->block;
  s->block_crc = crc;
  return p-inbuf;
}


int
YBenc_collect(YBenc_t *enc, const void *buf, size_t *buf_sz)
{
  *buf_sz -= collect_data(enc, buf, *buf_sz);

  if (enc->rle_state == RUN_DONE)
    return YB_OVERFLOW;
  return YB_OK;
}

/*-
  Copyright (C) 2011 Mikolaj Izdebski

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>  /* free() */

#include "xalloc.h"  /* xmalloc() */

#include "decode.h"

#if 0
# include <stdio.h>
# define Trace(x) fprintf x
#else
# define Trace(x)
#endif


/* Block size theshold above which block randomization has any effect.
   Randomizing blocks of size <= RAND_THRESH is a no-op.
*/
#define RAND_THRESH 617

/* A table filled with arbitrary numbers, in range 50-999, used for
   derandomizing randomized blocks.  These numbers are strictly related
   to the bzip2 file format and they are not subject to change.
*/
static const Short rand_table[512] = { 
  619,720,127,481,931,816,813,233,566,247,985,724,205,454,863,491,
  741,242,949,214,733,859,335,708,621,574, 73,654,730,472,419,436,
  278,496,867,210,399,680,480, 51,878,465,811,169,869,675,611,697,
  867,561,862,687,507,283,482,129,807,591,733,623,150,238, 59,379,
  684,877,625,169,643,105,170,607,520,932,727,476,693,425,174,647,
   73,122,335,530,442,853,695,249,445,515,909,545,703,919,874,474,
  882,500,594,612,641,801,220,162,819,984,589,513,495,799,161,604,
  958,533,221,400,386,867,600,782,382,596,414,171,516,375,682,485,
  911,276, 98,553,163,354,666,933,424,341,533,870,227,730,475,186,
  263,647,537,686,600,224,469, 68,770,919,190,373,294,822,808,206,
  184,943,795,384,383,461,404,758,839,887,715, 67,618,276,204,918,
  873,777,604,560,951,160,578,722, 79,804, 96,409,713,940,652,934,
  970,447,318,353,859,672,112,785,645,863,803,350,139, 93,354, 99,
  820,908,609,772,154,274,580,184, 79,626,630,742,653,282,762,623,
  680, 81,927,626,789,125,411,521,938,300,821, 78,343,175,128,250,
  170,774,972,275,999,639,495, 78,352,126,857,956,358,619,580,124,
  737,594,701,612,669,112,134,694,363,992,809,743,168,974,944,375,
  748, 52,600,747,642,182,862, 81,344,805,988,739,511,655,814,334,
  249,515,897,955,664,981,649,113,974,459,893,228,433,837,553,268,
  926,240,102,654,459, 51,686,754,806,760,493,403,415,394,687,700,
  946,670,656,610,738,392,760,799,887,653,978,321,576,617,626,502,
  894,679,243,440,680,879,194,572,640,724,926, 56,204,700,707,151,
  457,449,797,195,791,558,945,679,297, 59, 87,824,713,663,412,693,
  342,606,134,108,571,364,631,212,174,643,304,329,343, 97,430,751,
  497,314,983,374,822,928,140,206, 73,263,980,736,876,478,430,305,
  170,514,364,692,829, 82,855,953,676,246,369,970,294,750,807,827,
  150,790,288,923,804,378,215,828,592,281,565,555,710, 82,896,831,
  547,261,524,462,293,465,502, 56,661,821,976,991,658,869,905,758,
  745,193,768,550,608,933,378,286,215,979,792,961, 61,688,793,644,
  986,403,106,366,905,644,372,567,466,434,645,210,389,550,919,135,
  780,773,635,389,707,100,626,958,165,504,920,176,193,713,857,265,
  203, 50,668,108,645,990,626,197,510,357,358,850,858,364,936,638,
};


/* Implementation of Sliding Lists algorithm for doing Inverse Move-To-Front
   (IMTF) transformation in O(n) space and amortised O(sqrt(n)) time.
   The naive IMTF algorithm does the same in both O(n) space and time.
   Generally IMTF can be done in O(log(n)) time, but a quite big constant
   factor makes such algorithms impractical for MTF of 256 items.

   TODO: add more comments before I forget how this worked :)
*/
static Byte
mtf_one(YBdec_t *state, Byte c)
{
  Byte *pp;

  /* We expect the index to be small, so we have a special case for that. */
  if (likely(c < IMTF_ROW_WIDTH))
  {
    Int nn = c;
    pp = state->imtf_row[0];
    c = pp[nn];

    /* Forgive me the ugliness of this code, but mtf_one() is executed
       frequently and needs to be fast.  An eqivalent (simpler and slower)
       version is given in #else clause.
    */
#if IMTF_ROW_WIDTH == 16
    switch (nn) {
    default:
      abort();
#define R(n) case n: pp[n] = pp[n-1]
      R(15); R(14); R(13); R(12); R(11); R(10); R( 9);
      R( 8); R( 7); R( 6); R( 5); R( 4); R( 3); R( 2); R( 1);
#undef R
    }
#else
    while (nn > 0)
    {
      pp[nn] = pp[nn-1];
      nn--;
    }
#endif
  }
  else  /* A general case for indices >= IMTF_ROW_WIDTH. */
  {
    /* If the sliding list already reached the bottom of memory pool
       allocated for it, we need to rebuild it. */
    if (unlikely(state->imtf_row[0] == state->imtf_slide))
    {
      Byte *kk = state->imtf_slide + IMTF_SLIDE_LENGTH;
      Byte **rr = state->imtf_row + IMTF_NUM_ROWS;

      while (rr > state->imtf_row)
      {
        Byte *bg = *--rr;
        Byte *bb = bg + IMTF_ROW_WIDTH;

        assert(bg >= state->imtf_slide &&
               bb <= state->imtf_slide + IMTF_SLIDE_LENGTH);

        while (bb > bg)
          *--kk = *--bb;
        *rr = kk;
      }
    }

    {
      Byte **lno = state->imtf_row + c / IMTF_ROW_WIDTH;
      Byte *bb = *lno;

      pp = bb + c % IMTF_ROW_WIDTH;
      c = *pp;

      while (pp > bb)
      {
        Byte *tt = pp--;
        *tt = *pp;
      }

      while (lno > state->imtf_row)
      {
        Byte **lno1 = lno;
        pp = --(*--lno);
        **lno1 = pp[IMTF_ROW_WIDTH];
      }
    }
  }

  *pp = c;
  return c;
}


int
YBdec_work(YBdec_t *state)
{
  Short s;
  Int i, j = 0;
  Int cum;
  Byte uc;

  Byte runChar = state->imtf_slide[IMTF_SLIDE_LENGTH - 256];
  Int shift = 0;
  Int r = 0;

  Int ftab[256];  /* frequency table used in counting sort */

  const Short *tt16;
  Int *tt;

  assert(state->state == 0);

  /* Initialise IBWT frequency table. */
  for (s = 0; s < 256; s++)
    ftab[s] = 0;

  /* Initialise IMTF decoding structure. */
  for (i = 0; i < IMTF_NUM_ROWS; i++)
    state->imtf_row[i] = state->imtf_slide + IMTF_SLIDE_LENGTH - 256
      + i * IMTF_ROW_WIDTH;

  tt = xmalloc(900000u * sizeof(Int));
  state->tt = tt;
  tt16 = state->tt16;

  for (i = 0; i < state->num_mtfv; i++)
  {
    s = tt16[i];

    /* If we decoded a RLE symbol, increase run length and keep going.
       However, we need to stop accepting RLE symbols if the run gets
       too long.  Note that rejcting further RLE symbols after the run
       has reached the length of 900k bytes is perfectly correct because
       runs longer than 900k bytes will cause block overflow anyways
       and hence stop decoding with an error. */
    if (s >= 256 && r <= 900000)
    {
      r += (s & 3) << shift++;
      continue;
    }

    /* At this point we most likely have a run of one or more bytes.
       Zero-length run is possible only at the beginning, once per block,
       so any optimizations inolving zero-length runs are pointless. */
    if (unlikely(j + r > 900000))
      return YB_ERR_OVERFLOW;
    ftab[runChar] += r;
    while (r--)
      state->tt[j++] = runChar;

    runChar = mtf_one(state, s);
    shift = 0;
    r = 1;
  }

  /* Reclaim memory occupied by tt16 as it's no longer needed. */
  free(state->tt16);
  state->tt16 = 0;

  /* At this point we must have an unfinished run, let's finish it. */
  assert(r > 0);
  if (unlikely(j + r > 900000))
    return YB_ERR_OVERFLOW;
  ftab[runChar] += r;
  while (r--)
    state->tt[j++] = runChar;

  assert(j >= state->num_mtfv);
  state->block_size = j;

  /* Sanity-check the BWT primary index. */
  if (state->bwt_idx >= state->block_size)
  {
    Trace((stderr, "primary index %u too large, should be %u max.\n",
           (unsigned)state->bwt_idx, (unsigned)state->block_size));
    return YB_ERR_BWTIDX;
  }

  /* Transform counts into indices (cumulative counts). */
  cum = 0;
  for (i = 0; i < 256; i++)
    ftab[i] = (cum += ftab[i]) - ftab[i];
  assert(cum == state->block_size);


  /* Construct the IBWT singly-linked cyclic list.  Traversing that list
     starting at primary index produces the source string.

     Each list node consists of a pointer to the next node and a character
     of the source string.  Those 2 values are packed into a single 32bit
     integer.  The character is kept in bits 0-7 and the pointer in stored
     in bits 8-27.  Bits 28-31 are unused (always clear).

     Note: Iff the source string consists of a string repeated k times
     (eg. ABABAB - the string AB is repeated k=3 times) then this algorithm
     will construct k independant (not connected), isomorphic lists.
  */
  for (i = 0; i < state->block_size; i++)
  {
    uc = state->tt[i];
    state->tt[ftab[uc]] |= (i << 8);
    ftab[uc]++;
  }
  assert(ftab[255] == state->block_size);

  state->rle_index = state->tt[state->bwt_idx];
  state->rle_avail = state->block_size;

  /* Derandomize the block if necessary.

     The derandomization algorithm is implemented inefficiently, but the
     assumption is that randomized blocks are unlikely to be encountered.
     Most of bzip2 implementations try to avoid randomizing blocks because
     it usually leads to decreased compression ratio.
   */
  if (unlikely(state->rand && state->block_size > RAND_THRESH))
  {
    Byte *block;

    Trace((stderr, "A randomized block of size %u was found.\n",
           state->block_size));

    /* Allocate a temporary array to hold the block. */
    block = xmalloc(state->block_size);

    /* Copy the IBWT linked list into the termporary array. */
    j = state->rle_index;
    for (i = 0; i < state->block_size; i++) {
      j = state->tt[j >> 8];
      block[i] = j;
    }

    /* Derandomize the block. */
    i = 0, j = RAND_THRESH;
    do {
      block[j] ^= 1;
      i = (i + 1) & 0x1FF;
      j += rand_table[i];
    } while (j < state->block_size);

    /* Reform a linked list from the array. */
    for (i = 0; i < state->block_size; i++)
      state->tt[i] = ((i+1) << 8) | block[i];
    state->rle_index = 0;

    /* Release the temoprary array. */
    free(block);
  }

  return 0;
}


const char *
YBerr_detail(int code)
{
  static const char *msg_table[] = {
    "bad stream header magic", /* YB_ERR_MAGIC */
    "bad block header magic",  /* YB_ERR_HEADER */
    "empty source alphabet",   /* YB_ERR_BITMAP */
    "bad number of trees",     /* YB_ERR_TREES */
    "no coding groups",        /* YB_ERR_GROUPS */
    "invalid selector",        /* YB_ERR_SELECTOR */
    "invalid delta code",      /* YB_ERR_DELTA */
    "invalid prefix code",     /* YB_ERR_PREFIX */
    "incomplete prefix code",  /* YB_ERR_INCOMPLT */
    "empty block",             /* YB_ERR_EMPTY */
    "unterminated block",      /* YB_ERR_UNTERM */
    "missing run length",      /* YB_ERR_RUNLEN */
    "block CRC mismatch",      /* YB_ERR_BLKCRC */
    "stream CRC mismatch",     /* YB_ERR_STRMCRC */
    "block overflow",          /* YB_ERR_OVERFLOW */
    "primary index too large", /* YB_ERR_BWTIDX */
  };

  /* It's the caller's responsibility to check non-error cases. */
  assert(code <= YB_ERR_MAGIC && code >= YB_ERR_BWTIDX);

  return msg_table[-code + YB_ERR_MAGIC];
}

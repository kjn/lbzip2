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

#include <config.h>

#include "decode.h"


#define Trace(x)



#define STREAM_MAGIC_MIN 0x425A6831
#define STREAM_MAGIC_MAX 0x425A6839
#define HEADER_MAGIC_HI  0x314159
#define HEADER_MAGIC_LO  0x265359
#define TRAILER_MAGIC_HI 0x177245
#define TRAILER_MAGIC_LO 0x385090



/* Start table width */
#define SW HUFF_START_WIDTH


/*
  Notes on prefix code decoding:

  1) Width of a tree node is defined as 2^-d, where d is depth of that node.
  A prefix tree is said to be full iff all leaf widths sum to 1.  If this sum
  is less (greater) than 1, we say the tree is incomplete (oversubscribed).
  See also: Kraft's inequality

  2) In this implementation, malformed trees (oversubscribed or incomplete)
  aren't rejected directly at creation (that's the moment when both bad cases
  are detected).  Instead, invalid trees cause decode error only when they are
  actually used to decode a group.  This is noncomforming behavior -- the
  original bzip2, which serves as a reference implementation, accepts
  malformed trees as long as nonexistent codes don't appear in compressed
  stream.  Neither bzip2 nor any alternative implementation I know produces
  such trees, so this behaviour seems sane.
*/


/* Given a list of code lengths, make a set of tables to decode that set
   of codes.  Return zero on success (the tables are built only in this
   case), 7 if the given code set is incomplete, 6 if the input is invalid
   (an oversubscribed set of lengths).

   The algorithm implemented here was inspired by "On the implementation of
   minimum-redundancy prefix codes" by Alistair Moffat and Andrew Turpin,
   but the code below doesn't diectly follow the description in the paper.
*/
static int
make_tree(YBibs_t *ibs,   /* the IBS where to store created tables */
          Int t,          /* tree index */
          const Byte *L,  /* code lengths */
          Int n)          /* alphabet size */
{
  Int   *C;  /* code length count; C[0] is a sentinel (always zero) */
  Long  *B;  /* left-justified base */
  Short *P;  /* symbols sorted in ascending code length order */
  Short *S;  /* lookup table */

  int    k;  /* current code length */
  Int    s;  /* current symbol */
  Long sofar;
  Long next;
  Int cum;
  Int code;
  Long inc;

  Long v;

  C = ibs->tree[t].count;
  B = ibs->tree[t].base;
  P = ibs->tree[t].perm;
  S = ibs->tree[t].start;

  /* Count symbol lengths. */
  for (k = 0; k <= MAX_CODE_LENGTH; k++)
    C[k] = 0;
  for (s = 0; s < n; s++) {
    k = L[s];
    C[k]++;
  }
  /* Make sure there are no zero-length codes. */
  assert(C[0] == 0);

  /* Check if Kraft's inequality is satisfied. */
  sofar = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++)
    sofar += (Long)C[k] << (20-k);
  if (sofar != (1<<20))
  {
    if (sofar < (1<<20)) return 7;
    return 6;
  }

  /* Create left-justified base table. */
  sofar = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++)
  {
    next = sofar + ((Long)C[k] << (64-k));
    assert(next == 0 || next >= sofar);
    B[k] = sofar;
    sofar = next;
  }
  /* Ensure that "sofar" has overflowed to zero. */
  assert(sofar == 0);

  /* The last few entries of lj-base may have overflowed to zero,
     so replace all trailing zeros with the greatest possible 64-bit value
     (which is greater than the greatest possible left-justified base).
  */
  assert(k == MAX_CODE_LENGTH+1);
  k = MAX_CODE_LENGTH;
  while (C[k] == 0)
  {
    assert(k > MIN_CODE_LENGTH);
    assert(B[k] == 0);
    B[k--] = ~(Long)0;
  }

  /* Transform counts into indices (cumulative counts). */
  cum = 0;
  for (k = MIN_CODE_LENGTH; k <= MAX_CODE_LENGTH; k++)
  {
    Int t1 = C[k];
    C[k] = cum;
    cum += t1;
  }
  assert(cum == n);

  /* Perform counting sort.
     Note: Internal symbol values differ from that used in BZip2!
     Here 0 denotes EOB, 1-255 are MTF values, 256 is unused and 257-258
     are ZRLE symbols (RUN-A and RUN-B).
  */
  P[C[L[0]]++] = 257;  /* RUN-A */
  P[C[L[1]]++] = 258;  /* RUN-B */
  for (s = 2; s < n-1; s++)
    P[C[L[s]]++] = s-1;  /* MTF-V */
  P[C[L[n-1]]++] = 0;  /* EOB */

  /* Create first, complete start entries. */
  code = 0;
  inc = 1 << (SW-1);
  for (k = 1; k <= SW; k++)
  {
    for (s = C[k-1]; s < C[k]; s++)
    {
      Short x = (P[s] << 5) | k;
      v = code;
      code += inc;
      while (v < code)
        S[v++] = x;
    }
    inc >>= 1;
  }

  /* Fill remaining, incomplete start entries. */
  assert(k == SW+1);
  sofar = (Long)code << (64-SW);
  while (code < (1 << SW)) {
    while (sofar >= B[k+1])
      k++;
    S[code] = k;
    code++;
    sofar += (Long)1 << (64-SW);
  }
  assert(sofar == 0);

  /* Restore cumulative counts as they were destroyed by the sorting
     phase.  The sentinel wasn't touched, so no need to restore it. */
  for (k = MAX_CODE_LENGTH; k > 0; k--)
  {
    C[k] = C[k-1];
  }
  assert(C[0] == 0);

  /* Valid tables were created successfully. */
  return 0;
}



#define REFILL(ss)                              \
  do                                            \
  {                                             \
    if (in_avail < 4)                           \
    {                                           \
      togo = 4;                                 \
      ibs->recv_state = ss;                     \
    case ss:                                    \
      while (in_avail && togo)                  \
      {                                         \
        w += 8;                                 \
        v |= (Long)*in++ << (64-w);             \
        in_avail--;                             \
        togo--;                                 \
      }                                         \
      if (togo)                                 \
        goto save_and_ret;                      \
    }                                           \
    else                                        \
    {                                           \
      v |= (Long)peekl(in) << (32-w);           \
      w += 32;                                  \
      in += 4;                                  \
      in_avail -= 4;                            \
    }                                           \
  }                                             \
  while (0)

#define REFILL_1(ss)                            \
  do                                            \
  {                                             \
    if (in_avail < 1)                           \
    {                                           \
      togo = 1;                                 \
      ibs->recv_state = ss;                     \
    case ss:                                    \
      while (in_avail && togo)                  \
      {                                         \
        w += 8;                                 \
        v |= (Long)*in++ << (64-w);             \
        in_avail--;                             \
        togo--;                                 \
      }                                         \
      if (togo)                                 \
        goto save_and_ret;                      \
    }                                           \
    else                                        \
    {                                           \
      v |= (Long)in[0] << (64-w-8);             \
      w += 8;                                   \
      in += 1;                                  \
      in_avail -= 1;                            \
    }                                           \
  }                                             \
  while (0)

#define GET(vv,nn,ss)                           \
  do                                            \
  {                                             \
    if (w < (nn)) { REFILL(ss); }               \
    (vv) = v >> (64-(nn));                      \
    w -= (nn);                                  \
    v <<= (nn);                                 \
  }                                             \
  while (0)

#define GET_SLOW(vv,nn,ss)                      \
  do                                            \
  {                                             \
    while (w < (nn)) { REFILL_1(ss); }          \
    (vv) = v >> (64-(nn));                      \
    w -= (nn);                                  \
    v <<= (nn);                                 \
  }                                             \
  while (0)


#define S_NEW_STREAM 0
#define S_DATA_BLOCK 1
#define S_DONE       2

#define S_MAGIC          10
#define S_HEADER_1       11
#define S_HEADER_2       12
#define S_CRC            13
#define S_RAND           14
#define S_BWT_IDX        15
#define S_BITMAP_BIG     16
#define S_BITMAP_SMALL   17
#define S_NUM_TREES      18
#define S_NUM_SELECTORS  19
#define S_SELECTOR_MTF   20
#define S_DELTA_BASE     21
#define S_DELTA_TAG      22
#define S_DELTA_INC      23
#define S_PREFIX         24
#define S_CRC2           25



YBibs_t *
YBibs_init()
{
  YBibs_t *ibs;

  ibs = xalloc(sizeof(YBibs_t));

  ibs->dec = 0;
  ibs->recv_state = S_NEW_STREAM;
  ibs->crc = 0;
  ibs->next_shift = 0;
  ibs->canceled = 0;

  ibs->save_v = 0;
  ibs->save_w = 0;
  ibs->save_big = 0;
  ibs->save_small = 0;
  ibs->save_i = 0;
  ibs->save_t = 0;
  ibs->save_s = 0;
  ibs->save_r = 0;
  ibs->save_j = 0;
  ibs->save_T = 0;
  ibs->save_x = 0;
  ibs->save_k = 0;
  ibs->save_g = 0;
  ibs->save_togo = 0;
  ibs->save_magic1 = 0;
  ibs->save_magic2 = 0;
  ibs->save_has_block = 0;

  return ibs;
}


YBdec_t *
YBdec_init(void)
{
  YBdec_t *dec;

  dec = xalloc(sizeof(YBdec_t));

  dec->ibs = 0;

  return dec;
}


void
YBibs_destroy(YBibs_t *ibs)
{
  assert(ibs != 0);
  xfree(ibs);
}


void
YBdec_destroy(YBdec_t *dec)
{
  assert(dec != 0);
  xfree(dec);
}


void
YBdec_join(YBdec_t *dec)
{
  assert(dec != 0);
  assert(dec->ibs != 0);

  if (dec->rle_state != 0xDEAD)
  {
    dec->ibs->canceled = 1;
  }
  else
  {
    dec->ibs->crc ^= ((dec->rle_crc >> dec->block_shift) |
                      (dec->rle_crc << (32 - dec->block_shift)));
  }
}


int
YBibs_check(YBibs_t *ibs)
{
  assert(ibs != 0);

  if (ibs->canceled)
    return YB_CANCELED;

  if (ibs->next_crc != ((ibs->crc << ibs->next_shift) |
                        (ibs->crc >> (32 - ibs->next_shift))))
  {
    Trace(("combined crc error (is 0x%08X, should be 0x%08X)\n",
           ((ibs->crc << ibs->next_shift) |
            (ibs->crc >> (32 - ibs->next_shift))), ibs->next_crc));
    return YB_ERR_STRMCRC;
  }

  return YB_OK;
}


/* TODO: add optimization similar to inflate_fast from gzip. */
/* TODO: add prefetchnta to avoid lookup tables pollution. */
int
YBibs_retrieve(YBibs_t *ibs, YBdec_t *dec, const void *buf, size_t *buf_sz)
{
  /* These two used to be function parameters, but the function
     prototype changed since then. */
  const Byte *in;  /* pointer to the next input byte */
  Int in_avail;    /* number of input bytes available */

  Long v;  /* next 0-64 bits of input stream, left aligned */
  int w;   /* available bits in v */
  Short big;    /* big descriptor of the bitmap */
  Short small;  /* small descriptor of the bitmap */
  int i;
  int t;  /* tree number (aka selector) */
  Int s;  /* symbol value */
  int r;  /* run length */
  Int j;

  struct Tree *T;

  Short x; /* lookahead bits */
  int k;   /* code length */
  int g;   /* group number */
  int togo;  /* number of bytes that still need to be read */
  Int magic1;
  Int magic2;
  int has_block;  /* bool, another block is present after current block */


  assert(dec != 0);
  assert(ibs != 0);
  assert(buf != 0);
  assert(dec->ibs == 0 || dec->ibs == ibs);
  assert(ibs->dec == 0 || ibs->dec == dec);

  in = (const Byte *)buf;
  in_avail = *buf_sz;


  /*=== RESTORE SAVED AUTOMATIC VARIABLES ===*/

#define RESTORE(var) var = ibs->save_##var
  RESTORE(v);
  RESTORE(w);
  RESTORE(big);
  RESTORE(small);
  RESTORE(i);
  RESTORE(t);
  RESTORE(s);
  RESTORE(r);
  RESTORE(j);
  RESTORE(T);
  RESTORE(x);
  RESTORE(k);
  RESTORE(g);
  RESTORE(togo);
  RESTORE(magic1);
  RESTORE(magic2);
  RESTORE(has_block);
#undef RESTORE


  switch (ibs->recv_state)
  {
  case S_DONE:
    *buf_sz = in_avail;
    return YB_DONE;

  case S_NEW_STREAM:

    /*=== RETRIEVE STREAM HEADER ===*/

    /* Read stream magic. */
    GET(magic1, 32, S_MAGIC);
    if (magic1 < STREAM_MAGIC_MIN || magic1 > STREAM_MAGIC_MAX)
    {
      Trace(("invalid stream magic\n"));
      return YB_ERR_MAGIC;
    }

    ibs->max_block_size = (magic1 - STREAM_MAGIC_MIN + 1) * 100000UL;


    /*=== RETRIEVE BLOCK HEADER ===*/

  block_header:
    GET(magic1, 24, S_HEADER_1);
    GET(magic2, 24, S_HEADER_2);

    if (magic1 == TRAILER_MAGIC_HI && magic2 == TRAILER_MAGIC_LO)
    {
      GET_SLOW(ibs->next_crc, 32, S_CRC);

      if (has_block)
      {
        has_block = 0;
        ibs->recv_state = S_DONE;
        ibs->save_v = v;
        ibs->save_w = w;
        *buf_sz = in_avail;
        return YB_OK;
      }

      assert(w == 0);
      *buf_sz = in_avail;
      return YB_DONE;
    }

    if (magic1 != HEADER_MAGIC_HI ||
        magic2 != HEADER_MAGIC_LO)
    {
      Trace(("invalid header magic.\n"));
      return YB_ERR_HEADER;
    }

    GET(ibs->next_crc, 32, S_CRC2);

    if (has_block)
    {
      has_block = 0;
      ibs->recv_state = S_DATA_BLOCK;
      ibs->save_v = v;
      ibs->save_w = w;
      *buf_sz = in_avail;
      return YB_OK;
    }


    /*=== RETRIEVE BLOCK DATA ===*/

  case S_DATA_BLOCK:

    ibs->dec = dec;
    dec->ibs = ibs;

    ibs->next_shift = (ibs->next_shift + 1) % 32;
    dec->block_shift = ibs->next_shift;
    dec->rle_state = 0;
    dec->rle_crc = 0xFFFFFFFF;
    dec->expect_crc = ibs->next_crc;

    /* Get rand flag. */
    GET(dec->rand, 1, S_RAND);

    /* Get BWT index. */
    GET(dec->bwt_idx, 24, S_BWT_IDX);


    /*=== RETRIEVE BITMAP ===*/

    k = 0;
    j = 0;
    GET(big, 16, S_BITMAP_BIG);
    small = 0;
    do {
      if (big & 0x8000) { GET(small, 16, S_BITMAP_SMALL); }
      do {
        dec->imtf_slide[IMTF_SLIDE_LENGTH - 256 + k] = j++;
        k += small >> 15;
        small <<= 1;
      } while (j & 0xF);
      big <<= 1;
    } while (j < 256);

    if (k == 0)
    {
      Trace(("empty alphabet\n"));
      return YB_ERR_BITMAP;
    }

    dec->alpha_size = k+2;


    GET(ibs->num_trees, 3, S_NUM_TREES);
    if (ibs->num_trees < MIN_TREES || ibs->num_trees > MAX_TREES)
    {
      Trace(("bad number of trees (%d, should be from 2 to 6)\n",
             ibs->num_trees));
      return YB_ERR_TREES;
    }

    GET(ibs->num_selectors, 15, S_NUM_SELECTORS);
    if (ibs->num_selectors == 0)
    {
      Trace(("no selectors\n"));
      return YB_ERR_GROUPS;
    }


    /*=== RETRIEVE SELECTOR MTF VALUES ===*/

    for (i = 0; i < ibs->num_selectors; i++)
    {
      /* The following is a lookup table for determining the position
         of the first zero bit (starting at the most significant bit)
         in a 6-bit integer.

         0xxxxx... -> 1
         10xxxx... -> 2
         110xxx... -> 3
         1110xx... -> 4
         11110x... -> 5
         111110... -> 6
         111111... -> no zeros (marked as 7)
      */
      static const Byte table[64] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                      1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                      2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                                      3,3,3,3,3,3,3,3,4,4,4,4,5,5,6,7, };

      if (w < 6)
        REFILL(S_SELECTOR_MTF);

      k = table[v >> (64-6)];

      if (unlikely(k > ibs->num_trees))
      {
        Trace(("bad selector mtfv\n"));
        return YB_ERR_SELECTOR;
      }

      v <<= k;
      w -= k;

      ibs->selector[i] = k-1;
    }


    /*=== RETRIEVE DECODING TABLES ===*/

    for (t = 0; t < ibs->num_trees; t++)
    {
      GET(x, 5, S_DELTA_BASE);

      for (s = 0; s < dec->alpha_size; s++)
      {
        /* Pattern L[] R[]
           0xxxxx   1   0
           100xxx   3  +1
           10100x   5  +2
           101010   6  +3
           101011   6  +1
           10110x   5   0
           101110   6  +1
           101111   6  -1
           110xxx   3  -1
           11100x   5   0
           111010   6  +1
           111011   6  -1
           11110x   5  -2
           111110   6  -1
           111111   6  -3
        */
        static const Byte L[64] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                    3,3,3,3,3,3,3,3,5,5,6,6,5,5,6,6,
                                    3,3,3,3,3,3,3,3,5,5,6,6,5,5,6,6, };

        static const Byte R[64] = { 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                                    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                                    4,4,4,4,4,4,4,4,5,5,6,4,3,3,4,2,
                                    2,2,2,2,2,2,2,2,3,3,4,2,1,1,2,0, };

        do {
          if (unlikely(w < 6))
            REFILL(S_DELTA_TAG);
          k = v >> (64-6);
          x += R[k];
          if (unlikely(x < 3+MIN_CODE_LENGTH || x > 3+MAX_CODE_LENGTH))
          {
            Trace(("bad code length\n"));
            return YB_ERR_DELTA;
          }
          x -= 3;
          k = L[k];
          w -= k;
          v <<= k;
        }
        while (unlikely(k == 6));

        ((Byte *)ibs->tree[t].start)[s] = x;
      }
      r = make_tree(ibs, t, (Byte *)ibs->tree[t].start,
                    dec->alpha_size);
      if (!r)
        r = t;
      ibs->mtf[t] = r;  /* Initialise MTF state. */
    }


    /*=== RETRIEVE BLOCK MTF VALUES ===*/

    /* Block MTF values (MTFV) are prefix-encoded with varying trees.
       MTFVs are divided into max. 18000 groups, each group contains 50 MTFVs
       (except the last one, which can contain from 1 to 50 MTFVs).

       Each group has assigned a prefix-free codebook.  As there are up to 6
       codebooks, the group's codebook number (called selector) is a value
       from 0 to 5.  A selector of 6 or 7 means oversubscribed or incomplete
       codebook.  If such selector is encountered, decoding is aborted.
     */

    j = 0;

    /* Bound selectors at 18001 so they don't overflow tt16[]. */
    if (ibs->num_selectors > 18001)
      ibs->num_selectors = 18001;

    for (g = 0; g < ibs->num_selectors; g++)
    {
      /* We started a new group, time to (possibly) switch to a new tree.
         We check the selector table to determine which tree to use.
         If the value we looked up is 6 or 7, it means decode error
         (values of 6 and 7 are special cases, they denote invalid trees). */
      i = ibs->selector[g];

      t = ibs->mtf[i];
      if (unlikely(t >= 6))
      {
        if (t == 6) {
          Trace(("oversubscribed prefix code\n"));
          return YB_ERR_PREFIX;
        }
        else {
          Trace(("incomplete prefix code\n"));
          return YB_ERR_INCOMPLT;
        }
      }

      while (i > 0)
      {
        ibs->mtf[i] = ibs->mtf[i-1];
        i--;
      }
      ibs->mtf[0] = t;

      T = &ibs->tree[t];

      /* There are up to GROUP_SIZE codes in any group. */
      assert(i == 0);
      for (/*i = 0*/; i < GROUP_SIZE; i++)
      {
        /* We are about to decode a prefix code.  We need lookahead of
           20 bits at the beginning (the greatest possible code length)
           to reduce number of bitwise operations to absolute minimum. */
        if (unlikely(w < MAX_CODE_LENGTH)) { REFILL(S_PREFIX); }

        /* Use a table lookup to determine minimal code length quickly.
           For lengths <= SW, this table always gives precise reults.
           For lengths > SW, some additional iterations must be performed
           to determine the exact code length. */
        x = T->start[v >> (64 - SW)];
        k = x & 0x1F;

        /* Distinguish between complete and incomplete lookup table entries.
           If an entry is complete, we have the symbol immediately -- it's
           stored in higher bits of the entry.  Otherwise we need to use
           so called "cannonical decoding" algorithm to decode the symbol. */
        if (likely(k <= SW))
          s = x >> 5;
        else
        {
          while (v >= T->base[k+1])
            k++;  /* iterate to determine the exact code length */
          /* cannonical decode */
          s = T->perm[T->count[k] + ((v - T->base[k]) >> (64-k))];
        }

        /* At this point we know the prefix code is exactly k-bit long,
           so we can consume (ie. shift out from lookahead buffer)
           bits occupied by the code. */
        v <<= k;
        w -= k;

        if (unlikely(s == 0))
        {
          assert(j < 900050);

          if (j == 0)
          {
            Trace(("empty block\n"));
            return YB_ERR_EMPTY;
          }

          dec->num_mtfv = j;
          has_block = 1;
          ibs->dec = 0;
          goto block_header;
        }

        dec->tt16[j++] = s;
      }
    }

    Trace(("no EOB in the last group\n"));
    return YB_ERR_UNTERM;
  }

#define SAVE(var) ibs->save_##var = var
save_and_ret:
  SAVE(v);
  SAVE(w);
  SAVE(big);
  SAVE(small);
  SAVE(i);
  SAVE(t);
  SAVE(s);
  SAVE(r);
  SAVE(j);
  SAVE(T);
  SAVE(x);
  SAVE(k);
  SAVE(g);
  SAVE(togo);
  SAVE(magic1);
  SAVE(magic2);
  SAVE(has_block);
#undef SAVE

  *buf_sz = in_avail;
  return YB_UNDERFLOW;
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

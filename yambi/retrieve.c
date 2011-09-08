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

#include <stdlib.h>  /* abort() */

#include "decode.h"


#if 0
# include <stdio.h>
# define Trace(x) fprintf x
#else
# define Trace(x)
#endif


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
make_tree(YBdec_t *dec,   /* where to store created tables */
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

  C = dec->tree[t].count;
  B = dec->tree[t].base;
  P = dec->tree[t].perm;
  S = dec->tree[t].start;

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



#define GET(vv,nn)                              \
  do                                            \
  {                                             \
    assert(w >= (nn));                          \
    (vv) = v >> (64-(nn));                      \
    w -= (nn);                                  \
    v <<= (nn);                                 \
  }                                             \
  while (0)

#define W_RANGE(from,to) assert(w+1 >= (from)+1 && w <= (to))

#define S_INIT         1
#define S_BWT_IDX      2
#define S_BITMAP_BIG   3
#define S_BITMAP_SMALL 4
#define S_SELECTOR_MTF 5
#define S_DELTA_TAG    6
#define S_PREFIX       7
#define S_TRAILER      8



YBdec_t *
YBdec_init(void)
{
  YBdec_t *dec;

  dec = xalloc(sizeof(YBdec_t));
  dec->rle_state = 0;
  dec->rle_crc = 0xffffffff;
  dec->state = S_INIT;

  return dec;
}


void
YBdec_destroy(YBdec_t *dec)
{
  assert(dec != 0);
  xfree(dec);
}


/* TODO: add optimization similar to inflate_fast from gzip. */
/* TODO: add prefetchnta to avoid lookup tables pollution. */
int
YBdec_retrieve(YBdec_t *dec, const void *buf, size_t *ipos_p, size_t ipos_lim,
    unsigned *bit_buf, unsigned *bits_left)
{
  const Int *in;  /* pointer to the next input byte */
  Int in_avail;    /* number of input bytes available */

  Long v;  /* next 0-63 bits of input stream, left-aligned, zero-padded */
  Int w;   /* available bits in v */
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


  assert(dec != 0);
  assert(buf != 0);

  Trace((stderr, "retrieve called with ipos=%u, w=%u, v=0x%X\n",
         (unsigned)*ipos_p, *bits_left, *bit_buf));

  in = (const Int *)buf + *ipos_p;
  in_avail = ipos_lim - *ipos_p;
  assert(in_avail >= 1);

  w = *bits_left;
  if (w > 0)
    v = (Long)*bit_buf << (64-w);
  else
    v = 0;

  switch (dec->state)
  {
  case S_INIT:
    W_RANGE(0,31);
    v |= (Long)peekl(in) << (32-w);
    w += 32;
    in++;
    in_avail--;
    Trace((stderr, "retrieve init: w=%u, v=0x%lX\n", w, v));

    W_RANGE(32,63);
    GET(dec->expect_crc, 32);
    Trace((stderr, "retrieve init: CRC is 0x%X\n", dec->expect_crc));

    W_RANGE(0,31);
    if (unlikely(in_avail == 0)) {
      dec->state = S_BWT_IDX;
      *ipos_p = in - (const Int *)buf;
      *bits_left = w;
      *bit_buf = v >> (64-w);
      Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
      return YB_UNDERFLOW;
    }
  case S_BWT_IDX:
    Trace((stderr, "v=0x%lX, w=%u\n", v, w));
    v |= (Long)peekl(in) << (32-w);
    w += 32;
    Trace((stderr, "v=0x%lX, w=%u\n", v, w));
    in++;
    in_avail--;

    W_RANGE(32,63);
    GET(dec->rand, 1);
    Trace((stderr, "rand_bit=%d\n", dec->rand));

    W_RANGE(31,62);
    GET(dec->bwt_idx, 24);
    Trace((stderr, "bwt_idx=%u\n", dec->bwt_idx));
    W_RANGE(7,38);


    /*=== RETRIEVE BITMAP ===*/

    if (w < 32) {
      if (unlikely(in_avail == 0)) {
        dec->state = S_BITMAP_BIG;
        *ipos_p = in - (const Int *)buf;
        *bits_left = w;
        *bit_buf = v >> (64-w);
        Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
        return YB_UNDERFLOW;
      }
    case S_BITMAP_BIG:
      v |= (Long)peekl(in) << (32-w);
      w += 32;
      in++;
      in_avail--;
    }
    W_RANGE(32,63);
    GET(big, 16); W_RANGE(16,47);
    small = 0;
    k = 0;
    j = 0;
    do {
      if (big & 0x8000) {
        GET(small, 16); W_RANGE(0,47);
        if (w < 32) {
          if (unlikely(in_avail == 0)) {
            dec->state = S_BITMAP_SMALL;
            dec->save_1 = j;
            dec->save_2 = k;
            dec->save_3 = big;
            dec->save_4 = small;
            *ipos_p = in - (const Int *)buf;
            *bits_left = w;
            *bit_buf = v >> (64-w);
            Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
            return YB_UNDERFLOW;
          case S_BITMAP_SMALL:
            j = dec->save_1;
            k = dec->save_2;
            big = dec->save_3;
            small = dec->save_4;
          }
          v |= (Long)peekl(in) << (32-w);
          w += 32;
          in++;
          in_avail--;
        }
        W_RANGE(32,63);
      }
      do {
        dec->imtf_slide[IMTF_SLIDE_LENGTH - 256 + k] = j++;
        k += small >> 15;
        small <<= 1;
      } while (j & 0xF);
      big <<= 1;
    } while (j < 256);

    if (k == 0)
    {
      Trace((stderr, "empty alphabet\n"));
      return YB_ERR_BITMAP;
    }

    dec->alpha_size = k+2;


    W_RANGE(32,63);
    GET(dec->num_trees, 3); W_RANGE(29,60);
    if (dec->num_trees < MIN_TREES || dec->num_trees > MAX_TREES)
    {
      Trace((stderr, "bad number of trees (%d, should be from 2 to 6)\n",
             dec->num_trees));
      return YB_ERR_TREES;
    }

    GET(dec->num_selectors, 15); W_RANGE(14,45);
    if (dec->num_selectors == 0)
    {
      Trace((stderr, "no selectors\n"));
      return YB_ERR_GROUPS;
    }


    /*=== RETRIEVE SELECTOR MTF VALUES ===*/

    for (i = 0; i < dec->num_selectors; i++)
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

      assert(w >= 6);
      k = table[v >> (64-6)];

      if (unlikely(k > dec->num_trees))
      {
        Trace((stderr, "bad selector mtfv\n"));
        return YB_ERR_SELECTOR;
      }

      v <<= k;
      w -= k;

      dec->selector[i] = k-1;

      if (w < 11) {
        if (unlikely(in_avail == 0)) {
          dec->state = S_SELECTOR_MTF;
          dec->save_1 = i;
          *ipos_p = in - (const Int *)buf;
          *bits_left = w;
          *bit_buf = v >> (64-w);
          Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
          return YB_UNDERFLOW;
        case S_SELECTOR_MTF:
          i = dec->save_1;
        }
        v |= (Long)peekl(in) << (32-w);
        w += 32;
        in++;
        in_avail--;
      }
    }
    W_RANGE(11,44);

    /*=== RETRIEVE DECODING TABLES ===*/

    for (t = 0; t < dec->num_trees; t++)
    {
      W_RANGE(11,44);
      GET(x, 5);
      W_RANGE(6,39);

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
          assert(w >= 6);
          k = v >> (64-6);
          x += R[k];
          if (unlikely(x < 3+MIN_CODE_LENGTH || x > 3+MAX_CODE_LENGTH))
          {
            Trace((stderr, "bad code length\n"));
            return YB_ERR_DELTA;
          }
          x -= 3;
          k = L[k];
          w -= k;
          v <<= k;
          W_RANGE(0,41);

          if (unlikely(w < 11)) {
            if (unlikely(in_avail == 0)) {
              dec->state = S_DELTA_TAG;
              dec->save_1 = x;
              dec->save_2 = k;
              dec->save_3 = s;
              dec->save_4 = t;
              *ipos_p = in - (const Int *)buf;
              *bits_left = w;
              *bit_buf = v >> (64-w);
              Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
              return YB_UNDERFLOW;
            case S_DELTA_TAG:
              x = dec->save_1;
              k = dec->save_2;
              s = dec->save_3;
              t = dec->save_4;
            }
            v |= (Long)peekl(in) << (32-w);
            w += 32;
            in++;
            in_avail--;
          }
          W_RANGE(11,42);
        }
        while (unlikely(k == 6));

        ((Byte *)dec->tree[t].start)[s] = x;
      }
      r = make_tree(dec, t, (Byte *)dec->tree[t].start,
                    dec->alpha_size);
      if (!r)
        r = t;
      dec->mtf[t] = r;  /* Initialise MTF state. */
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
    if (dec->num_selectors > 18001)
      dec->num_selectors = 18001;

    for (g = 0; g < dec->num_selectors; g++)
    {
      /* We started a new group, time to (possibly) switch to a new tree.
         We check the selector table to determine which tree to use.
         If the value we looked up is 6 or 7, it means decode error
         (values of 6 and 7 are special cases, they denote invalid trees). */
      i = dec->selector[g];

      t = dec->mtf[i];
      if (unlikely(t >= 6))
      {
        if (t == 6) {
          Trace((stderr, "oversubscribed prefix code\n"));
          return YB_ERR_PREFIX;
        }
        else {
          Trace((stderr, "incomplete prefix code\n"));
          return YB_ERR_INCOMPLT;
        }
      }

      while (i > 0)
      {
        dec->mtf[i] = dec->mtf[i-1];
        i--;
      }
      dec->mtf[0] = t;

      T = &dec->tree[t];

      /* There are up to GROUP_SIZE codes in any group. */
      assert(i == 0);
      for (/*i = 0*/; i < GROUP_SIZE; i++)
      {
        /* We are about to decode a prefix code.  We need lookahead of
           20 bits at the beginning (the greatest possible code length)
           to reduce number of bitwise operations to absolute minimum. */
        W_RANGE(0,50);
        if (unlikely(w < MAX_CODE_LENGTH)) {
          if (unlikely(in_avail == 0)) {
            dec->state = S_PREFIX;
            dec->save_1 = g;
            dec->save_2 = i;
            dec->save_3 = j;
            *ipos_p = in - (const Int *)buf;
            *bits_left = w;
            *bit_buf = v >> (64-w);
            Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
            return YB_UNDERFLOW;
          case S_PREFIX:
            g = dec->save_1;
            i = dec->save_2;
            j = dec->save_3;
            T = &dec->tree[dec->mtf[0]];
          }
          v |= (Long)peekl(in) << (32-w);
          w += 32;
          in++;
          in_avail--;
        }
        W_RANGE(20,51);

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
            Trace((stderr, "empty block\n"));
            return YB_ERR_EMPTY;
          }

          W_RANGE(0,50);
          dec->num_mtfv = j;

          j = 2;
          k = 0;
          while (j > 0) {
            if (w < 24) {
              if (unlikely(in_avail == 0)) {
                dec->state = S_TRAILER;
                dec->save_1 = j;
                dec->save_2 = k;
                *ipos_p = in - (const Int *)buf;
                *bits_left = w;
                *bit_buf = v >> (64-w);
                Trace((stderr, "retrieve: return YB_UNDERFLOW\n"));
                return YB_UNDERFLOW;
              case S_TRAILER:
                j = dec->save_1;
                k = dec->save_2;
              }
              v |= (Long)peekl(in) << (32-w);
              w += 32;
              in++;
              in_avail--;
            }
            j--;
            GET(i, 24);
            if (j == 1)
              k = i;
          }

          if ((k != 0x314159 || i != 0x265359) &&
              (k != 0x177245 || i != 0x385090))
          {
            Trace((stderr, "retrieve: return YB_ERR_HEADER\n"));
            return YB_ERR_HEADER;
          }

          assert(w < 32);
          *ipos_p = in - (const Int *)buf;
          *bits_left = w;
          *bit_buf = v >> (64-w);
          dec->state = 0;
          Trace((stderr, "@return: bwt_idx=%d\n", dec->bwt_idx));
          return (k == 0x314159) ? YB_OK : YB_DONE;
        }

        dec->tt16[j++] = s;
      }
    }

    Trace((stderr, "no EOB in the last group\n"));
    return YB_ERR_UNTERM;

  default:
    abort();
  }
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

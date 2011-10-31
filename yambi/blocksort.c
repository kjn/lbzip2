/*-
  blocksort.c -- BWT encoder

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

/*-
  This program, "bzip2", the associated library "libbzip2", and all
  documentation, are copyright (C) 1996-2005 Julian R Seward.  All
  rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. The origin of this software must not be misrepresented; you must
      not claim that you wrote the original software.  If you use this
      software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.
   3. Altered source versions must be plainly marked as such, and must
      not be misrepresented as being the original software.
   4. The name of the author may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <setjmp.h>     /* setjmp() */
#include <stdlib.h>     /* free() */
#include <string.h>     /* memcmp() */
#include <strings.h>    /* bzero() */

#include "xalloc.h"     /* xmalloc() */

#include "encode.h"


struct Work {
  SLong budget;
  jmp_buf jmp_buffer;
};


/*=========================================================================
  (I) THE LSD RADIX SORT ALGORITHM
  ========================================================================= */

/* If a block size is less or equal to this value, the LSD radix sort
   will be used to compute BWT for that particular block. */
#define RS_MBS 512

/* ============================================================================
   Compute BWT using the naive, O(n^2) algorithm, which uses counting sort.
   This function is used for small blocks only because it is slow in practice.

   The algorithm consists of `n' passes of counting sort, each pass is O(n).
   First all rotations are sorted according to their n-th character, then
   (n-1)-th character and so on, down to the 1st character.
*/
static void
radix_sort_bwt(
  Byte *D,
  Int n,
  Int *bwt_idx)
{
  Int i;  /* index */
  Int j;
  Int d;  /* distance */
  Int t;  /* temp */
  Int C[256];
  Int P[RS_MBS];
  Int U[RS_MBS];
  Byte B[RS_MBS];

  assert(n <= RS_MBS);

  /* Zero counts. */
  d = 0;
  bzero(C, 256 * sizeof(Int));

  /* Counting sort doesn't sort in place.  Instead sorting to a temporary
     location and copying back in each step, we sort from P to U and then
     back from U to P.  We need to distinguish between odd and even block
     sizes because after final step we need to have indices placed in P.
     For even n, start in P, for odd n start in U. */
  if (n & 1) {
    for (i = 0; i < n; i++) {
      U[i] = i;
      C[B[i] = D[i]]++;
    }

    t = 0;
    for (i = 0; i < 256; i++)
      C[i] = (t += C[i]) - C[i];
    assert(t == n);

    goto inner;
  }

  for (i = 0; i < n; i++) {
    P[i] = i;
    C[B[i] = D[i]]++;
  }

  for (i = 1; i < 256; i++)
    C[i] += C[i-1];
  assert(C[255] == n);

  while (d < n) {
    /* Sort from P to U, indices descending. */
    d++;
    for (i = n; i > 0; i--) {
      j = P[i-1];
      t = j + n - d;
      if (t >= n) t -= n;
      U[--C[B[t]]] = j;
    }

  inner:
    /* Sort from U to P, indices ascending. */
    d++;
    for (i = 0; i < n; i++) {
      j = U[i];
      t = j + n - d;
      if (t >= n) t -= n;
      P[C[B[t]]++] = j;
    }

    assert(C[255] == n);
  }

  /* Compute BWT transform from sorted order. */
  *bwt_idx = n;
  for (i = 0; i < n; i++) {
    j = P[i];
    if (j == 0) {
      assert(*bwt_idx == n);
      *bwt_idx = i;
      j = n;
    }

    D[i] = B[j-1];
  }

  assert(*bwt_idx < n);
}


/*=========================================================================
  (II) TWO-BYTE BUCKET SORT
  ========================================================================= */

/* A single pass of bucket sort. */
#define SINGLE_BUCKET_SORT_PASS(op) do          \
  {                                             \
    i = 0;                                      \
    u = block[nblock - 1];                      \
                                                \
    while (i+4 <= i_lim) {                      \
      v = peekl(block + i);                     \
      u = (u << 8) | (v >> 24); op; i++;        \
      u = v >> 16; op; i++;                     \
      u = v >> 8; op; i++;                      \
      u = v; op; i++;                           \
    }                                           \
                                                \
    while (i < nblock) {                        \
      u = (u << 8) | block[i]; op; i++;         \
    }                                           \
                                                \
    assert((Byte)u == block[nblock - 1]);       \
  }                                             \
  while (0)

/* Steps Q3 and Q4 combined. */
static void
bucket_sort(
  Int *ptr,
  const Byte *block,
  Int *ftab,
  Int nblock)
{
  Int f;
  Int i;
  Short u;
  Int v;
  const Int i_lim = nblock & 0xFFFFC;

  /* Determine the size of each bucket. */
  bzero(ftab, 65537 * sizeof(Int));
  SINGLE_BUCKET_SORT_PASS(ftab[u]++);

  /* Transform counts into indices. */
  for (i = 0; i < 65536; i++)
    ftab[i+1] += ftab[i];
  assert(ftab[65536] == nblock);

  /* Sort indices. */
  f = ftab[(block[nblock-1] << 8) | block[0]];
  SINGLE_BUCKET_SORT_PASS(ptr[--ftab[u]] = i-1);
  ptr[f-1] = nblock-1;
}

#undef SINGLE_BUCKET_SORT_PASS


/*=========================================================================
  (III) THE MAIN SORTING ALGORITHM
  ========================================================================= */

/* How much bytes rot_cmp() comares in one step. */
#define FULLGT_GRANULARITY 256

/* ============================================================================
   Compare lexicographically two rotations, R_i and R_j. Return a positive
   integer if R_i > R_j, a negative integer if R_i < R_j. If both rotations are
   equal, this function does not return; longjmp() is called instead.
*/
static int
rot_cmp(
  Int i,             /* Index of the first rotation to compare. */
  Int j,             /* Index of the second rotation to compare. */
  const Short *qptr, /* Pointer to quadrant table, aligned by current depth. */
  Int n,             /* Block size. */
  struct Work *work) /*  */
{
  /* Calculate number of steps. Rounding down is correct because the actual
     number of steps is one greater. */
  Int k = n / FULLGT_GRANULARITY;

  do {
    int rv;
    if ((rv = memcmp(qptr+i, qptr+j, 2 * FULLGT_GRANULARITY)) != 0)
      return rv > 0;

    work->budget--;
    i += FULLGT_GRANULARITY; if (i >= n) i -= n;
    j += FULLGT_GRANULARITY; if (j >= n) j -= n;
  }
  while (k-- > 0);

  /* If we got to this point it means that the block consists of a string
     repeated number of times.  In this case we simply abandon quicksorting
     and switch to BPR.
  */
  longjmp(work->jmp_buffer, 1);
}


/*---------------------------------------------*/

static void
shell_sort(
  Int *ptr,
  const Short *qptr,
  Int nblock,
  Int lo,
  Int hi,
  struct Work *work)
{
  /*--
    Knuth's increments seem to work better
    than Incerpi-Sedgewick here.  Possibly
    because the number of elems to sort is
    usually small, typically <= 20.
    --*/
  static const Int incs[] = { 1, 4, 13, 40, 121, 364, 1093, 3280,
                              9841, 29524, 88573, 265720,
                              797161, 900000 };  /* 900000 is a sentinel */

  Int i, j, h, hp;

  /* Less than 2 elements -- the range is already sorted. */
  if (hi - lo < 2)
    return;

  hp = 0;
  while (incs[hp] < hi - lo) hp++;

  while (hp > 0) {
    h = incs[--hp];

    i = lo + h;
    for (;;) {

#define ITER                                                    \
      {                                                         \
        Int t,u,v;                                              \
        if (i >= hi) break;                                     \
        v = ptr[i];                                             \
        j = i;                                                  \
        u = j-h;                                                \
        while (rot_cmp((t = ptr[u]), v, qptr, nblock, work) > 0)     \
        {                                                       \
          ptr[j] = t;                                           \
          j = u;                                                \
          if (u < lo+h) break;                                  \
          u -= h;                                               \
        }                                                       \
        ptr[j] = v;                                             \
        i++;                                                    \
      }

      ITER; ITER; ITER;

      if (unlikely(work->budget <= 0))
        longjmp(work->jmp_buffer, 1);
    }
  }
}

/*---------------------------------------------*/

#define swap(zzt, zz1, zz2)                    \
  zzt = zz1; zz1 = zz2; zz2 = zzt;

#define vswap(zzp1, zzp2, zzn)                  \
  {                                             \
    Int yyp1 = (zzp1);                          \
    Int yyp2 = (zzp2);                          \
    Int yyn  = (zzn);                           \
    Int yyt;                                    \
    while (yyn > 0) {                           \
      swap(yyt, ptr[yyp1], ptr[yyp2]);          \
      yyp1++; yyp2++; yyn--;                    \
    }                                           \
  }

/* Compare and swap */
#define CAS(c,d,a,b)                                            \
  const Int r##d##a = r##c##a > r##c##b ? r##c##a : r##c##b;    \
  const Int r##d##b = r##c##a > r##c##b ? r##c##b : r##c##a     \

/* Direct copy */
#define PHI(c,d,a) const Int r##d##a = r##c##a

/* Select the median of 5 integers without branching. */
static Int
med5(
  const Int r11,
  const Int r12,
  const Int r13,
  const Int r14,
  const Int r15)
{
  /* The following code corresponds            1 ---*---*----*-----------
     to this sorting network.                       |   |    |
     The network isn't complete, but           2 ---*---+*---*---*-------
     always gives correct median.                       ||       |
                                               3 ---*---*+---*---*---*---  */
  CAS(1,2,1,2); CAS(1,2,3,4); PHI(1,2,5);  /*       |    |   |       |     */
  CAS(2,3,1,3); CAS(2,3,2,5); PHI(2,3,4);  /*  4 ---*----+---*---*---*---  */
  CAS(3,4,1,2); CAS(3,4,3,4); PHI(3,4,5);  /*            |       |         */
  CAS(4,5,2,3); CAS(4,5,4,5); PHI(4,5,1);  /*  5 --------*-------*-------  */
  CAS(5,6,3,4);

  (void)(r51 + r52 + r55 + r64);  /* Avoid warnings about unused variables. */
  return r63;
}

#undef CAS
#undef PHI


#define BZ_N_RADIX 2
#define BZ_N_QSORT 16
#define BZ_N_OVERSHOOT 800

#define QSORT_SMALL_THRESH 10
#define QSORT_DEPTH_THRESH (BZ_N_RADIX + BZ_N_QSORT)
#define QSORT_STACK_SIZE 100

/* Step Q6a. */
static void
quick_sort(
  Int *ptr,
  const Short *quadrant,
  Int nblock,
  Int lo,
  Int hi,
  struct Work *work)
{
  /* All pointers are assumed to point between elements,
     i.e. *Lo pointers are inclusive, *Hi are exclusive.
  */
  Int unLo;  /* unprocessed low */
  Int unHi;  /* unprocessed high */
  Int ltLo;  /* less-than high */
  Int gtHi;  /* greater-than high */
  Int n, m;
  Int pivot;
  Int sp;    /* stack pointer */
  Int t;

  const Short *qptr = quadrant + BZ_N_RADIX;
  const Short *qptr_max = quadrant + QSORT_DEPTH_THRESH;

  Long vec;
  Long stack[QSORT_STACK_SIZE];

  sp = 0;

  for (;;) {
    while (hi-lo <= QSORT_SMALL_THRESH || qptr > qptr_max) {
      shell_sort ( ptr, qptr, nblock, lo, hi, work );
      if (sp == 0)
        return;
      vec = stack[--sp];
      qptr = quadrant + (Byte)vec;
      vec >>= 8;
      lo = vec;
      hi = vec + (vec >> 32);
    }

    pivot = med5(peeks(qptr + ptr[ lo             ]),
                 peeks(qptr + ptr[ lo+(hi-lo)/4   ]),
                 peeks(qptr + ptr[ (lo+hi)>>1     ]),
                 peeks(qptr + ptr[ lo+3*(hi-lo)/4 ]),
                 peeks(qptr + ptr[ hi-1           ]));

    unLo = ltLo = lo;
    unHi = gtHi = hi;

    /* Perform a fast, three-way partitioning.

       Pass 1: partition
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       | elements  | elements  | elements  | elements      | elements  |
       | equal to  | less than |  not yet  | greater than  | equal to  |
       | the pivot | the pivot | processed | the pivot     | the pivot |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       ^           ^           ^           ^               ^           ^
       lo          ltLo        unLo       unHi            gtHi        hi

       Always uses N-1 comparisions.
       One additional exchange per equal key.
       Invariant: lo <= ltLo <= unLo <= unHi <= gtHi <= hi
    */
    for (;;) {
      assert(unLo < unHi);
      /* Move from left to find an element that is not less. */
      while ((n = peeks(qptr + ptr[unLo])) <= pivot) {
        /* If the element is equal, move it to the left. */
        if (n == pivot) {
          swap(t, ptr[unLo], ptr[ltLo]);
          ltLo++;
        }

        unLo++;
        /* Stop if pointers have crossed */
        if (unLo >= unHi) goto qs_done;
      }
      assert(unLo < unHi);
      /* Move from left to find an element that is not greater. */
      while ((n = peeks(qptr + ptr[unHi-1])) >= pivot) {
        /* If the element is equal, move it to the right. */
        if (n == pivot) {
          swap(t, ptr[unHi-1], ptr[gtHi-1]);
          gtHi--;
        }
        unHi--;
        /* Stop if pointers have crossed */
        if (unLo >= unHi) goto qs_done;
      }
      /* Exchange. */
      swap(t, ptr[unLo], ptr[unHi-1]); unLo++; unHi--;
      if (unLo >= unHi) goto qs_done;
    }

  qs_done:
    assert ( unHi == unLo);

    n = min(ltLo-lo, unLo-ltLo); vswap(lo, unLo-n, n);
    m = min(hi-gtHi, gtHi-unHi); vswap(unLo, hi-m, m);

    n = lo + unLo - ltLo;
    m = hi - gtHi + unHi;

    {
      /* Pack quicksort parameters into single words so they can be sorted
         easier. */
      Byte d = qptr - quadrant;
      Long v1 = ((Long)(n - lo) << 40) | (lo << 8) | d;
      Long v2 = ((Long)(m -  n) << 40) | (n  << 8) | (d + 1);
      Long v3 = ((Long)(hi - m) << 40) | (m  << 8) | d;

      /* Sort 3 integers, without branching. */
      Long min123 = min(min(v1, v2), v3);
      Long max123 = max(max(v1, v2), v3);
      Long median = v1 ^ v2 ^ v3 ^ max123 ^ min123;

      assert(sp <= QSORT_STACK_SIZE-2);
      stack[sp++] = max123;
      stack[sp++] = median;

      qptr = quadrant + (Byte)min123;
      min123 >>= 8;
      lo = min123;
      hi = min123 + (min123 >> 32);
    }
  }
}


#define BIG_START(bb) ftab[(bb) << 8]
#define BIG_END(bb)   ftab[((bb) + 1) << 8]
#define BIG_SIZE(bb)  (BIG_END(bb) - BIG_START(bb))

#define SMALL_START(bb,sb) ftab[((bb) << 8) + (sb)]
#define SMALL_END(bb,sb)   ftab[((bb) << 8) + (sb) + 1]
#define SMALL_SIZE(bb,sb)  (SMALL_END(bb,sb) - SMALL_START(bb,sb))


static void
induce_orderings(
  Int *ptr,
  const Int *ftab,
  const Short *quadrant,
  const Byte *bigDone,
  Int ss,
  Int nblock)
{
  Byte c1;
  Int copy[256];
  Int i, j, k;

  /* Step Q6b. */
  for (i = 0; i < 256; i++)
    copy[i] = SMALL_START(i,ss);
  for (i = BIG_START(ss); i < copy[ss]; i++) {
    k = ptr[i];
    if (k == 0) k = nblock;
    k--;
    c1 = *(const Byte *)&quadrant[k];
    if (!bigDone[c1])
      ptr[ copy[c1]++ ] = k;
#ifdef DEBUG
    else
      assert( ptr[ copy[c1]++ ] == k );
#endif
  }
  assert(i == copy[ss]);
  j = i;

  /* Step Q6c. */
  for (i = 0; i < 256; i++)
    copy[i] = SMALL_END(i,ss);
  for (i = BIG_END(ss); i > copy[ss]; i--) {
    k = ptr[i-1];
    if (k == 0) k = nblock;
    k--;
    c1 = *(const Byte *)&quadrant[k];
    if (!bigDone[c1])
      ptr[ --copy[c1] ] = k;
#ifdef DEBUG
    else
      assert( ptr[ --copy[c1] ] == k );
#endif
  }
  assert(i == copy[ss]);

  assert(i == j);
}


/* Step Q7. */
static void
update_quadrants(
  Short *quadrant,
  const Int *ptr,
  const Int *ftab,
  Int bb,  /* big bucket no. */
  Int sb,  /* small bucket no. */
  Int nblock,
  Byte *bwt,
  Int *prim_idx)
{
  Int start = SMALL_START(bb,sb);
  Int end   = SMALL_END  (bb,sb);
  Int shift = 0;
  Int j;

  while (((end-start-1) >> shift) >= 256)
    shift++;

  for (j = start; j < end; j++)
  {
    Byte qVal;

    Int k = ptr[j];
    k++;
    if (k == nblock) k = 0;

    qVal = (j - start) >> shift;
    *((Byte *)&quadrant[k] + 1) = qVal;
    if (unlikely(k < BZ_N_OVERSHOOT))
      *((Byte *)&quadrant[k + nblock] + 1) = qVal;

    k = ptr[j];
    if (unlikely(k == 0)) { *prim_idx = j; k = nblock; }
    k--;
    bwt[j] = *(Byte *)&quadrant[k];
  }
}


/* Algorithm Q. */
static Int
copy_cache(
  Int *ptr,
  Int *ftab,
  Short *quadrant,
  Byte *block,
  Byte *bigDone,
  Int  nblock,
  struct Work *work)
{
  Int i, j, k, ss, sb;
  Byte bigOrder[256];
  Byte smallOrder[255];
  Int idx = 900000;
  Int bigSize[256];
  Int num_big = 0;

  /* Calculate the running order, from smallest to largest big bucket.
     We want to sort smaller buckets first because sorting them will
     populate some of quadrant descriptors, which can help a lot sorting
     bigger buckets. */
  for (i = 0; i < 256; i++) {
    bigSize[i] = BIG_SIZE(i);
    if (bigSize[i] > 0)
      bigOrder[num_big++] = i;
    else
      bigDone[i] = 1;
  }
  assert(num_big > 0);

  if (unlikely(num_big == 1)) {
    idx = 0;
    return 0;
  }


  /*--
    The main sorting loop.
    --*/

  for (i = 0; i < num_big; i++)
  {
    Int num_small;

    /* Select the smallest, not yet sorted big bucket. */
    {
      Int best_size, best_bucket, best_idx;

      best_idx = i;
      best_bucket = bigOrder[i];
      best_size = bigSize[bigOrder[i]];

      for (j = i+1; j < num_big; j++)
      {
        ss = bigOrder[j];
        if (bigSize[ss] < best_size)
        {
          best_idx = j;
          best_bucket = ss;
          best_size = bigSize[ss];
        }
      }
      ss = best_bucket;
      bigOrder[best_idx] = bigOrder[i];
      bigOrder[i] = ss;
    }

    /* Select non-empty small buckets. */
    num_small = 0;
    for (j = i+1; j < num_big; j++)
    {
      sb = bigOrder[j];
      if (SMALL_SIZE(ss, sb) > 0)
        smallOrder[num_small++] = sb;
    }
    assert(num_small <= num_big-(i+1));

    /* Define small bucket order within the big bucket. */
    {
      Byte vv;
      Int h = 121;
      do {
        for (j = h; j < num_small; j++) {
          vv = smallOrder[j];
          k = j;
          while ( SMALL_SIZE(ss,smallOrder[k-h]) > SMALL_SIZE(ss,vv) ) {
            smallOrder[k] = smallOrder[k-h];
            k = k - h;
            if (k < h) break;
          }
          smallOrder[k] = vv;
        }
      } while ((h /= 3) > 0);
    }

    /* Step Q6a. Quicksort all small buckets within the current big bucket. */
    assert ( !bigDone[ss]);
    for (j = 0; j < num_small; j++)
    {
      sb = smallOrder[j];
      quick_sort(ptr, quadrant, nblock, SMALL_START(ss,sb),
                 SMALL_END(ss,sb), work);
      bigSize[ss] -= SMALL_SIZE(ss,sb);
      update_quadrants(quadrant, ptr, ftab, ss, sb, nblock, block, &idx);
    }

    /* Steps Q6b, Q6c. */
    induce_orderings(ptr, ftab, quadrant, bigDone, ss, nblock);

    for (j = 0; j < 256; j++)
    {
      if (!bigDone[j]) {
        bigSize[j] -= SMALL_SIZE(j,ss);
        update_quadrants(quadrant, ptr, ftab, j, ss, nblock, block, &idx);
      }
    }

    assert(bigSize[ss] == 0);
    bigDone[ss] = 1;
  }

  for (j = 0; j < 256; j++) {
    assert(bigDone[j]);
    assert(bigSize[j] == 0);
  }

  assert(idx < nblock);
  return idx;
}

/* This wrapper function is here only to make sure that bevavior of setjmp()
   is well-defined. */
static int
copy_cache_wrap(
  Int *ptr,
  Int *ftab,
  Short *quadrant,
  Byte *block,
  Byte *bigDone,
  Int nblock,
  Int shallow_factor,
  Int *bwt_idx)
{
  struct Work work;
  work.budget = (SLong)nblock * shallow_factor / FULLGT_GRANULARITY;

  if (setjmp(work.jmp_buffer))
    return 0;

  *bwt_idx = copy_cache(ptr, ftab, quadrant, block, bigDone, nblock, &work);
  return 1;
}


/*=========================================================================
  (IV) BUCKET POINTER REFINEMENT ALGORITHM
  ========================================================================= */


/* Depth factor of BPR. Must be >= 2. */
#define BPR_K 2

/* BH stands for `bucket header'.  BH is a bit indicating whether a particular
   rotation starts a new bucket (BH==1) or lies in the same bucket as preceding
   rotation (BH==0).
*/
#define       SET_BH(zz)  bhtab[(zz) >> 6] |= ((Long)1 << ((zz) & 63))
#define     ISSET_BH(zz)  (bhtab[(zz) >> 6] & ((Long)1 << ((zz) & 63)))
#define      WORD_BH(zz)  bhtab[(zz) >> 6]
#define UNALIGNED_BH(zz)  ((zz) & 63)


static int
bpr_cmp(
  const Int *E,  /* equivalence classes */
  Int i,         /* first rotation to compare */
  Int j,         /* second rotation to compare */
  Int h0,        /* order magnitude */
  Int d,         /* depth */
  Int n)         /* block size */
{
  /* Initially align the pointers so they reflect current depth. */
  i += d * h0;
  j += d * h0;

  /* Compare equivalence classes lexicographically. Substrings of width h0 are
     compared at time. */
  while (d < BPR_K) {
    /* Wrap pointers around if they cross block boundrary. */
    if (i >= n) i -= n;
    if (j >= n) j -= n;

    /* If substrings at depth d belong to different equivalence classes,
       return the order immediately. */
    if (E[i] > E[j]) return 1;
    if (E[i] < E[j]) return -1;

    /* So far everything is equal, go one level deeper.  Remember to update the
       pointers too.*/
    d++;
    i += h0;
    j += h0;
  }

  return 0;
}


/* ============================================================================
   Sort range of rotations using a simple comparison sort and then partition it
   into standalone buckets.  Used as a subroutine in bpr_quick_sort().
 */
static void
bpr_simple_sort(
  Int *ptr,          /* sorted order */
  const Int *eclass, /* equivalence clsses */
  Long *bhtab,       /* bit heaer table */
  Int lo,            /* low end of the range to sort (inclusive) */
  Int hi,            /* high end of the range to sort (inclusive) */
  Int h0,            /* order magnitude */
  Int d,             /* depth */
  Int nblock)        /* block size */
{
  Int i, j;
  Int t, u, v;

  /* Less than 2 elements -- the range is already sorted and there is nothing
     to partition. */
  if (hi - lo < 2)
    return;

  /* One iteration of Shell sort with increment equal to 4. */
  i = lo + 4;
  while (i < hi) {
    v = ptr[i];
    j = i;
    u = j - 4;
    while (bpr_cmp(eclass, (t = ptr[u]), v, h0, d, nblock) > 0) {
      ptr[j] = t;
      j = u;
      if (u < lo + 4) break;
      u -= 4;
    }
    ptr[j] = v;
    i++;
  }

  /* One iteration of Shell sort with increment equal to 1 (ie. plain
     insertion sort). */
  i = lo + 1;
  while (i < hi) {
    v = ptr[i];
    j = i;
    u = j - 1;
    while (bpr_cmp(eclass, (t = ptr[u]), v, h0, d, nblock) > 0) {
      ptr[j] = t;
      j = u;
      if (u < lo + 1) break;
      u -= 1;
    }
    ptr[j] = v;
    i++;
  }

  /* The sorted order is already established. Now update bucket headers to
     reflect the bucket partition. We don't need to set BH(lo), because it was
     already set in bpr_quick_sort(). */
  t = ptr[lo];
  i = lo + 1;
  while (i < hi) {
    if (bpr_cmp(eclass, ptr[i], t, h0, d, nblock) != 0) {
      SET_BH(i);
      t = ptr[i];
    }
    i++;
  }
}


/* ============================================================================
   Sort and partition a single bucket.
*/
static void
bpr_quick_sort(
  Int *ptr,
  const Int *eclass,
  Long *bhtab,
  Int lo,
  Int hi,
  Int h0,
  Int nblock)
{
  /* All pointers are assumed to point between elements,
     i.e. *Lo pointers are inclusive, *Hi are exclusive.
  */
  Int unLo;  /* unprocessed low */
  Int unHi;  /* unprocessed high */
  Int ltLo;  /* less-than high */
  Int gtHi;  /* greater-than high */
  Int n, m;
  Int pivot;
  Int sp;    /* stack pointer */
  Int t;
  Int d;

  Long vec;
  Long stack[QSORT_STACK_SIZE];

  d = 1;
  sp = 0;

  for (;;) {
    while (hi-lo <= 10 || d >= BPR_K) {
      SET_BH(lo);
      if (d < BPR_K)
        bpr_simple_sort(ptr, eclass, bhtab, lo, hi, h0, d, nblock);
      if (sp == 0)
        return;
      vec = stack[--sp];
      d = (Byte)vec;
      vec >>= 8;
      lo = vec;
      hi = vec + (vec >> 32);
    }

    pivot = med5(eclass[(ptr[ lo             ] + d*h0) % nblock],
                 eclass[(ptr[ lo+(hi-lo)/4   ] + d*h0) % nblock],
                 eclass[(ptr[ (lo+hi)>>1     ] + d*h0) % nblock],
                 eclass[(ptr[ lo+3*(hi-lo)/4 ] + d*h0) % nblock],
                 eclass[(ptr[ hi-1           ] + d*h0) % nblock]);

    unLo = ltLo = lo;
    unHi = gtHi = hi;

    /* Perform a fast, three-way partitioning.

       Pass 1: partition
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       | elements  | elements  | elements  | elements      | elements  |
       | equal to  | less than |  not yet  | greater than  | equal to  |
       | the pivot | the pivot | processed | the pivot     | the pivot |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       ^           ^           ^           ^               ^           ^
       lo          ltLo        unLo       unHi            gtHi        hi

       Always uses N-1 comparisions.
       One additional exchange per equal key.
       Invariant: lo <= ltLo <= unLo <= unHi <= gtHi <= hi
    */
    for (;;) {
      assert(unLo < unHi);
      /* Move from left to find an element that is not less. */
      while ((n = eclass[(ptr[unLo] + d*h0) % nblock]) <= pivot) {
        /* If the element is equal, move it to the left. */
        if (n == pivot) {
          swap(t, ptr[unLo], ptr[ltLo]);
          ltLo++;
        }

        unLo++;
        /* Stop if pointers have crossed */
        if (unLo >= unHi) goto qs_done;
      }
      assert(unLo < unHi);
      /* Move from left to find an element that is not greater. */
      while ((n = eclass[(ptr[unHi-1] + d*h0) % nblock]) >= pivot) {
        /* If the element is equal, move it to the right. */
        if (n == pivot) {
          swap(t, ptr[unHi-1], ptr[gtHi-1]);
          gtHi--;
        }
        unHi--;
        /* Stop if pointers have crossed */
        if (unLo >= unHi) goto qs_done;
      }
      /* Exchange. */
      swap(t, ptr[unLo], ptr[unHi-1]); unLo++; unHi--;
      if (unLo >= unHi) goto qs_done;
    }

  qs_done:
    assert(unHi == unLo);

    n = min(ltLo-lo, unLo-ltLo); vswap(lo, unLo-n, n);
    m = min(hi-gtHi, gtHi-unHi); vswap(unLo, hi-m, m);

    n = lo + unLo - ltLo;
    m = hi - gtHi + unHi;

    {
      /* Pack quicksort parameters into single words so they can be sorted
         easier. */
      Long v1 = ((Long)(n - lo) << 40) | (lo << 8) | d;
      Long v2 = ((Long)(m -  n) << 40) | (n  << 8) | (d + 1);
      Long v3 = ((Long)(hi - m) << 40) | (m  << 8) | d;

      /* Sort 3 integers without branching, doing only 3 comparisons (yes,
         three; min(v1,v2) and max(v1,v2) do *one* comparison together). */
      Long min123 = min(min(v1, v2), v3);
      Long max123 = max(max(v1, v2), v3);
      Long median = v1 ^ v2 ^ v3 ^ max123 ^ min123;

      assert(sp <= QSORT_STACK_SIZE-2);
      stack[sp++] = max123;
      stack[sp++] = median;

      d = (Byte)min123;
      min123 >>= 8;
      lo = min123;
      hi = min123 + (min123 >> 32);
    }
  }
}


static void
bpr_sort(
  Int *ptr,
  Int *ftab,
  Byte *bigDone,
  Int nblock)
{
  Int d, i, k, r, j;
  Int nBhtab;
  Long *bhtab;
  Int *eclass;

  /* Allocate memory. */
  nBhtab = (nblock + 2*64 + 63) / 64;
  bhtab = xmalloc(nBhtab * sizeof(Long));
  bzero(bhtab, nBhtab * sizeof(Long));
  eclass = xmalloc(nblock * sizeof(Int));

  /* Set sentinel bits for block-end detection. */
  for (i = 0; i < 2*64; i += 2)
    SET_BH(nblock+i);

  /* The initial depth is always 2 because the initial bucket sort considers
     only first 2 characters. */
  d = 2;

  /* Scan initial buckets; set bucket headers and create initial equivalence
     classes */
  for (i = 0; i < 65536; i++) {
    k = ftab[i];
    r = ftab[i+1];
    if (k == r)
      continue;
    SET_BH(k);
    eclass[ptr[k]] = k;

    /* Check whether the small bucket [i/256, i%256] is already ordered.
       If the big bucket [i/256] is done, then obviously so is [i/256, i%256].
       Otherwise if the big bucket [i%256] is done, then [i/256, i%256] must
       be done too because it was ordered with Seward's Copy algorithm.

       If the small bucket is done, we place all strings it contains in their
       own singleton buckets, and we assign them unique equivalence classes.
       Otherwise we create one common equivalence class. */
    if (bigDone[i >> 8] | bigDone[i & 0xff]) {
      while (++k < r) {
        SET_BH(k);
        eclass[ptr[k]] = k;
      }
    }
    else {
      j = k;
      while (++k < r)
        eclass[ptr[k]] = j;
    }
  }

  /* The log(n) loop. */
  for (;;) {
    /* Assume we're done unltil we find out we're not. */
    int done = 1;

    k = 1;
    for (;;) {
      /* Find the next non-singleton bucket. */
      while (ISSET_BH(k) && UNALIGNED_BH(k)) k++;
      if (ISSET_BH(k)) {
        while (WORD_BH(k) == ~(Long)0) k += 64;
#if GNUC_VERSION >= 30406
        /* We use ctzll() instead of ctzl() for compatibility with 32-bit
           systems -- "long long int" is guarranteed to be at least 64-bit
           wide.  We abuse the fact that GNU C has ctzll() even in C89 mode,
           when the "long long int" type is disabled.
        */
        k += __builtin_ctzll(~WORD_BH(k));
#else
        while (ISSET_BH(k)) k++;
#endif
      }
      if (unlikely(k > nblock)) break;
      r = k;
      k -= 1;
      while (!ISSET_BH(r) && UNALIGNED_BH(r)) r++;
      if (!ISSET_BH(r)) {
        while (WORD_BH(r) == 0) r += 64;
#if GNUC_VERSION >= 30406
        r += __builtin_ctzll(WORD_BH(r));
#else
        while (!ISSET_BH(r)) r++;
#endif
      }
      if (unlikely(r > nblock)) break;

      /* Sort the current bucket -- [k,r). */
      assert(k < r-1);
      bpr_quick_sort(ptr, eclass, bhtab, k, r, d, nblock);

      /* Bucket refinement caused current equivalence class to be divided into
         disjoint classes. Scan bucket headers and update classes. */
      j = k;
      while (++k < r) {
        if (ISSET_BH(k)) j = k;
        else done = 0;
        eclass[ptr[k]] = j;
      }
    }

    d *= BPR_K;
    if (done || d >= nblock) break;
  }

  free(eclass);
  free(bhtab);
}


/*=========================================================================
  (IV) MASTER ALGORITHM
  ========================================================================= */

/* ============================================================================
   Compute the BWT (Burrows-Wheeler Transform) of the input string.

   This method uses three different algorithms for BWT computation:
   The algorithms are:
     1) The Cache-Copy Algorithm (implementation adopted from bzip2-1.0.3)
     2) The Bucket-Pointer-Refinement (BPR) algorithm
     3) The LSD radix sort algotim

   I have also implemented Ukkonen and Manber-Myers algorithms, but they turned
   to be too slow in practice. Manber-Myers can be found  at the end of the
   file (it's not used and therefore #if 0'ed).

   For very short strings (<= 512 characters) the naive LSD radix sort is used.

   Longer strings are attempted to be sorted using a highly optimized variation
   of Bentley-McIlroy three-way quicksort (the Cache-Copy algorithm; Cache and
   Copy are one of the improvements to the basic quicksort for strings).

   If quicksort runs into difficulties (or if the user gives `--exponential' on
   the command line), then BPR is used.
*/
void
YBpriv_block_sort(YBenc_t *s)
{
  Int i;
  Int nblock = s->nblock;
  Byte *block = s->block;

  /* Quadrants are 16-bit unsigned integers stored in *network* (big-endian)
     order so they can be easily (and efficiently) compared with memcmp().

     Quadrants are in 1-to-1 relation with input string rotations. The higher
     byte of each quadrant is simply the first character of the corresponding
     rotation.  The lower byte of i-th quadrant is the position of (i-1)-th
     rotation within the small bucket it belongs to, or, if the bucket holds
     more than 256 rotations, the most significant 8 bits of the position.

     Comparing quadrants of two rotations is a fast way of comparing the
     rotations, even if they have long common prefix.  If two rotations belong
     to the same small bucket but they have different quadrants, then the
     lexicographical order of the rotations is the same as order of their
     quadrants.

     Quadrants are stored in mtfv[], but they have nothing to do with MTF
     values -- they just share the same storage. */
  Short *quadrant = s->mtfv;

  Byte  bigDone[256];         /* One for big buckets that are done. */

  Int *ftab; /* Cumulative frequencies of 2-character pairs. (freq. table) */
  Int *ptr;  /* Pointers to rotations as they are being ordered. (pointrers) */

  /* For small blocks the 16-bit bucket sort would be an overkill.  We can't
     use Shell sort because it requires quadrant, which isn't initialised yet,
     so we just use a naive counting sort in this case. */
  if (nblock <= RS_MBS) {
    radix_sort_bwt(block, nblock, &s->bwt_idx);
    return;
  }

  /* Sort buckets.  After the sort ftab contains the first location of every
     small bucket. */
  ptr = xmalloc(nblock * sizeof(Int));
  ftab = xmalloc(65537 * sizeof(Int));
  bucket_sort(ptr, block, ftab, nblock);
  bzero(bigDone, 256);

  /* Step Q2. Create quadrants. */
  for (i = 0; i < nblock-1; i++)
    pokes(quadrant + i, (block[i] << 8) | block[i+1]);
  pokes(quadrant + nblock-1, (block[nblock-1] << 8) | block[0]);
  for (i = 0; i < BZ_N_OVERSHOOT; i++)
    quadrant[nblock+i] = quadrant[i];

  /* Shallow factor is equal to 0 when the user specifies `--exponential'. */
  if (s->shallow_factor > 0) {
    if (copy_cache_wrap(ptr, ftab, quadrant, block, bigDone, nblock,
        s->shallow_factor, &s->bwt_idx))
      goto ok;
  }

  bpr_sort(ptr, ftab, bigDone, nblock);

  /* Compute BWT. */
  s->bwt_idx = 900000;
  for (i = 0; i < nblock; i++) {
    Int j = ptr[i];
    if (unlikely(j == 0)) {
      assert(s->bwt_idx == 900000);
      s->bwt_idx = i;
      j = nblock;
    }
    block[i] = *(Byte *)&quadrant[j-1];
  }

ok:
  assert(s->bwt_idx < nblock);
  free(ftab);
  free(ptr);
}


#if 0

/* Manber-Myers algorithm, O(n log n) */
static void
manber_myers(Byte *D, Int n, Int *x)
{
  Int *P, *R, *C;
  Byte *A, *B;
  Int i, j, d, e, h;

  A = xmalloc(n * sizeof(Byte));
  B = xmalloc((n+1) * sizeof(Byte));
  C = xmalloc(max(n,256) * sizeof(Int));
  P = xmalloc(n * sizeof(Int));
  R = xmalloc(n * sizeof(Int));

  for (i = 0; i < 256; i++) C[i] = 0;
  for (i = 0; i < n; i++) C[D[i]]++;
  for (i = 1; i < 256; i++) C[i] += C[i-1];
  for (i = 0; i < n; i++) P[--C[D[i]]] = i;

  for (i = 0; i < n; i++) B[i] = 0;
  for (i = 0; i < 256; i++) B[C[i]] = 1;
  B[n] = 1;

  for (h = 1; h < n; h *= 2) {
    for (i = 0; i < n; i = j) {
      C[i] = i;
      for (j = i; j == i || !B[j]; j++) {
        R[P[j]] = i;
        A[j] = 0;
      }
    }

    for (i = 0; i < n; i = j) {
      for (j = i; j == i || !B[j]; j++) {
        d = (P[j] - h + n) % n;
        e = R[d];
        R[d] = C[e]++;
        A[R[d]] = 1;
      }

      for (j = i; j == i || !B[j]; j++) {
        d = (P[j] - h + n) % n;
        e = R[d];
        if (A[e]) {
          e++;
          while (!B[e] && A[e]) {
            A[e] = 0;
            e++;
          }
        }
      }
    }

    for (i = 0; i < n; i++) {
      P[R[i]] = i;
      B[i] |= A[i];
    }
  }

  *x = R[0];
  P[R[0]] = n;
  for (i = 0; i < n; i++) B[i] = D[i];
  for (i = 0; i < n; i++) D[i] = B[P[i]-1];

  free(A);
  free(B);
  free(C);
  free(P);
  free(R);
}

#endif

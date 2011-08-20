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


/* This file implements three algorithms for sorting rotations of a string.
   All of them have space complexity of O(n), they differ in time complexity.


   I. The Cache-Copy Algorithm

   Implementation adopted from bzip2-1.0.5.

   A Block-sorting Lossless Data Compression Algorithm
   Algorithm Q: Fast quicksorting on suffixes
   SRC-124, Digital Equipment Corporation
   ftp://gatekeeper.dec.com/pub/DEC/SRC/research-reports/SRC-124.pdf


   II. The Bucket-Pointer-Refinement (BPR) algorithm

   An Incomplex Algorithm for Fast Suffix Array Construction
   Klaus-Bernd Schuermann, Jens Stoye
   http://techfak.uni-bielefeld.de/~stoye/cpublications/alenex2005final.pdf


   III. The LSB radix sort algotim - O(n^2)

   It consists of n passes of counting sort, each pass is O(n).  First sort
   all rotations according to their n-th character, then (n-1)-th and so on.
   Finally sort them according to the 1st character.
*/

#include <config.h>

#include <setjmp.h>     /* setjmp() */
#include <string.h>     /* memcmp() */
#include <strings.h>    /* bzero() */

#include "encode.h"


#if 0
# include <stdio.h>
# define Trace(x) fprintf x
#else
# define Trace(x)
#endif


struct Work {
  SLong budget;
  jmp_buf jmp_buffer;
};


/*=========================================================================
  (I) THE LSB RADIX SORT ALGORITHM
  ========================================================================= */

/* If a block size is less or equal to this value, the LSB radix sort
   will be used to compute BWT for that particular block. */
#define RADIX_SORT_THRESH 512

/* The naive, O(n^2) algorithm, which uses counting sort.
   It works in all cases, but it should be used for small
   blocks only because it is very slow in practice. */
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
  Int P[RADIX_SORT_THRESH];
  Int U[RADIX_SORT_THRESH];
  Byte B[RADIX_SORT_THRESH];

  assert(n <= RADIX_SORT_THRESH);

  /* Zero counts. */
  d = 0;
  bzero(C, 256 * sizeof(Int));

  /* Counting sort doesn't sort in place.  Instead sorting to a temporary
     location and copying back in each step, we sort from P to U and then
     back from U to P.  We need to distinguish between odd and even block
     sizes because after final step we need to have indices placed in P.
     For even n, start in P, for odd n start in U. */
  if (n & 1)
  {
    for (i = 0; i < n; i++)
    {
      U[i] = i;
      C[B[i] = D[i]]++;
    }

    t = 0;
    for (i = 0; i < 256; i++)
      C[i] = (t += C[i]) - C[i];
    assert(t == n);

    goto inner;
  }

  for (i = 0; i < n; i++)
  {
    P[i] = i;
    C[B[i] = D[i]]++;
  }

  for (i = 1; i < 256; i++)
    C[i] += C[i-1];
  assert(C[255] == n);

  while (d < n)
  {
    /* Sort from P to U, indices descending. */
    d++;
    for (i = n; i > 0; i--)
    {
      j = P[i-1];
      t = j + n - d;
      if (t >= n) t -= n;
      U[--C[B[t]]] = j;
    }

  inner:
    /* Sort from U to P, indices ascending. */
    d++;
    for (i = 0; i < n; i++)
    {
      j = U[i];
      t = j + n - d;
      if (t >= n) t -= n;
      P[C[B[t]]++] = j;
    }

    assert(C[255] == n);
  }

  /* Compute the BWT transformation from sorted order. */
  *bwt_idx = n;
  for (i = 0; i < n; i++)
  {
    j = P[i];
    if (j == 0)
    {
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
    while (i+4 <= i_lim)                        \
    {                                           \
      v = peekl(block + i);                     \
      u = (u << 8) | (v >> 24); op; i++;        \
      u = v >> 16; op; i++;                     \
      u = v >> 8; op; i++;                      \
      u = v; op; i++;                           \
    }                                           \
                                                \
    while (i < nblock)                          \
    {                                           \
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

/*
block - S
quadrant - W
ptr - V
nblock - N
*/

#define FULLGT_GRANULARITY 256

/* Compare two different rotations of the block.
   Return 1 if j-th rotation preceeds i-th one.
   Otherwise return 0 (even in case both rotations are equal).
   (The name of this routine stands for Full Greater-Than).
*/
static int
fullGt(
  Int i,
  Int j,
  const Short *qptr,
  Int nblock,
  struct Work *work)
{
  Int k = nblock / FULLGT_GRANULARITY;

  do
  {
    int rv;
    if ((rv = memcmp(qptr+i, qptr+j, 2 * FULLGT_GRANULARITY)) != 0)
      return rv > 0;

    work->budget--;
    i += FULLGT_GRANULARITY; if (i >= nblock) i -= nblock;
    j += FULLGT_GRANULARITY; if (j >= nblock) j -= nblock;
  }
  while (k-- > 0);

  /* If we got to this point it means that the block consists of a string
     repeated number of times.
  */
  Trace((stderr, "Very repetetive data; switching to BPR immediately...\n"));
  longjmp(work->jmp_buffer, 1);
}


/*---------------------------------------------*/
/*--
   Knuth's increments seem to work better
   than Incerpi-Sedgewick here.  Possibly
   because the number of elems to sort is
   usually small, typically <= 20.
--*/
static
Int incs[] = { 1, 4, 13, 40, 121, 364, 1093, 3280,
               9841, 29524, 88573, 265720,
               797161, 900000 };  /* 900000 is a sentinel */

static void
shell_sort(
  Int *ptr,
  const Short *qptr,
  Int nblock,
  Int lo,
  Int hi,
  struct Work *work)
{
  Int i, j, h, hp;

  if (hi - lo < 2) return;

  hp = 0;
  while (incs[hp] < hi - lo) hp++;

  while (hp > 0) {
    h = incs[--hp];

    i = lo + h;
    while (1) {

#define ITER                                                    \
      {                                                         \
        Int t,u,v;                                              \
        if (i >= hi) break;                                     \
        v = ptr[i];                                             \
        j = i;                                                  \
        u = j-h;                                                \
        while (fullGt((t = ptr[u]), v, qptr, nblock, work))     \
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
      {
        Trace((stderr, "Block is too repetitive; "
               "switching to BPR algorithm...\n"));
        longjmp(work->jmp_buffer, 1);
      }
    }
  }
}

/*---------------------------------------------*/
/*--
   The following is an implementation of
   an elegant 3-way quicksort for strings,
   described in a paper "Fast Algorithms for
   Sorting and Searching Strings", by Robert
   Sedgewick and Jon L. Bentley.
--*/

#define mswap(zzt, zz1, zz2)                    \
  zzt = zz1; zz1 = zz2; zz2 = zzt;

#define mvswap(zzp1, zzp2, zzn)                 \
  {                                             \
    Int yyp1 = (zzp1);                          \
    Int yyp2 = (zzp2);                          \
    Int yyn  = (zzn);                           \
    Int yyt;                                    \
    while (yyn > 0) {                           \
      mswap(yyt, ptr[yyp1], ptr[yyp2]);         \
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
  CAS(1,2,1,2); CAS(1,2,3,4); PHI(1,2,5);
  CAS(2,3,1,3); CAS(2,3,2,5); PHI(2,3,4);
  CAS(3,4,1,2); CAS(3,4,3,4); PHI(3,4,5);
  CAS(4,5,2,3); CAS(4,5,4,5); PHI(4,5,1);
  CAS(5,6,3,4); PHI(5,6,1); PHI(5,6,2); PHI(5,6,5);
  (void)(r61 + r62 + r64 + r65);
  return r63;
}

#undef CAS
#undef PHI



#define BZ_N_RADIX 2
#define BZ_N_QSORT 16
#define BZ_N_SHELL 18
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

  while (1)
  {
    while (hi-lo <= QSORT_SMALL_THRESH || qptr > qptr_max)
    {
      shell_sort ( ptr, qptr, nblock, lo, hi, work );
      if (sp == 0)
        return;
      vec = stack[--sp];
      qptr = quadrant + (Byte)vec;
      vec >>= 8;
      lo = vec;
      hi = vec + (vec >> 32);
    }

    pivot = med5(peeks(qptr + ptr[ lo         ]),
                 peeks(qptr + ptr[ lo+(hi-lo)/4 ]),
                 peeks(qptr + ptr[ (lo+hi)>>1 ]),
                 peeks(qptr + ptr[ lo+3*(hi-lo)/4 ]),
                 peeks(qptr + ptr[ hi-1       ]));

    unLo = ltLo = lo;
    unHi = gtHi = hi;

    /* Perform a fast, three-way partitioning.

       This code is based on the following descriptions:
       "QuickSort is Optimal" by Robert Sedgewick and Jon Bentley
       "Engineering a Sort Function" by Jon Bentley and Douglas McIlroy

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
    while (1) {
      assert(unLo < unHi);
      /* Move from left to find an element that is not less. */
      while ((n = peeks(qptr + ptr[unLo])) <= pivot) {
        /* If the element is equal, move it to the left. */
        if (n == pivot) {
          mswap(t, ptr[unLo], ptr[ltLo]);
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
          mswap(t, ptr[unHi-1], ptr[gtHi-1]);
          gtHi--;
        }
        unHi--;
        /* Stop if pointers have crossed */
        if (unLo >= unHi) goto qs_done;
      }
      /* Exchange. */
      mswap(t, ptr[unLo], ptr[unHi-1]); unLo++; unHi--;
      if (unLo >= unHi) goto qs_done;
    }

  qs_done:
    assert ( unHi == unLo);

    n = min(ltLo-lo, unLo-ltLo); mvswap(lo, unLo-n, n);
    m = min(hi-gtHi, gtHi-unHi); mvswap(unLo, hi-m, m);

    n = lo + unLo - ltLo;
    m = hi - gtHi + unHi;

    {
      Byte d = qptr - quadrant;
      Long v1 = ((Long)(n - lo) << 40) | (lo << 8) | d;
      Long v2 = ((Long)(m -  n) << 40) | (n  << 8) | (d + 1);
      Long v3 = ((Long)(hi - m) << 40) | (m  << 8) | d;

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


/*---------------------------------------------*/
/* Pre:
      nblock > N_OVERSHOOT
      block32 exists for [0 .. nblock-1 +N_OVERSHOOT]
      ((Byte*)block32) [0 .. nblock-1] holds block
      ptr exists for [0 .. nblock-1]

   Post:
      ((Byte*)block32) [0 .. nblock-1] holds block
      All other areas of block32 destroyed
      ftab [0 .. 65536 ] destroyed
      ptr [0 .. nblock-1] holds sorted order
      if (*budget <= 0), sorting was abandoned
*/


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
  for (i = BIG_START(ss); i < copy[ss]; i++)
  {
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
  for (i = BIG_END(ss); i > copy[ss]; i--)
  {
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
    if (k == 0) { *prim_idx = j; k = nblock; }
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
  Int qqBigSize[256];
  Int num_big = 0;

  /* Calculate the running order, from smallest to largest big bucket.
     We want to sort smaller buckets first because sorting them will
     populate some of quadrant descriptors, which can help a lot sorting
     bigger buckets. */
  for (i = 0; i < 256; i++)
  {
    qqBigSize[i] = BIG_SIZE(i);
    if (qqBigSize[i] > 0)
      bigOrder[num_big++] = i;
    else
      bigDone[i] = 1;
  }
  assert(num_big > 0);

  if (unlikely(num_big == 1))
  {
    Trace((stderr, "There is only one bucket!! Sorting skipped.\n"));
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
      best_size = 900000;
      for (j = i; j < num_big; j++)
      {
        ss = bigOrder[j];
        if (qqBigSize[ss] < best_size)
        {
          best_idx = j;
          best_bucket = ss;
          best_size = qqBigSize[ss];
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
          while ( SMALL_SIZE(ss,smallOrder[k-h]) > SMALL_SIZE(ss,vv) )
          {
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
      qqBigSize[ss] -= SMALL_SIZE(ss,sb);
      update_quadrants(quadrant, ptr, ftab, ss, sb, nblock, block, &idx);
    }

    /* Steps Q6b, Q6c. */
    induce_orderings(ptr, ftab, quadrant, bigDone, ss, nblock);

    for (j = 0; j < 256; j++)
    {
      if (!bigDone[j])
      {
        qqBigSize[j] -= SMALL_SIZE(j,ss);
        update_quadrants(quadrant, ptr, ftab, j, ss, nblock, block, &idx);
      }
    }

    assert(qqBigSize[ss] == 0);
    bigDone[ss] = 1;
  }

  for (j = 0; j < 256; j++) {
    assert(bigDone[j]);
    assert(qqBigSize[j] == 0);
  }

  assert(idx < nblock);
  return idx;
}

static void
copy_cache_wrap(
  Int *ptr,
  Int *ftab,
  Short *quadrant,
  Byte *block,
  Byte *bigDone,
  Int nblock,
  Int shallow_factor,
  Int *bwt_idx,
  int *failed)
{
  struct Work work;
  work.budget = (SLong)nblock * shallow_factor / FULLGT_GRANULARITY;

  if (!setjmp(work.jmp_buffer))
  {
    *bwt_idx = copy_cache(ptr, ftab, quadrant, block, bigDone, nblock, &work);
    *failed = 0;
    return;
  }

  *failed = 1;
}


/*=========================================================================
  (IV) BUCKET POINTER REFINEMENT ALGORITHM
  ========================================================================= */


/*---------------------------------------------*/
static
void fallbackSimpleSort ( Int* fmap,
                          Int* eclass,
                          SInt   lo,
                          SInt   hi )
{
   SInt i, j, tmp;
   Int ec_tmp;

   if (lo == hi) return;

   if (hi - lo > 3) {
      for ( i = hi-4; i >= lo; i-- ) {
         tmp = fmap[i];
         ec_tmp = eclass[tmp];
         for ( j = i+4; j <= hi && ec_tmp > eclass[fmap[j]]; j += 4 )
            fmap[j-4] = fmap[j];
         fmap[j-4] = tmp;
      }
   }

   for ( i = hi-1; i >= lo; i-- ) {
      tmp = fmap[i];
      ec_tmp = eclass[tmp];
      for ( j = i+1; j <= hi && ec_tmp > eclass[fmap[j]]; j++ )
         fmap[j-1] = fmap[j];
      fmap[j-1] = tmp;
   }
}


/*---------------------------------------------*/
#define fswap(zz1, zz2) \
  { SInt zztmp = zz1; zz1 = zz2; zz2 = zztmp; }

#define fvswap(zzp1, zzp2, zzn)       \
{                                     \
  SInt yyp1 = (zzp1);                 \
  SInt yyp2 = (zzp2);                 \
  SInt yyn  = (zzn);                  \
  while (yyn > 0) {                   \
    fswap(fmap[yyp1], fmap[yyp2]);    \
    yyp1++; yyp2++; yyn--;            \
  }                                   \
}


#define fmin(a,b) ((a) < (b)) ? (a) : (b)

#define fpush(lz,hz) { stackLo[sp] = lz; \
                       stackHi[sp] = hz; \
                       sp++; }

#define fpop(lz,hz) { sp--;              \
                      lz = stackLo[sp];  \
                      hz = stackHi[sp]; }

#define FALLBACK_QSORT_SMALL_THRESH 10
#define FALLBACK_QSORT_STACK_SIZE   100


static
void fallbackQSort3 ( Int* fmap,
                      Int* eclass,
                      SInt   loSt,
                      SInt   hiSt )
{
   SInt unLo, unHi, ltLo, gtHi, n, m;
   SInt sp, lo, hi;
   Int med, r, r3;
   SInt stackLo[FALLBACK_QSORT_STACK_SIZE];
   SInt stackHi[FALLBACK_QSORT_STACK_SIZE];

   r = 0;

   sp = 0;
   fpush ( loSt, hiSt );

   while (sp > 0) {

      assert(sp < FALLBACK_QSORT_STACK_SIZE - 1);

      fpop ( lo, hi );
      if (hi - lo < FALLBACK_QSORT_SMALL_THRESH) {
         fallbackSimpleSort ( fmap, eclass, lo, hi );
         continue;
      }

      /* Random partitioning.  Median of 3 sometimes fails to
         avoid bad cases.  Median of 9 seems to help but
         looks rather expensive.  This too seems to work but
         is cheaper.  Guidance for the magic constants
         7621 and 32768 is taken from Sedgewick's algorithms
         book, chapter 35.
      */
      r = ((r * 7621) + 1) % 32768;
      r3 = r % 3;
      if (r3 == 0) med = eclass[fmap[lo]]; else
      if (r3 == 1) med = eclass[fmap[(lo+hi)>>1]]; else
                   med = eclass[fmap[hi]];

      unLo = ltLo = lo;
      unHi = gtHi = hi;

      while (1) {
         while (1) {
            if (unLo > unHi) break;
            n = (SInt)eclass[fmap[unLo]] - (SInt)med;
            if (n == 0) {
               fswap(fmap[unLo], fmap[ltLo]);
               ltLo++; unLo++;
               continue;
            }
            if (n > 0) break;
            unLo++;
         }
         while (1) {
            if (unLo > unHi) break;
            n = (SInt)eclass[fmap[unHi]] - (SInt)med;
            if (n == 0) {
               fswap(fmap[unHi], fmap[gtHi]);
               gtHi--; unHi--;
               continue;
            }
            if (n < 0) break;
            unHi--;
         }
         if (unLo > unHi) break;
         fswap(fmap[unLo], fmap[unHi]); unLo++; unHi--;
      }

      assert(unHi == unLo-1);

      if (gtHi < ltLo) continue;

      n = fmin(ltLo-lo, unLo-ltLo); fvswap(lo, unLo-n, n);
      m = fmin(hi-gtHi, gtHi-unHi); fvswap(unLo, hi-m+1, m);

      n = lo + unLo - ltLo - 1;
      m = hi - (gtHi - unHi) + 1;

      if (n - lo > hi - m) {
         fpush ( lo, n );
         fpush ( m, hi );
      } else {
         fpush ( m, hi );
         fpush ( lo, n );
      }
   }
}


/*---------------------------------------------*/
/* Pre:
      nblock > 0
      eclass exists for [0 .. nblock-1]
      ((Byte*)eclass) [0 .. nblock-1] holds block
      ptr exists for [0 .. nblock-1]

   Post:
      ((Byte*)eclass) [0 .. nblock-1] holds block
      All other areas of eclass destroyed
      fmap [0 .. nblock-1] holds sorted order
      bhtab [ 0 .. 2+(nblock/64) ] destroyed
*/

/* Here BH stands for `bucket header'. */
#define       SET_BH(zz)  bhtab[(zz) >> 6] |= ((Long)1 << ((zz) & 63))
#define     CLEAR_BH(zz)  bhtab[(zz) >> 6] &= ~((Long)1 << ((zz) & 63))
#define     ISSET_BH(zz)  (bhtab[(zz) >> 6] & ((Long)1 << ((zz) & 63)))
#define      WORD_BH(zz)  bhtab[(zz) >> 6]
#define UNALIGNED_BH(zz)  ((zz) & 63)


static void
bpr_sort(Int *fmap, Int *ftab, Byte *bigDone, SInt nblock)
{
  SInt d, i, j, k, l, r, cc, cc1;
  SInt nNotDone;
  SInt nBhtab;
  Long *bhtab;
  Int *eclass;


  nBhtab = (nblock + 2*64 + 63) / 64;
  bhtab = xalloc(nBhtab * sizeof(Long));
  bzero(bhtab, nBhtab * sizeof(Long));

  /*-- set sentinel bits for block-end detection --*/
  for (i = 0; i < 2*64; i += 2)
    SET_BH(nblock+i);

  d = 2;

  for (i = 0; i < 65536; i++)
  {
    j = ftab[i];
    r = ftab[i+1];
    SET_BH(j);
    if (bigDone[i >> 8] | bigDone[i & 0xff]) {
      for (k = j+1; k < r; k++)
        SET_BH(k);
    }
  }

  /*--
    Inductively refine the buckets.  Kind-of an
    "exponential radix sort" (!), inspired by the
    Manber-Myers suffix array construction algorithm.
    --*/
  eclass = xalloc(nblock * sizeof(Int));

  /* This is an implementation of Bucket-Pointer Refinement algorithm
     as described in a paper "An Incomplex Algorithm for Fast Suffix
     Array Construction" by Klaus-Bernd Schuermann and Jens Stoye. */

  /*-- the log(N) loop --*/
  while (1) {

    for (i = 0; i < nblock; i++) {
      if (ISSET_BH(i)) j = i;
      k = fmap[i] - d; if (k < 0) k += nblock;
      eclass[k] = j;
    }

    nNotDone = 0;
    r = 0;
    while (1) {

      /*-- find the next non-singleton bucket --*/
      k = r + 1;
      while (ISSET_BH(k) && UNALIGNED_BH(k)) k++;
      if (ISSET_BH(k)) {
        while (WORD_BH(k) == ~(Long)0) k += 64;
#ifdef __GNUC__
        k += __builtin_ctzl(~WORD_BH(k));
#else
        while (ISSET_BH(k)) k++;
#endif
      }
      l = k - 1;
      if (l >= nblock) break;
      while (!ISSET_BH(k) && UNALIGNED_BH(k)) k++;
      if (!ISSET_BH(k)) {
        while (WORD_BH(k) == 0) k += 64;
#ifdef __GNUC__
        k += __builtin_ctzl(WORD_BH(k));
#else
        while (!ISSET_BH(k)) k++;
#endif
      }
      r = k - 1;
      if (unlikely(r >= nblock)) break;

      /*-- now [l, r] bracket current bucket --*/
      if (r > l) {
        nNotDone += (r - l + 1);
        fallbackQSort3 ( fmap, eclass, l, r );

        /*-- scan bucket and generate header bits --*/
        cc = -1;
        for (i = l; i <= r; i++) {
          cc1 = eclass[fmap[i]];
          if (cc != cc1) { SET_BH(i); cc = cc1; };
        }
      }
    }

    d *= 2;
    if (d >= nblock || nNotDone == 0) break;
  }

  xfree(eclass);
  xfree(bhtab);
}


/*=========================================================================
  (IV) MASTER ALGORITHM
  ========================================================================= */

void
YBpriv_block_sort(YBenc_t *s)
{
  Int i;
  Int nblock = s->nblock;
  Byte *block = s->block;
  Short *quadrant = s->mtfv;
  Byte  bigDone[256];
  int sort_failed;

  Int *ftab;
  Int *ptr;


  /* For small blocks the 16-bit bucket sort would be an overkill.
     We can't use Shell sort because it requires quadrant, which isn't
     initialised yet, so we just use a naive counting sort in this case. */
  if (nblock <= RADIX_SORT_THRESH)
  {
    radix_sort_bwt(block, nblock, &s->bwt_idx);
    return;
  }


  /* Sort buckets.  After the sort ftab contains the first location
     of every small bucket. */
  ptr = xalloc(nblock * sizeof(Int));
  ftab = xalloc(65537 * sizeof(Int));
  bucket_sort(ptr, block, ftab, nblock);
  bzero(bigDone, 256);


  /* Step Q2. */
  for (i = 0; i < nblock-1; i++)
    pokes(quadrant + i, (block[i] << 8) | block[i+1]);
  pokes(quadrant + nblock-1, (block[nblock-1] << 8) | block[0]);
  for (i = 0; i < BZ_N_OVERSHOOT; i++)
    quadrant[nblock+i] = quadrant[i];


  if (s->shallow_factor > 0)
  {
    copy_cache_wrap(ptr, ftab, quadrant, block, bigDone, nblock,
                    s->shallow_factor, &s->bwt_idx, &sort_failed);
    if (!sort_failed)
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
  xfree(ftab);
  xfree(ptr);
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

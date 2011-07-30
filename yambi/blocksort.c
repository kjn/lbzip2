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

   I. The naive algotim - O(n^2)

   It consists of n passes of counting sort, each pass is O(n).  First sort
   all rotations according to their n-th character, then (n-1)-th and so on.
   Finally sort them according to the 1st character.


   II. The Bucket-Pointer-Refinement (BPR) algorithm

   This algorithm consists of two phases.  In the first phase we sort all
   rotations of the source string accoriding to first two characters.
   This places each rotation into one of 65536 buckets.  In the second phase
   all non-singleton buckets are recursively refined.

   An Incomplex Algorithm for Fast Suffix Array Construction
   Klaus-Bernd Schuermann, Jens Stoye
   http://techfak.uni-bielefeld.de/~stoye/cpublications/alenex2005final.pdf


   III. The Cache-Copy Algorithm

   Implementation adopted from bzip2-1.0.5.
   On the performance of BWT sorting algorithms
   Julian Seward
*/


/*

block    -  900k
ptr      - 3433k

ftab     -  256k \ / bucket - 3443k
quadrant - 1717k / \

bhtab    -  110k


*/


#include "encode.h"

#define ALIGNMENT_REQUIRED 1

#if 0
# include <stdio.h>
# define Trace(x) fprintf x
#else
# define Trace(x)
#endif


/* The naive, O(n^2) algorithm, which uses counting sort.
   It works in all cases, but it should be called for very small
   blocks only because it is (as the name suggests) very slow. */
static void
naive_sort(Int *P, Byte *B, Int *U, Int n)
{
  int i;  /* index */
  int d;  /* distance */
  int k;
  int t;  /* temp */
  int C[256];

  /* Zero counts. */
  d = 0;
  for (i = 0; i < 256; i++)
    C[i] = 0;

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
      C[B[i]]++;
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
    C[B[i]]++;
  }

  for (i = 1; i < 256; i++)
    C[i] += C[i-1];
  assert(C[255] == n);

  while (d < n)
  {
    /* Sort from P to U, indices descending. */
    d++;
    for (i = n-1; i >= 0; i--)
    {
      k = P[i];
      t = k - d;
      t += n & -(t < 0);
      U[--C[B[t]]] = k;
    }

  inner:
    /* Sort from U to P, indices ascending. */
    d++;
    for (i = 0; i < n; i++)
    {
      k = U[i];
      t = k - d;
      t += n & -(t < 0);
      P[C[B[t]]++] = k;
    }

    assert(C[255] == n);
  }
}


/* Compare two different rotations of the block.
   Return 1 if j-th rotation preceeds i-th one.
   Otherwise return 0 (even in case both rotations are equal).
   (The name of this routine stands for Full Greater-Than).
*/
static inline int
fullGt(SInt   i,
       SInt   j,
       Byte  *block,
       Short *quadrant,
       Int    nblock,
       SLong *budget)
{
  SInt k;

  assert (i != j);

#define ITER(ii)                                        \
  {                                                     \
    Byte c1, c2;                                        \
    Short s1, s2;                                       \
    if ((c1 = block[i]) != (c2 = block[j]))             \
      return (c1 > c2);                                 \
    if ((s1 = quadrant[i]) != (s2 = quadrant[j]))       \
      return (s1 > s2);                                 \
    i++; j++;                                           \
  }

 compare_slowly:
  ITER(0); ITER(1); ITER(2); ITER(3); ITER(4); ITER(5); ITER(6);
  while (i & 7) ITER(0);

  if (ALIGNMENT_REQUIRED && likely(j & 7))
    goto bad_alignment;


  k = nblock;
  do {
    if ((*(Long *)(   block+i  ) ^ *(Long *)(   block+j  )) |
        (*(Long *)(quadrant+i  ) ^ *(Long *)(quadrant+j  )) |
        (*(Long *)(quadrant+i+4) ^ *(Long *)(quadrant+j+4)))
      goto compare_slowly;
    i += 8; j += 8;
    if (i > nblock) i -= nblock;
    if (j > nblock) j -= nblock;
    k -= 8;
    (*budget)--;
  } while (k >= 0);
  return 0;

 bad_alignment:

  /* TODO: optimize for machines that require aligned access.
     Optimization can be done in a similar way as glibc's memcmp
     (with one pointer aligned and a shift register). */

  k = nblock;
  do {
    ITER(0); ITER(1); ITER(2); ITER(3);
    ITER(4); ITER(5); ITER(6); ITER(7);
    if (i > nblock) i -= nblock;
    if (j > nblock) j -= nblock;
    k -= 8;
    (*budget)--;
  } while (k >= 0);
  return 0;

#undef ITER
}


/*---------------------------------------------*/
/*--
   Knuth's increments seem to work better
   than Incerpi-Sedgewick here.  Possibly
   because the number of elems to sort is
   usually small, typically <= 20.
--*/
static
SInt incs[14] = { 1, 4, 13, 40, 121, 364, 1093, 3280,
                  9841, 29524, 88573, 265720,
                  797161, 2391484 };

static inline void
shell_sort(Int   *ptr,
           Byte  *block,
           Short *quadrant,
           SInt   nblock,
           SInt   lo,
           SInt   hi,
           SInt   d,
           SLong *budget)
{
  SInt i, j, h, bigN, hp;
  Int v;

  bigN = hi - lo + 1;
  if (bigN < 2) return;

  hp = 0;
  while (incs[hp] < bigN) hp++;
  hp--;

  for (; hp >= 0; hp--) {
    h = incs[hp];

    i = lo + h;
    while (1) {

#define ITER                                                    \
      {                                                         \
        SInt t,u;                                               \
        if (i > hi) break;                                      \
        v = ptr[i];                                             \
        j = i;                                                  \
        u = j-h;                                                \
        while (fullGt((t = ptr[u])+d, v+d, block, quadrant,     \
                      nblock, budget))                          \
          {                                                     \
            ptr[j] = t;                                         \
            j = u;                                              \
            u -= h;                                             \
            if (u <= lo - 1) break;                             \
          }                                                     \
        ptr[j] = v;                                             \
        i++;                                                    \
      }

      ITER; ITER; ITER;

      if (*budget < 0) return;
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
    SInt yyp1 = (zzp1);                         \
    SInt yyp2 = (zzp2);                         \
    SInt yyn  = (zzn);                          \
    SInt yyt;                                   \
    while (yyn > 0) {                           \
      mswap(yyt, ptr[yyp1], ptr[yyp2]);         \
      yyp1++; yyp2++; yyn--;                    \
    }                                           \
  }

static inline Byte
mmed3 ( Byte a, Byte b, Byte c )
{
  Byte t;
  if (a > b) { t = a; a = b; b = t; };
  if (b > c) {
    b = c;
    if (a > b) b = a;
  }
  return b;
}

#define mmin(a,b) ((a) < (b)) ? (a) : (b)

#define mpush(lz,hz,dz) { stack[sp++] = lz; \
                          stack[sp++] = hz; \
                          stack[sp++] = dz; }

#define mpop(lz,hz,dz) { dz = stack[--sp]; \
                         hz = stack[--sp]; \
                         lz = stack[--sp]; }


#define BZ_N_RADIX 2
#define BZ_N_QSORT 16
#define BZ_N_SHELL 18
#define BZ_N_OVERSHOOT (BZ_N_RADIX + BZ_N_QSORT + BZ_N_SHELL + 2)

#define QSORT_SMALL_THRESH 15
#define QSORT_DEPTH_THRESH (BZ_N_RADIX + BZ_N_QSORT)
#define QSORT_STACK_SIZE 100

static inline void
quick_sort(Int   *ptr,
           Byte  *block,
           Short *quadrant,
           SInt   nblock,
           SInt   lo,
           SInt   hi,
           SInt   d,
           SLong *budget)
{
  SInt unLo;  /* unique to low */
  SInt unHi;  /* unique to high */
  SInt ltLo;  /* less than low */
  SInt gtHi;  /* greater than high */
  SInt n, m;
  SInt med;   /* pivot */
  SInt sp;    /* stack pointer */
  SInt t;

  SInt stack[3*QSORT_STACK_SIZE];

  sp = 0;

  while (1)
  {
    while (hi - lo < QSORT_SMALL_THRESH || d > QSORT_DEPTH_THRESH)
    {
      shell_sort ( ptr, block, quadrant, nblock, lo, hi, d, budget );
      if (sp == 0 || *budget < 0)
        return;
      mpop(lo, hi, d);
    }

    med = (SInt)
      mmed3 ( block[ptr[ lo         ]+d],
              block[ptr[ hi         ]+d],
              block[ptr[ (lo+hi)>>1 ]+d] );

    unLo = ltLo = lo;
    unHi = gtHi = hi;

    /* Perform three way partition. */
    while (1) {
      while (1) {
        if (unLo > unHi) break;
        n = ((SInt)block[ptr[unLo]+d]) - med;
        if (n == 0) {
          mswap(t, ptr[unLo], ptr[ltLo]);
          ltLo++; unLo++; continue;
        }
        if (n >  0) break;
        unLo++;
      }
      while (1) {
        if (unLo > unHi) break;
        n = ((SInt)block[ptr[unHi]+d]) - med;
        if (n == 0) {
          mswap(t, ptr[unHi], ptr[gtHi]);
          gtHi--; unHi--; continue;
        }
        if (n <  0) break;
        unHi--;
      }
      if (unLo > unHi) break;
      mswap(t, ptr[unLo], ptr[unHi]); unLo++; unHi--;
    }

    assert ( unHi == unLo-1);

    if (gtHi < ltLo) {
      d++;
      continue;
    }

    n = mmin(ltLo-lo, unLo-ltLo); mvswap(lo, unLo-n, n);
    m = mmin(hi-gtHi, gtHi-unHi); mvswap(unLo, hi-m+1, m);

    n = lo + unLo - ltLo - 1;
    m = hi - (gtHi - unHi) + 1;

    {
      SInt n0h, n0l, n0d;
      SInt n1h, n1l, n1d;
      SInt n2h, n2l, n2d;
      SInt t;

      n0l = lo;  n0h = n;   n0d = d;
      n1l = m;   n1h = hi;  n1d = d;
      n2l = n+1; n2h = m-1; n2d = d+1;

      if (n0h - n0l < n1h - n1l)
      {
        mswap(t, n0h, n1h);
        mswap(t, n0l, n1l);
      }
      if (n1h - n1l < n2h - n2l)
      {
        mswap(t, n1h, n2h);
        mswap(t, n1l, n2l);
        mswap(t, n1d, n2d);
      }
      if (n0h - n0l < n1h - n1l)
      {
        mswap(t, n0h, n1h);
        mswap(t, n0l, n1l);
        mswap(t, n0d, n1d);
      }

      assert(sp <= 3*(QSORT_STACK_SIZE-2));
      mpush (n0l, n0h, n0d);
      mpush (n1l, n1h, n1d);
      lo = n2l; hi = n2h; d = n2d;
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
      if (*budget < 0), sorting was abandoned
*/

/* NOTE: I'm not quite sure if this code is endian-neutral. */
#define BUCKET_SORT(op)                         \
  {                                             \
    Int i = 0;                                  \
    Int i_lim = nblock & 0xFFFF8;               \
    Int a0, a1, b0, b1, c0, c1, d0, d1;         \
    Int e0, e1, f0, f1, g0, g1, h0, h1;         \
    Int s, u;                                   \
    Long w;                                     \
                                                \
    s = block[nblock - 1] << 8;                 \
                                                \
    while (i+8 <= i_lim)                        \
    {                                           \
      /* Little endian code:                    \
      w = *(Long *)(block + i);                 \
      a1 = (w <<  8) & 0xFF00;                  \
      a0 = (w      ) & 0x00FF;                  \
      b1 = (w      ) & 0xFF00;                  \
      b0 = (w >>  8) & 0x00FF;                  \
      c1 = (w >>  8) & 0xFF00;                  \
      c0 = (w >> 16) & 0x00FF;                  \
      d1 = (w >> 16) & 0xFF00;                  \
      d0 = (w >> 24) & 0x00FF;                  \
      e1 = (w >> 24) & 0xFF00;                  \
      e0 = (w >> 32) & 0x00FF;                  \
      f1 = (w >> 32) & 0xFF00;                  \
      f0 = (w >> 40) & 0x00FF;                  \
      g1 = (w >> 40) & 0xFF00;                  \
      g0 = (w >> 48) & 0x00FF;                  \
      h1 = (w >> 48) & 0xFF00;                  \
      h0 = (w >> 56);                           \
      */                                        \
      a0 = block[i  ]; a1 = a0 << 8;            \
      b0 = block[i+1]; b1 = b0 << 8;            \
      c0 = block[i+2]; c1 = c0 << 8;            \
      d0 = block[i+3]; d1 = d0 << 8;            \
      e0 = block[i+4]; e1 = e0 << 8;            \
      f0 = block[i+5]; f1 = f0 << 8;            \
      g0 = block[i+6]; g1 = g0 << 8;            \
      h0 = block[i+7]; h1 = h0 << 8;            \
                                                \
      u = s  | a0; op; i++;                     \
      u = a1 | b0; op; i++;                     \
      u = b1 | c0; op; i++;                     \
      u = c1 | d0; op; i++;                     \
      u = d1 | e0; op; i++;                     \
      u = e1 | f0; op; i++;                     \
      u = f1 | g0; op; i++;                     \
      u = g1 | h0; op; i++;                     \
      s = h1;                                   \
    }                                           \
                                                \
    while (i < nblock)                          \
    {                                           \
      a0 = block[i];                            \
      a1 = a0 << 8;                             \
      u = s | a0; op; i++;                      \
      s = a1;                                   \
    }                                           \
  }

static void
bucket_sort(Int  *ptr,
            Byte *block,
            Int  *ftab,
            Int   nblock)
{
  Int f;
  Int i;

  /*-- set up the 2-byte frequency table --*/
  for (i = 0; i <= 65536; i++) ftab[i] = 0;
  BUCKET_SORT(ftab[u]++);

  /*-- Complete the initial radix sort --*/
  for (i = 0; i < 65536; i++) ftab[i+1] += ftab[i];
  assert(ftab[65536] == nblock);
  f = ftab[(block[nblock-1] << 8) | block[0]];
  BUCKET_SORT(ptr[--ftab[u]] = i-1);
  ptr[f-1] = nblock-1;
}


#define BIGFREQ(b) (ftab[((b)+1) << 8] - ftab[(b) << 8])
/* A flag indicating that a small bucket is done. */
#define SETMASK (1 << 20)
#define CLEARMASK (~SETMASK)


/* return 1 if failed, 0 if success */
static int
shallow_sort(Int *ptr,
             Byte *block,
             Short *quadrant,
             Int *ftab,
             Int  nblock,
             Int  work_factor)
{
  SInt  i, j, k, ss, sb;
  Byte  runningOrder[256];
  Byte  bigDone[256];
  SInt  copyStart[256];
  SInt  copyEnd  [256];
  Byte  c1;
  SLong budget = nblock * work_factor;

  assert(ptr);
  assert(block);
  assert(quadrant);
  assert(ftab);
  assert(nblock > 0);

  /* For small blocks the 16-bit bucket sort would be an overkill.
     We can't use Shell sort because it requires quadrant, which isn't
     initialised yet, so we just use a naive counting sort in this case. */
  if (nblock <= 256)
  {
    naive_sort(ptr, block, ftab, nblock);
    return 0;
  }

  /* Sort buckets.  After the sort ftab contains the first location
     of every small bucket. */
  bucket_sort(ptr, block, ftab, nblock);

  if (work_factor == 0)
    return 1;

  /* Calculate the running order, from smallest to largest big bucket.
     We want to sort smaller buckets first because sorting them will
     populate some of quadrant descriptors, which can help a lot sorting
     bigger buckets. */
  for (i = 0; i <= 255; i++)
  {
    bigDone     [i] = 0;
    runningOrder[i] = i;
  }

  {
    Byte vv;
    SInt h = 1;
    do h = 3 * h + 1; while (h <= 256);
    do {
      h = h / 3;
      for (i = h; i <= 255; i++) {
        vv = runningOrder[i];
        j = i;
        while ( BIGFREQ(runningOrder[j-h]) > BIGFREQ(vv) )
        {
          runningOrder[j] = runningOrder[j-h];
          j = j - h;
          if (j <= (h - 1)) break;
        }
        runningOrder[j] = vv;
      }
    } while (h != 1);
  }

  /* If the biggest bucket size is equal to block size, then we have only
     one bucket.  This means that the input block consists of one character
     repeated `nblock' times.  In this case the input block is already sorted
     (bucket sort phase sorted it) and there's nothing more to do. */
  if (BIGFREQ(runningOrder[255]) == nblock)
    return 0;

  /* Create the overshoot area. */
  for (i = 0; i < BZ_N_OVERSHOOT; i++)
    block[nblock+i] = block[i];

  /* Create quadrants. */
  for (i = 0; i < nblock+BZ_N_OVERSHOOT; i++)
    quadrant[i] = 0;


  /*--
    The main sorting loop.
    --*/

  for (i = 0;; i++)
  {
    /*--
      Process big buckets, starting with the least full.
      Basically this is a 3-step process in which we call
      quick_sort to sort the small buckets [ss, j], but
      also make a big effort to avoid the calls if we can.
      --*/
    ss = runningOrder[i];

    /*--
      Step 1:
      Complete the big bucket [ss] by quicksorting
      any unsorted small buckets [ss, j], for j != ss.
      Hopefully previous pointer-scanning phases have already
      completed many of the small buckets [ss, j], so
      we don't have to sort them at all.
      --*/
    for (j = 0; j <= 255; j++)
    {
      if (j != ss)
      {
        sb = (ss << 8) + j;
        if ( ! (ftab[sb] & SETMASK) )
        {
          SInt lo = ftab[sb]   & CLEARMASK;
          SInt hi = (ftab[sb+1] & CLEARMASK) - 1;
          if (hi > lo)
          {
            quick_sort(ptr, block, quadrant, nblock,
                       lo, hi, BZ_N_RADIX, &budget);
            if (budget < 0)
              return 1;
          }
        }
        ftab[sb] |= SETMASK;
      }
    }

    assert ( !bigDone[ss]);

    /*--
      Step 2:
      Now scan this big bucket [ss] so as to synthesise the
      sorted order for small buckets [t, ss] for all t,
      including, magically, the bucket [ss,ss] too.
      This will avoid doing Real Work in subsequent Step 1's.
      --*/
    for (j = 0; j <= 255; j++)
    {
      copyStart[j] =  ftab[(j << 8) + ss]     & CLEARMASK;
      copyEnd  [j] = (ftab[(j << 8) + ss + 1] & CLEARMASK) - 1;
    }
    for (j = ftab[ss << 8] & CLEARMASK; j < copyStart[ss]; j++)
    {
      k = ptr[j]-1; if (k < 0) k += nblock;
      c1 = block[k];
      if (!bigDone[c1])
        ptr[ copyStart[c1]++ ] = k;
    }
    for (j = (ftab[(ss+1) << 8] & CLEARMASK) - 1; j > copyEnd[ss]; j--)
    {
      k = ptr[j]-1; if (k < 0) k += nblock;
      c1 = block[k];
      if (!bigDone[c1])
        ptr[ copyEnd[c1]-- ] = k;
    }

    assert(copyStart[ss]-1 == copyEnd[ss] ||
           (copyStart[ss] == 0 && copyEnd[ss] == nblock-1));

    /* We're done if the last big bucket has just been processed. */
    if (i == 255)
      return 0;

    /* Mark all small buckets [j,ss] as done. */
    for (j = 0; j <= 255; j++)
      ftab[(j << 8) + ss] |= SETMASK;

    /*--
      Step 3:
      The [ss] big bucket is now done.  Record this fact,
      and update the quadrant descriptors.  Remember to
      update quadrants in the overshoot area too, if
      necessary.

      The quadrant array provides a way to incrementally
      cache sort orderings, as they appear, so as to
      make subsequent comparisons in fullGt() complete
      faster.  For repetitive blocks this makes a big
      difference (but not big enough to be able to avoid
      the fallback sorting mechanism, exponential radix sort).

      The precise meaning is: at all times:

      for 0 <= i < nblock and 0 <= j <= nblock

      if block[i] != block[j],

      then the relative values of quadrant[i] and
      quadrant[j] are meaningless.

      else {
      if quadrant[i] < quadrant[j]
      then the string starting at i lexicographically
      precedes the string starting at j

      else if quadrant[i] > quadrant[j]
      then the string starting at j lexicographically
      precedes the string starting at i

      else
      the relative ordering of the strings starting
      at i and j has not yet been determined.
      }
      --*/
    bigDone[ss] = 1;

    {
      SInt bbStart = ftab[ss << 8] & CLEARMASK;
      SInt bbSize  = (ftab[(ss+1) << 8] & CLEARMASK) - bbStart;
      SInt shifts  = 0;

      while (((bbSize-1) >> shifts) >= 65536) shifts++;

      for (j = bbSize-1; j >= 0; j--) {
        SInt a2update     = ptr[bbStart + j];
        Short qVal        = (Short)(j >> shifts);
        quadrant[a2update] = qVal;
        if (unlikely(a2update < BZ_N_OVERSHOOT))
          quadrant[a2update + nblock] = qVal;
      }
      assert ( ((bbSize-1) >> shifts) <= 65535);
    }

  }
}


/*---------------------------------------------*/
/*--- Fallback O(N log(N)^2) sorting        ---*/
/*--- algorithm, for repetitive blocks      ---*/
/*---------------------------------------------*/

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
#if 1
#define       SET_BH(zz)  bhtab[(zz) >> 6] |= ((Long)1 << ((zz) & 63))
#define     CLEAR_BH(zz)  bhtab[(zz) >> 6] &= ~((Long)1 << ((zz) & 63))
#define     ISSET_BH(zz)  (bhtab[(zz) >> 6] & ((Long)1 << ((zz) & 63)))
#define      WORD_BH(zz)  bhtab[(zz) >> 6]
#define UNALIGNED_BH(zz)  ((zz) & 63)
#else

#define       SET_BH(zz)        X_SET_BH(bhtab, nblock, (zz))
#define     CLEAR_BH(zz)      X_CLEAR_BH(bhtab, nblock, (zz))
#define     ISSET_BH(zz)      X_ISSET_BH(bhtab, nblock, (zz))
#define      WORD_BH(zz)       X_WORD_BH(bhtab, nblock, (zz))
#define UNALIGNED_BH(zz)  X_UNALIGNED_BH(bhtab, nblock, (zz))

static void X_SET_BH(Int *bhtab, Int nblock, Int zz)
{
#ifndef NDEBUG
  if (zz >= nblock+2*64)
  { Trace((stderr, "SET_BH(): zz=%u (nblock=%u)\n", zz, nblock)); }
#endif
  assert(zz < nblock+2*64);
  bhtab[(zz) >> 6] |= ((Long)1 << ((zz) & 63));
}

static void X_CLEAR_BH(Int *bhtab, Int nblock, Int zz)
{
#ifndef NDEBUG
  if (zz >= nblock+2*64)
  { Trace((stderr, "CLEAR_BH(): zz=%u (nblock=%u)\n", zz, nblock)); }
#endif
  assert(zz < nblock+2*64);
  bhtab[(zz) >> 6] &= ~((Long)1 << ((zz) & 63));
}

static int X_ISSET_BH(Int *bhtab, Int nblock, Int zz)
{
#ifndef NDEBUG
  if (zz >= nblock+2*64)
  { Trace((stderr, "ISSET_BH(): zz=%u (nblock=%u)\n", zz, nblock)); }
#endif
  assert(zz < nblock+2*64);
  return !!(bhtab[(zz) >> 6] & ((Long)1 << ((zz) & 63)));
}

static Long X_WORD_BH(Int *bhtab, Int nblock, Int zz)
{
#ifndef NDEBUG
  if (zz >= nblock+2*64)
  { Trace((stderr, "WORD_BH(): zz=%u (nblock=%u)\n", zz, nblock)); }
#endif
  assert(zz < nblock+2*64);
  return  bhtab[(zz) >> 6];
}

static int X_UNALIGNED_BH(Int *bhtab, Int nblock, Int zz)
{
#ifndef NDEBUG
  if (zz >= nblock+2*64)
  { Trace((stderr, "UNALIGNED_BH(): zz=%u (nblock=%u)\n", zz, nblock)); }
#endif
  assert(zz < nblock+2*64);
  return ((zz) & 63);
}

#endif


static void
deep_sort(Int *fmap,
          Int *eclass,
          Int *ftab,
          Long *bhtab,
          Int nblock)
{
  SInt d, i, j, k, l, r, cc, cc1;
  SInt nNotDone;
  SInt nBhtab;


  nBhtab = (nblock + 2*64 + 63) / 64;
  for (i = 0; i < nBhtab; i++)
    bhtab[i] = 0;
  /*-- set sentinel bits for block-end detection --*/
  for (i = 0; i < 2*64; i += 2)
    SET_BH(nblock+i);

  d = 2;

  for (i = 0; i < 65536; i++)
  {
    Int done = ftab[i] & SETMASK;
    j = ftab[i] & CLEARMASK;
    ftab[i] = j;
    r = ftab[i+1] & CLEARMASK;
    SET_BH(j);
    if (done)
      for (k = j+1; k < r; k++)
        SET_BH(k);
  }

  /*--
    Inductively refine the buckets.  Kind-of an
    "exponential radix sort" (!), inspired by the
    Manber-Myers suffix array construction algorithm.
    --*/

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
        /* TODO: instead of a linear scan we should do something clever */
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
}


void
YBpriv_block_sort(YBenc_t *s)
{
  int failed = shallow_sort(s->ptr, s->block, s->quadrant, s->ftab,
                            s->nblock, s->shallow_factor);
  if (failed)
  {
    Int *bucket;

    Trace((stderr,
           "Block is too repetitive; switching to BPR algorithm...\n"));

    bucket = xalloc(s->nblock * sizeof(Int));
    deep_sort(s->ptr, bucket, s->ftab, s->bhtab, s->nblock);
    xfree(bucket);
  }
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

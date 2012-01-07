/*-
  blocksort.c -- BWT encoder

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

#include <setjmp.h>     /* setjmp() */
#include <stdlib.h>     /* free() */
#include <string.h>     /* memcmp() */
#include <strings.h>    /* bzero() */

#include "xalloc.h"     /* xmalloc() */

#include "encode.h"


/* User-tunable parameters. */
#define CMP_STEP      256  /* 1 - 900,000 */
#define MAX_DEPTH      18  /* 0 - 900,000 */
#define SHELL_THRESH    9  /* 0 - 450,000 */

/* Hardcoded parameters. Changing them requires nontrivial code changes. */
#define INITIAL_DEPTH   2  /* hardcoded */
#define STACK_SIZE     32  /* > 2*log3(450000)-2*log3(SHELL_THRESH)+1 */
#define OVERSHOOT     MAX_DEPTH + CMP_STEP


/* Compute a+b in GF(n). */
static Int
add(Int a, Int b, Int n)
{
  return min(a+b, a+b-n);
}

/* Compute a-b in GF(n). */
static Int
sub(Int a, Int b, Int n)
{
  return min(a-b, a-b+n);
}

/* Swap two elements of array A[]. */
static void
swap(Int *A, Int i, Int j)
{
  Int t = A[i];
  A[i] = A[j];
  A[j] = t;
}

/* Select the median of 5 integers without branching. */
static Short
med5(Short a, Short b, Short c, Short d, Short e)
{
  /* Use a sorting network. */
  Short f = max(a,b);
  Short g = min(a,b);  /*  1 -(a)--*--(f)--*----(j)--*---------------------- */
  Short h = max(c,d);  /*          |       |         |                       */
  Short i = min(c,d);  /*          |       |         |                       */
  Short j = max(f,h);  /*  2 -(b)--*--(g)--+-*--(k)--*--(n)--*-------------- */
  Short k = max(g,e);  /*                  | |               |               */
  Short l = min(f,h);  /*                  | |               |               */
  Short m = min(g,e);  /*  3 -(c)--*--(h)--*-+--(l)--*--(o)--*--(q)--*--(s)- */
  Short n = min(j,k);  /*          |         |       |               |       */
  Short o = max(l,i);  /*          |         |       |               |       */
  Short p = min(l,i);  /*  4 -(d)--*--(i)----+-------*--(p)--*--(r)--*------ */
  Short q = min(n,o);  /*                    |               |               */
  Short r = max(p,m);  /*                    |               |               */
  Short s = max(q,r);  /*  5 -(e)------------*--(m)----------*-------------- */

  return s;
}


struct Work {
  SLong budget;
  jmp_buf jmp_buffer;
};

/* Compare lexicographically two rotations, R_i and R_j. Return 1 if R_i > R_j,
   0 if R_i < R_j. If both rotations are equal, this function does not return,
   longjmp() is called instead.
*/
static int
rot_cmp(
  Int i,
  Int j,
  const Short *Qd,
  Int n,
  struct Work *work)
{
  /* Calculate number of steps. */
  Int k = n / CMP_STEP;

  do {
    int rv;
    if ((rv = memcmp(Qd+i, Qd+j, sizeof(Short) * CMP_STEP)) != 0)
      return rv > 0;

    work->budget--;
    i = add(i, CMP_STEP, n);
    j = add(j, CMP_STEP, n);
  }
  while (k-- > 0);

  /* If we got to this point it means that the input string is periodic
     (with period < n). In this case we simply abandon quicksorting. */
  longjmp(work->jmp_buffer, 1);
}

static void
shell_sort(
  Int *SA,
  const Short *Qd,
  Int lo,
  Int hi,
  Int n,
  struct Work *work)
{
  static const Int incs[] = { 1, 4, 13, 40, 121, 364, 1093, 3280, 9841, 29524,
                              88573, 265720, -1 };  /* -1 is a sentinel. */

  Int i, j, h, hp;

  /* Less than 2 elements -- the range is already sorted. */
  if (hi-lo < 2)
    return;

  hp = 1;
  while (incs[hp] < hi-lo) hp++;

  while (hp > 0) {
    h = incs[--hp];

    i = lo + h;
    for (;;) {

#define ITER                                                    \
      {                                                         \
        Int t,u,v;                                              \
        if (i >= hi) break;                                     \
        v = SA[i];                                              \
        j = i;                                                  \
        u = j-h;                                                \
        while (rot_cmp((t = SA[u]), v, Qd, n, work) > 0) {      \
          SA[j] = t;                                            \
          j = u;                                                \
          if (u < lo+h) break;                                  \
          u -= h;                                               \
        }                                                       \
        SA[j] = v;                                              \
        i++;                                                    \
      }

      /* Unrolling really seems to help here... */
      ITER; ITER; ITER; ITER;
      ITER; ITER; ITER; ITER;
      ITER; ITER; ITER; ITER;
      ITER; ITER; ITER; ITER;

      if (unlikely(work->budget <= 0)) {
        longjmp(work->jmp_buffer, 1);
      }
    }
  }
}


#define quad(i) peeks(&Qd[SA[i]])

/* Perform fast, three-way partitioning. */
static void
partition(
  Int *SA,
  const Short *Qd,
  Int lo,
  Int hi,
  Int *midLo,
  Int *midHi,
  Short pivot)
{
  Int a, b, c, d;
  Short q;

  a = b = lo;
  c = d = hi;

  do {
    for (q = quad(b++); q <= pivot; q = quad(b++)) {
      if (q == pivot)
        swap(SA, b-1, a++);
      if (b >= c)
        goto stop;
    }

    for (q = quad(--c); q >= pivot; q = quad(--c)) {
      if (q == pivot)
        swap(SA, c, --d);
      if (b > c)
        goto stop;
    }

    swap(SA, b-1, c);
  } while (b < c);

stop:
  assert(b == c || b-1 == c);
  assert(a-lo + hi-d >= 1);
  b = c;

  *midLo = lo + (b-a);
  a = min(a, *midLo);
  while (lo < a)
    swap(SA, lo++, --b);

  *midHi = hi - (d-c);
  d = max(d, *midHi);
  while (hi > d)
    swap(SA, --hi, c++);
}


#define BU(ss,sb) CF[((ss) << 8) + (sb)]


/* Step Q6a. Quicksort a small bucket. */
static void
quick_sort(
  Int *SA,
  const Short *Q,
  Int lo,
  Int hi,
  Int n,
  struct Work *work)
{
  Int sp;    /* stack pointer */
  Int midLo, midHi;
  Long v1, v2, v3;
  Long min123, median, max123;
  Short pivot;

  const Short *Qd = Q + INITIAL_DEPTH;

  Long vec;
  Long stack[STACK_SIZE];

  sp = 0;

  for (;;) {
    /* After reaching certain minimal width threshold we switch to Shell sort.
       Also stop partitioning the block after reaching certain depth
       (otherwise it could happen that all rotations within the range are
       equal and partitioning loops forever). */
    while (hi-lo < SHELL_THRESH || Qd - Q > MAX_DEPTH) {
      shell_sort(SA, Qd, lo, hi, n, work);
      if (sp == 0)
        return;
      vec = stack[--sp];
      Qd = Q + (Byte)vec;
      vec >>= 8;
      lo = vec;
      hi = vec + (vec >> 32);
    }

    /* Choose the partitioning element. */
    pivot = med5(quad( lo          ),   /* (4 lo + 0 hi) / 4 */
                 quad( (3*lo+hi)/4 ),   /* (3 lo + 1 hi) / 4 */
                 quad( (lo+hi)/2   ),   /* (2 lo + 2 hi) / 4 */
                 quad( (lo+3*hi)/4 ),   /* (1 lo + 3 hi) / 4 */
                 quad( hi-1        ));  /* (0 lo + 4 hi) / 4 */

    /* Partition the array. */
    partition(SA, Qd, lo, hi, &midLo, &midHi, pivot);

    /* To avoid worst-case linear space complexity, the subranges are sorted
       from the narrowest to the widest. Quicksort parameters are packed
       into single words so they can be ordered easier. */
    v1 = ((Long)(midLo - lo   ) << 40) | (   lo << 8) | (Qd - Q);
    v2 = ((Long)(midHi - midLo) << 40) | (midLo << 8) | (Qd - Q + 1);
    v3 = ((Long)(   hi - midHi) << 40) | (midHi << 8) | (Qd - Q);

    /* Sort 3 integers, without branching. */
    min123 = min(min(v1, v2), v3);
    max123 = max(max(v1, v2), v3);
    median = v1 ^ v2 ^ v3 ^ max123 ^ min123;
    assert(max123 > (Long)2<<40);

    assert(sp <= STACK_SIZE-2);
    stack[sp++] = max123;
    stack[sp++] = median;

    if (min123 < (Long)2<<40) { min123 = median; sp--; }

    Qd = Q + (Byte)min123;
    min123 >>= 8;
    lo = min123;
    hi = min123 + (min123 >> 32);
  }
}


/* Steps Q1, Q2, Q3 and Q4 combined. */
static void
bucket_sort(
  Int *SA,
  const Byte *T,
  Int *CF,
  Short *Q,
  Int n)
{
  Int i;
  Short u;

  /* Determine the size of each bucket. */
  bzero(CF, 65537 * sizeof(Int));
  u = T[n-1];
  for (i = 0; i < n/4*4; i += 4) {
    Int v = peekl(&T[i]);
    u = (u << 8) + (v >> 24); CF[u]++;
    u = v >> 16;              CF[u]++;
    u = v >> 8;               CF[u]++;
    u = v;                    CF[u]++;
  }
  for (; i < n; i++) {
    u = (u << 8) + T[i];
    CF[u]++;
  }

  /* Transform counts into indices. */
  for (i = 0; i < 65536; i++)
    CF[i+1] += CF[i];
  assert(CF[65536] == n);

  /* Sort indices. */
  u = T[0] << 8;
  for (i = n; i & 3; i--) {
    u = (T[i-1] << 8) + (u >> 8);
    SA[--CF[u]] = i-1;
    pokes(&Q[i-1], u);
  }
  for (; i > 0; i -= 4) {
    Int v = peekl(&T[i-4]);
    u = (v << 8) + (u >> 8); SA[--CF[u]] = i-1; pokes(&Q[i-1], u);
    u = v;                   SA[--CF[u]] = i-2; pokes(&Q[i-2], u);
    u = v >> 8;              SA[--CF[u]] = i-3; pokes(&Q[i-3], u);
    u = v >> 16;             SA[--CF[u]] = i-4; pokes(&Q[i-4], u);
  }

  /* Create quadrants in the overshoot area. */
  for (i = 0; i < OVERSHOOT; i++)
    Q[n+i] = Q[i];
}


/* Once the big bucket `ss' is sorted, we can induce ordering of all small
   buckets [t,ss] in one step.

   If two arbitrary rotations R_i and R_j belong to the same big bucket
   (start with the same character) then the relative order of R_i and R_j
   is the same as order of R_{i+1} and R_{j+1}.
*/
static void
induce_orderings(
  Int *SA,
  const Short *Q,
  const Int *CF,
  Int *BS,
  Int ss,
  Int n)
{
  Byte c1;
  Int copy[256];
  Int i, j, k;

  assert(BS[ss] == 0);
  BS[ss] = BU(ss,ss+1) - BU(ss,ss);

  /* Step Q6b. */
  for (i = 0; i < 256; i++)
    copy[i] = BU(i,ss);
  for (i = BU(ss,0); i < copy[ss]; i++) {
    k = sub(SA[i], 1, n);
    c1 = peekb(&Q[k]);  /* FIXME: a lot of cache misses here :( */
    if (BS[c1] > 0)
      SA[copy[c1]++] = k;
  }
  assert(i == copy[ss]);
  j = i;

  /* Step Q6c. */
  for (i = 0; i < 256; i++)
    copy[i] = BU(i,ss+1);
  for (i = BU(ss,256); i > copy[ss]; i--) {
    k = sub(SA[i-1], 1, n);
    c1 = peekb(&Q[k]);  /* FIXME: a lot of cache misses here :( */
    if (BS[c1] > 0)
      SA[--copy[c1]] = k;
  }
  assert(i == copy[ss]);

  assert(i == j);
}


/* Step Q7. Refine quadrants.

   Once the small bucket [bb,sb] is done we can update quadrant descriptors
   for rotations that belong to that bucket.
*/
static void
update_quadrants(
  const Int *SA,
  Short *Q,
  Int lo,
  Int hi,
  Int n)
{
  Int j, s;
  Byte *Q3 = (Byte *)Q + 3;

  /* Mark the fact that [ss,sb] is done. */
  if (hi-lo < 2)
    return;

  /* Compute the appropriate s. */
  s = 0;
  while (((hi-lo-1) >> s) >= 256)
    s++;

  for (j = lo; j < hi; j++)
    Q3[2*SA[j]] = (j-lo) >> s;
  Q[0] = Q[n];
  for (j = 1; j < OVERSHOOT; j++)
    Q[j+n] = Q[j];
}


/* Algorithm Q. */
static Int
copy_cache(
  Int *SA,
  Int *CF,
  Short *Q,
  Int n,
  struct Work *work)
{
  Int m;  /* number of already sorted big buckets */
  Int j, k;
  Byte ss, sb;
  Int lo, hi;
  Byte ZN[256];
  Int idx;
  Int BS[256];
  Int nQS;  /* number of rotations ordered with quicksort */

  nQS = 0;
  idx = 0;

  /* Select non-empty big buckets. */
  m = 256;
  for (j = 0; j < 256; j++) {
    BS[j] = BU(j,256) - BU(j,j+1) + BU(j,j) - BU(j,0);
    if (BS[j] > 0)
      ZN[--m] = j;
  }

  /* Step Q5. Process big buckets. */
  for (; m < 256; m++) {
    Int h;

    /* Select the smallest, not yet sorted big bucket. */
    k = m;
    for (j = k+1; j < 256; j++) {
      if (BS[ZN[j]] < BS[ZN[k]])
        k = j;
    }
    ss = ZN[k];
    ZN[k] = ZN[m];
    nQS += BS[ss];

    /* Define small bucket order within the big bucket. */
    for (h = 121; h > 0; h /= 3) {
      for (j = m+1+h; j < 256; j++) {
        Int sv;
        sb = ZN[j];
        sv = BU(ss,sb+1) - BU(ss,sb);
        for (k = j; k > m+h && BU(ss,ZN[k-h]+1) - BU(ss,ZN[k-h]) > sv; k -= h)
          ZN[k] = ZN[k-h];
        ZN[k] = sb;
      }
    }

    /* Step Q6a. Quicksort small buckets. */
    for (j = m+1; j < 256; j++) {
      sb = ZN[j];
      lo = BU(ss,sb);
      hi = BU(ss,sb+1);
      quick_sort(SA, Q, lo, hi, n, work);
      update_quadrants(SA, Q, lo, hi, n);
      BS[ss] -= hi-lo;
    }

    /* Steps Q6b, Q6c. */
    induce_orderings(SA, Q, CF, BS, ss, n);

    /* Spep Q7. Update quadrants for all small buckets [j,ss] */
    if (m < 256) {
      for (j = 0; j < 256; j++) {
        if (BS[j] > 0) {
          lo = BU(j,ss);
          hi = BU(j,ss+1);
          update_quadrants(SA, Q, hi, lo, n);
          BS[j] -= hi-lo;
        }
      }
      assert(BS[ss] == 0);
    }

    /* Construct BWT transformation. */
    for (j = BU(ss,0); j < BU(ss,256); j++) {
      k = SA[j];
      if (k == 0) idx = j;
      k = sub(k, 1, n);
      SA[j] = peekb(&Q[k]);
    }
  }

  assert(nQS > 0 && nQS <= n/2);
  assert(idx < n);
  return idx;
}


/* Compute the BWT (Burrows-Wheeler Transform) of the input string. */
void
YBpriv_block_sort(YBenc_t *s)
{
  Int i;
  Int n = s->nblock;
  Byte *T = s->block;
  Short *Q = (void *)s->mtfv;
  Int *CF;
  Int *SA;

  struct Work work;

  /* Handle the trivial case. */
  if (n == 1) {
    s->bwt_idx = 0;
    return;
  }

  /* Use induced sort if the user explictly askes for it. Small blocks are
     better handled by induced sort because of high bucket sort overhead. */
  if (n <= 16384 || s->shallow_factor == 0)
    return YBpriv_sais(s);

  /* Sort buckets. */
  SA = s->bwt;
  CF = xmalloc(65537 * sizeof(Int));
  bucket_sort(SA, T, CF, Q, n);

  /* Set up an exception handler. Control is transfered here if quicksort runs
     into difficulties and we should fallback to induced sort. */
  if (setjmp(work.jmp_buffer)) {
    free(CF);
    for (i = 0; i < n; i++)
      T[i] = peekb(&Q[i]);
    return YBpriv_sais(s);
  }

  work.budget = (SLong)n * s->shallow_factor / CMP_STEP;
  s->bwt_idx = copy_cache(SA, CF, Q, n, &work);
  free(CF);
}

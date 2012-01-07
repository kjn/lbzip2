/*-
  sais.c -- suffix array induced sort

  Copyright (C) 2012 Mikolaj Izdebski

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
  Copyright (c) 2010 Yuta Mori. All Rights Reserved.

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation
  files (the "Software"), to deal in the Software without
  restriction, including without limitation the rights to use,
  copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following
  conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>     /* free() */

#include "xalloc.h"     /* xmalloc() */

#include "encode.h"


#define MINBUCKETSIZE 256

typedef SInt saidx_t;

#define chr(_a) (cs == sizeof(saidx_t) ? \
    ((const saidx_t *)T)[(_a)] : ((const unsigned char *)T)[(_a)])

/* find the start or end of each bucket */
static void
getCounts(const void *T, saidx_t *C, saidx_t n,
          saidx_t k, int cs)
{
  saidx_t i;
  for(i = 0; i < k; ++i) { C[i] = 0; }
  for(i = 0; i < n; ++i) { ++C[chr(i)]; }
}

static void
getBuckets(const saidx_t *C, saidx_t *B, saidx_t k,
           int end)
{
  saidx_t i, sum = 0;
  if(end) { for(i = 0; i < k; ++i) { sum += C[i]; B[i] = sum; } }
  else { for(i = 0; i < k; ++i) { sum += C[i]; B[i] = sum - C[i]; } }
}

/* sort all type LMS suffixes */
static void
LMSsort1(const void *T, saidx_t *SA, saidx_t *C,
         saidx_t *B, saidx_t n, saidx_t k, int cs)
{
  saidx_t *b, i, p0, p1;
  saidx_t c0, c1;
  /* compute SAl */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 0); /* find starts of buckets */
  for(i = 0, b = SA + B[c1 = 0]; i < n; ++i) {
    p1 = SA[i];
    if(0 <= p1) {
      assert(p1 < n);
      p0 = (p1 != 0) ? (p1 - 1) : (n - 1);
      assert(chr(p0) >= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      assert(i < (b - SA));
      *b++ = (((p0 != 0) ? chr(p0 - 1) : chr(n - 1)) < c1) ? ~p0 : p0;
      SA[i] = ~n;
    } else {
      SA[i] = ~p1;
    }
  }
  /* compute SAs */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 1); /* find ends of buckets */
  for(i = n - 1, b = SA + B[c1 = 0]; 0 <= i; --i) {
    if(0 <= (p1 = SA[i])) {
      assert(p1 < n);
      p0 = (p1 != 0) ? (p1 - 1) : (n - 1);
      assert(chr(p0) <= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      assert((b - 1 - SA) < i);
      *--b = (((p0 != 0) ? chr(p0 - 1) : chr(n - 1)) > c1) ? ~p0 : p0;
      SA[i] = n;
    } else {
      SA[i] = ~p1;
    }
  }
}

static saidx_t
LMSpostproc1(const void *T, saidx_t *SA, saidx_t n,
             saidx_t m, unsigned int lasttype, int cs)
{
  saidx_t i, j, t, p, q, plen, qlen, flen, len, name;
  saidx_t c0, c1;
  int diff;

  /* compact all the sorted substrings into the first m items of SA
      2*m must be not larger than n (proveable) */
  assert(0 < n);
  for(i = 0; (p = SA[i]) < n; ++i) { SA[i] = p; assert((i + 1) < n); }
  if(i < m) {
    for(j = i, ++i;; ++i) {
      assert(i < n);
      if((p = SA[i]) < n) {
        SA[j++] = p; SA[i] = n;
        if(j == m) { break; }
      }
    }
  }

  /* store the length of all substrings */
  if(lasttype & 1) { i = n; j = n; c0 = chr(0); }
  else {
    i = n - 1; j = n; c0 = chr(n - 1);
    do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
  }
  for(; 0 <= i;) {
    do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) <= c1));
    if(0 <= i) {
      assert((m + ((i + 1) >> 1)) < n);
      SA[m + ((i + 1) >> 1)] = j - i; j = i + 1;
      do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
    } else if(lasttype == 0) {
      assert((m + ((i + 1) >> 1)) < n);
      SA[m + ((i + 1) >> 1)] = j - i; j = i + 1;
    }
  }
  flen = j;

  /* find the lexicographic names of all substrings */
  for(i = 0, name = -1, q = n, qlen = -1; i < m; ++i) {
    p = SA[i], plen = SA[m + (p >> 1)], diff = 1;
    if(n < (p + plen)) { plen += flen; }
    if(plen == qlen) {
      if(n < (p + plen)) {
        len = n - p;
        for(j = 0; (j < len) && (chr(p + j) == chr(q + j)); ++j) { }
        if(j == len) {
          t = -j;
          for(; (j < plen) && (chr(t + j) == chr(q + j)); ++j) { }
          if(j == plen) { diff = 0; }
        }
      } else if(n < (q + qlen)) {
        len = n - q;
        for(j = 0; (j < len) && (chr(p + j) == chr(q + j)); ++j) { }
        if(j == len) {
          t = -j;
          for(; (j < plen) && (chr(p + j) == chr(t + j)); ++j) { }
          if(j == plen) { diff = 0; }
        }
      } else {
        for(j = 0; (j < plen) && (chr(p + j) == chr(q + j)); ++j) { }
        if(j == plen) { diff = 0; }
      }
    }
    if(diff != 0) { ++name, q = p, qlen = plen; }
    SA[m + (p >> 1)] = name;
  }

  return name + 1;
}

static void
induceSA(const void *T, saidx_t *SA, saidx_t *C,
         saidx_t *B, saidx_t n, saidx_t k, int cs)
{
  saidx_t *b, i, p0, p1;
  saidx_t c0, c1;
  /* compute SAl */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 0); /* find starts of buckets */
  for(i = 0, b = SA + B[c1 = 0]; i < n; ++i) {
    p1 = SA[i], SA[i] = ~p1;
    if(0 <= p1) {
      assert(p1 < n);
      p0 = (p1 != 0) ? (p1 - 1) : (n - 1);
      assert(chr(p0) >= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      assert(i < (b - SA));
      *b++ = (((p0 != 0) ? chr(p0 - 1) : chr(n - 1)) < c1) ? ~p0 : p0;
    }
  }
  /* compute SAs */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 1); /* find ends of buckets */
  for(i = n - 1, b = SA + B[c1 = 0]; 0 <= i; --i) {
    if(0 <= (p1 = SA[i])) {
      assert(p1 < n);
      p0 = (p1 != 0) ? (p1 - 1) : (n - 1);
      assert(chr(p0) <= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      assert((b - 1 - SA) < i);
      *--b = (((p0 != 0) ? chr(p0 - 1) : chr(n - 1)) > c1) ? ~p0 : p0;
    } else {
      SA[i] = ~p1;
    }
  }
}

static saidx_t
computeBWT(const void *T, saidx_t *SA, saidx_t *C,
           saidx_t *B, saidx_t n, saidx_t k, int cs)
{
  saidx_t *b, i, p0, p1, pidx = -2;
  saidx_t c0, c1;
  /* compute SAl */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 0); /* find starts of buckets */
  for(i = 0, b = SA + B[c1 = 0]; i < n; ++i) {
    p1 = SA[i], SA[i] = ~p1;
    if(0 <= p1) {
      assert(p1 < n);
      if(p1 != 0) { p0 = p1 - 1; }
      else { p0 = n - 1; pidx = i; }
      assert(chr(p0) >= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      SA[i] = ~((saidx_t)c1);
      assert(i < (b - SA));
      *b++ = (((p0 != 0) ? chr(p0 - 1) : chr(n - 1)) < c1) ? ~p0 : p0;
    }
  }
  /* compute SAs */
  if(C == B) { getCounts(T, C, n, k, cs); }
  getBuckets(C, B, k, 1); /* find ends of buckets */
  for(i = n - 1, b = SA + B[c1 = 0]; 0 <= i; --i) {
    if(0 <= (p1 = SA[i])) {
      assert(p1 < n);
      if(p1 != 0) { p0 = p1 - 1; }
      else { p0 = n - 1; pidx = i; }
      assert(chr(p0) <= chr(p1));
      if((c0 = chr(p0)) != c1) { B[c1] = b - SA; b = SA + B[c1 = c0]; }
      SA[i] = (saidx_t)c1;
      assert((b - 1 - SA) < i);
      if(p0 != 0) { c0 = chr(p0 - 1); }
      else { c0 = chr(n - 1); pidx = b - 1 - SA; }
      *--b = (c0 > c1) ? ~((saidx_t)c0) : p0;
    } else {
      SA[i] = ~p1;
    }
  }
  assert(0 <= pidx);
  return pidx;
}

/* find the cyclic suffix array SA of T[0..n-1] in {0..k-1}^n */
static saidx_t
csais_main(const void *T, saidx_t *SA, saidx_t *FA,
           saidx_t fs1, saidx_t fs2, saidx_t n,
           saidx_t k, int cs, int isbwt)
{
  saidx_t *C, *B, *RA;
  saidx_t i, j, m, p, q, name, newfs, pidx = 0;
  saidx_t c0, c1;
  unsigned int flags;
  unsigned int lasttype;

  assert((T != NULL) && (SA != NULL));
  assert((0 <= fs1) && (0 <= fs2) && (0 < n) && (1 <= k));

  if(k <= fs2) {
    C = FA + (fs2 - k); fs2 -= k;
    if(k <= fs2) { B = C - k; flags = 1 | 8; }
    else if(k <= fs1) { B = SA + n + (fs1 - k); flags = 1 | 16; }
    else if(k <= (MINBUCKETSIZE * 2)) {
      B = xmalloc(k * sizeof(saidx_t));
      flags = 1 | 32;
    }
    else { B = C; flags = 1 | 64 | 128; }
  } else if(k <= fs1) {
    C = SA + n + (fs1 - k);
    if(k <= (fs1 - k)) { B = C - k; flags = 2 | 16; }
    else if(k <= (MINBUCKETSIZE * 2)) {
      B = xmalloc(k * sizeof(saidx_t));
      flags = 2 | 32;
    }
    else { B = C; flags = 2 | 64 | 128; }
  } else if(k <= MINBUCKETSIZE) {
    C = xmalloc(k * sizeof(saidx_t));
    B = xmalloc(k * sizeof(saidx_t));
    flags = 32 | 256;
  } else {
    C = B = xmalloc(k * sizeof(saidx_t));
    flags = 4 | 64 | 128;
  }

  /* stage 1: reduce the problem by at least 1/2
     sort all the LMS-substrings */
  getCounts(T, C, n, k, cs); getBuckets(C, B, k, 1); /* find ends of buckets */
  for(i = 0; i < n; ++i) { SA[i] = -1; }
  if(chr(n - 1) != chr(0)) { lasttype = (chr(n - 1) < chr(0)); }
  else {
    for(i = 1, lasttype = 0; i < n; ++i) {
      if(chr(i - 1) != chr(i)) { lasttype = 2 | (chr(i - 1) < chr(i)); break; }
    }
  }
  m = 0;
  if(lasttype & 1) { i = n; c0 = chr(0); }
  else {
    i = n - 1; c0 = chr(n - 1);
    do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
  }
  for(; 0 <= i;) {
    do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) <= c1));
    if(0 <= i) {
      SA[--B[c1]] = i + 1; ++m;
      do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
    } else if(lasttype == 0) {
      SA[--B[c1]] = i + 1; ++m;
    }
  }
  assert((m + ((n - 1) >> 1)) < n);
  if(m == 0) {
    if(isbwt == 0) { for(i = 0; i < n; ++i) { SA[i] = i; } }
    else { for(i = 0; i < n; ++i) { SA[i] = chr(i); } }
    if(flags & (4 | 256)) { free(C); }
    if(flags & 32) { free(B); }
    return 0;
  }
  LMSsort1(T, SA, C, B, n, k, cs);
  name = LMSpostproc1(T, SA, n, m, lasttype, cs);

  /* stage 2: solve the reduced problem
     recurse if names are not yet unique */
  if(name < m) {
    if(flags &  4) { free(C); }
    if(flags & 32) { free(B); }
    newfs = (n + fs1) - (m * 2);
    if(flags & 2) {
      if((k + name) <= newfs) { newfs -= k; }
      else { flags |= 128; }
    }
    assert((n >> 1) <= (newfs + m));
    RA = SA + m + newfs;
    for(i = m + ((n - 1) >> 1), j = m - 1; m <= i; --i) {
      if(SA[i] < n) { assert(0 <= j); RA[j--] = SA[i]; }
    }
    pidx = csais_main(RA, SA, FA, newfs, fs2, m, name, sizeof(saidx_t), 0);
    assert(pidx == 0);
    j = m - 1;
    if(lasttype & 1) { i = n; c0 = chr(0); }
    else {
      i = n - 1; c0 = chr(n - 1);
      do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
    }
    for(; 0 <= i;) {
      do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) <= c1));
      if(0 <= i) {
        RA[j--] = i + 1;
        do { c1 = c0; } while((0 <= --i) && ((c0 = chr(i)) >= c1));
      } else if(lasttype == 0) {
        RA[j--] = i + 1;
      }
    }

    for(i = 0; i < m; ++i) { SA[i] = RA[SA[i]]; }
    if(flags &  4) { C = xmalloc(k * sizeof(saidx_t)); }
    if(flags & 32) { B = xmalloc(k * sizeof(saidx_t)); }
    if(flags & 64) { B = C; }
  }

  /* stage 3: induce the result for the original problem */
  if(flags & 128) { getCounts(T, C, n, k, cs); }
  /* put all left-most S characters into their buckets */
  getBuckets(C, B, k, 1); /* find ends of buckets */
  i = m - 1, j = n, p = SA[m - 1], c1 = chr(p);
  do {
    q = B[c0 = c1];
    while(q < j) { SA[--j] = -1; }
    do {
      SA[--j] = p;
      if(--i < 0) { break; }
      p = SA[i];
    } while((c1 = chr(p)) == c0);
  } while(0 <= i);
  while(0 < j) { SA[--j] = -1; }
  if(isbwt == 0) { induceSA(T, SA, C, B, n, k, cs); }
  else { pidx = computeBWT(T, SA, C, B, n, k, cs); }
  if(flags & (4 | 256)) { free(C); }
  if(flags & 32) { free(B); }

  return pidx;
}

/* Interface to yambi. */
void
YBpriv_sais(YBenc_t *s)
{
  Byte *T = s->block;
  SInt *SA = (void *)s->bwt;
  Int n = s->nblock;
  Int fi = (n + sizeof(saidx_t)) / sizeof(saidx_t);
  Int fs = s->max_block_size * sizeof(Short) / sizeof(saidx_t);

  s->bwt_idx = csais_main(T, SA, (saidx_t *)T + fi, 0, fs-fi, n, 256, 1, 1);
  assert(s->bwt_idx < n);
}

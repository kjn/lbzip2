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

/*

Alistair Moffat, Jyrki Katajainen
In-Place Calculation of Minimum-Redundancy Codes
http://www.diku.dk/~jyrki/Paper/WADS95.pdf

Lawrence Larmore, Daniel Hirschberg
A Fast Algorithm for Optimal Length-Limited Huffman Codes
www.cs.unlv.edu/~larmore/Research/larHir90.pdf
(package-merge algorithm)

Donald Knuth
The Art of Computer Programming, volume 3: Sorting and Searching

*/

#include <strings.h>  /* bzero() */

#include "encode.h"


#if 0
# include <stdio.h>
# define Trace(x) fprintf x
#else
# define Trace(x)
#endif

/*

n - input alphabet size (i.e. number of distinct input symbols)
    2+1 <= n <= 2+255+1

P - sorted order permutation

*/


/* Sort array of unsigned long integers in descending order.
   This is an implementation of diminishing increment sort (aka Shell sort)
   from The Art of Computer Programming by D. Knuth (vol. 3, chap. 5).
 */
static void
shell_sort(
  Long P[],  /* sorted order */
  Int n)     /* input alphabet size */
{
  static const SInt H[] = { 1, 4, 10, 23, 57, 132 };

  /* Step D1. */
  SInt s = sizeof(H) / sizeof(*H);
  while (s--)
  {
    /* Step D2. */
    SInt h = H[s];

    Int j;
    for (j = h; j < n; j++)
    {
      /* Step D3. */
      SInt i = j - h;
      Long v = P[j];

      /* Step D4. */
      while (v > P[i])
      {
        /* Step D5. */
        P[i+h] = P[i];
        i = i - h;
        if (i < 0)
          break;
      }

      /* Step D6. */
      P[i+h] = v;
    }
  }
}

#ifndef PACKAGE_MERGE

/* Build a prefix-free tree.  Because the source alphabet is already sorted,
   we need not to maintain a priority queue -- two normal FIFO queues
   (one for leaves and one for internal nodes) will suffice.
 */
static void
build_tree(
  Int T[],
  Long P[],
  SInt n)
{
  SInt r;  /* index of next tree in the queue */
  SInt s;  /* index of next singleton leaf (negative if no more left) */
  SInt t;  /**/
  Long w1, w2, w;

  r = n-1;
  s = n-1;  /* Start with the last singleton tree. */
  t = n-1;

  while (t > 0)
  {
    /* If it's not the first iteration then r < t and  */
    assert(t == n-1 || (r > t && s < t));

    /* Select the first node to be merged. */
    if (s < 0 || P[r] < P[s])
    {
      /* Select an internal node. */
      T[r] = t;
      w1 = P[r--];
    }
    else
      /* Select a singleton leaf node. */
      w1 = P[s--];

    /* Select the second node to be merged. */
    if (s < 0 || (r > t && P[r] < P[s]))
    {
      T[r] = t;
      w2 = P[r--];
    }
    else
      w2 = P[s--];

    w = (w1 + w2) & ~(Long)0xFF00FFFF;
    w1 &= 0xFF000000; w2 &= 0xFF000000;
    if (w2 > w1) w1 = w2;
    w2 = P[t] & 0xFFFF;
    P[t--] = w + w1 + 0x01000000 + w2;
  }
}


/* ===========================================================================
 * Compute counts from given Huffman tree.  The tree itself is clobbered.
 */
static void
compute_depths(
  Int C[],
  Int T[],
  Int n)
{
  Int a;     /* total number of nodes at current level */
  Int u;     /* number of internal nodes */
  Int t;     /* current tree node */
  Int d;     /* current node depth */

  T[1] = 0;  /* The root always has depth of 0. */
  C[0] = 0;  /* there are never zero-length codes in bzip2. */
  t = 2;     /* The root is done, advance to the next node (of index 2). */
  d = 1;     /* The root was the last node at depth 0, go deeper. */
  a = 2;     /* At depth of 1 there are always exactly 2 nodes. */

  /* Repeat while we have more nodes. */
  while (d < 32)
  {
    u = 0;  /* So far we haven't seen any internal nodes at this level. */

    while (t < n && T[T[t]] + 1 == d)
    {
      assert(a > u);
      u++;
      T[t++] = d;   /* overwrite parent pointer with node depth */
    }

    C[d] = a - u;
    d++;
    a = u << 1;
  }

  assert(a == 0);
}

#endif /*!PACKAGE_MERGE*/


/* The following is an implementation of the Package-Merge algorithm for
   finding an optimal length-limited prefix-free codeset, as described in [1].

*/

/* This structure holds packages of coins.  Single coins are treated as
   singleton packages, so they can be held in this structure aswell. */
struct Pkg {
  Long weight;
  Long pack[3];
};

/* Create a singleton package consisting of a single coin of specified weight
   and width equal to 2^-depth. */
static void
set_coin(struct Pkg *out, Long weight, int depth)
{
  int idx, shft;

  assert(depth > 0);

  out->weight = weight;
  out->pack[0] = 0;
  out->pack[1] = 0;
  out->pack[2] = 0;

  depth--;
  idx = depth / 7;
  shft = depth % 7 * 9;
  out->pack[idx] = (Long)1 << shft;
}

/* Merge two packages. */
static void
package(struct Pkg *out, struct Pkg *left, struct Pkg *right)
{
  out->weight = left->weight + right->weight;
  out->pack[0] = left->pack[0] + right->pack[0];
  out->pack[1] = left->pack[1] + right->pack[1];
  out->pack[2] = left->pack[2] + right->pack[2];
}

/* O(n log(n)) time
   O(n) memory
 */
static void
package_merge(Int *C, Long *Pr, Int n)
{
  /* Max. alphabet size in bzip2 is 258.  So why the following two arrays
     have 256 entries each?

     The maximum amount memory that   ___ inf         -k
     package merge algoirithm may     \       floor( 2   * max_alpha_size )
     require is expressed by a sum:   /__ k=1
  */
  struct Pkg arr1[256];
  struct Pkg arr2[256];
  struct Pkg t1;
  struct Pkg t2;

  Int x;

  Long s_c0;
  Long s_c1;
  Long s_c2;

  Int i;
  Int d;

  unsigned jP;
  unsigned szP;
  unsigned szL;
  struct Pkg *P;
  struct Pkg *L;
  struct Pkg *T;

  Int u, v;

  P = arr1;
  L = arr2;
  szP = 0;

  for (d = MAX_CODE_LENGTH; d > 0; d--)
  {
    i = 0;
    jP = 0;
    szL = 0;

    while ((n-i) + (szP-jP) >= 2)
    {
      if (jP == szP || (i < n && Pr[n-1-i] < P[jP].weight))
      {
        assert(i < n);
        set_coin(&t1, Pr[n-1-i], d);
        i++;
      }
      else
      {
        assert(jP < szP);
        t1 = P[jP++];
      }

      if (jP == szP || (i < n && Pr[n-1-i] < P[jP].weight))
      {
        assert(i < n);
        set_coin(&t2, Pr[n-1-i], d);
        i++;
      }
      else
      {
        assert(jP < szP);
        t2 = P[jP++];
      }

      package(&L[szL++], &t1, &t2);
    }

    T = P;
    P = L;
    L = T;
    szP = szL;
    assert(szP > 0);
    assert(szP < n);
  }

  /* Width of a full binary tree with n leaves is equal to n-1. */
  x = n-1;

  s_c0 = 0;
  s_c1 = 0;
  s_c2 = 0;

  while (x > 0)
  {
    jP = 0;

    if (x & 1)
    {
      s_c0 += P[jP].pack[0];
      s_c1 += P[jP].pack[1];
      s_c2 += P[jP].pack[2];
      jP++;
    }
    x >>= 1;

    szL = 0;
    while (szP-jP >= 2)
    {
      t1 = P[jP++];
      t2 = P[jP++];
      assert(szL < jP);
      package(&P[szL++], &t1, &t2);
    }

    szP = szL;
    assert((x == 0) == (szP == 0));
    assert(szP < n);
  }

  /* We have found an optimal solution, it's packed in s_c vector.
     Now we need to unpack it. */
  u = (s_c2 >> 5*9) & 0x1FF; C[20] = u;
  v = (s_c2 >> 4*9) & 0x1FF; C[19] = v - u;
  u = (s_c2 >> 3*9) & 0x1FF; C[18] = u - v;
  v = (s_c2 >> 2*9) & 0x1FF; C[17] = v - u;
  u = (s_c2 >>   9) & 0x1FF; C[16] = u - v;
  v = (s_c2       ) & 0x1FF; C[15] = v - u;
  u = (s_c1 >> 6*9) & 0x1FF; C[14] = u - v;
  v = (s_c1 >> 5*9) & 0x1FF; C[13] = v - u;
  u = (s_c1 >> 4*9) & 0x1FF; C[12] = u - v;
  v = (s_c1 >> 3*9) & 0x1FF; C[11] = v - u;
  u = (s_c1 >> 2*9) & 0x1FF; C[10] = u - v;
  v = (s_c1 >>   9) & 0x1FF; C[ 9] = v - u;
  u = (s_c1       ) & 0x1FF; C[ 8] = u - v;
  v = (s_c0 >> 6*9) & 0x1FF; C[ 7] = v - u;
  u = (s_c0 >> 5*9) & 0x1FF; C[ 6] = u - v;
  v = (s_c0 >> 4*9) & 0x1FF; C[ 5] = v - u;
  u = (s_c0 >> 3*9) & 0x1FF; C[ 4] = u - v;
  v = (s_c0 >> 2*9) & 0x1FF; C[ 3] = v - u;
  u = (s_c0 >>   9) & 0x1FF; C[ 2] = u - v;
  v = (s_c0       ) & 0x1FF; C[ 1] = v - u;
}




/* ===========================================================================
 */
static void
make_code_lengths(
  Int C[],
  Byte L[],
  Int P0[],
  Int n)  /* alphabet size */
{
  Int i;
  SInt k;
  SInt d;
  SInt c = 0;
  Long P[MAX_ALPHA_SIZE];
  Int V[MAX_ALPHA_SIZE];

  assert(n >= MIN_ALPHA_SIZE);
  assert(n <= MAX_ALPHA_SIZE);

  /* Label weights with sequence numbers.
     Labelling has two main purposes: firstly it allows to sort pairs of
     weight and sequence number more easily; secondly: the package-merge
     algorithm requires strict monotonicity of weights and putting
     an uniquie value in lower bits provides that. */
  for (i = 0; i < n; i++)
  {
    /*
      FFFFFFFF00000000 - symbol ferequencuy
      00000000FF000000 - node depth
      0000000000FF0000 - initially one
      000000000000FFFF - symbol
    */
    if (P0[i] == 0)
      P[i] = ((Long)1 << 32) | 0x10000 | (MAX_ALPHA_SIZE-i);
    else
      P[i] = ((Long)P0[i] << 32) | 0x10000 | (MAX_ALPHA_SIZE-i);
  }

  /* Sort weights and sequence numbers toogether. */
  shell_sort(P, n);

#ifdef PACKAGE_MERGE
  package_merge(C, P, n);

  C[0] = 0;
#else
  /* Build a Huffman tree. */
  build_tree(V, P, n);

  /* Traverse the Huffman tree and generate counts. */
  compute_depths(C, V, n);

  k = 0;
  for (d = MAX_CODE_LENGTH+1; d < 32; d++)
    k |= C[d];

  /* If any code exceeds length limit, fallback to package-merge algorithm. */
  if (k != 0)
  {
    for (i = 0; i < n; i++)
    {
      if (P0[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] == 0)
        P[i] = ((Long)1 << 32) | 0x10000 | (P[i] & 0xFFFF);
      else
        P[i] = ((Long)P0[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] << 32)
          | 0x10000 | (P[i] & 0xFFFF);
    }

    package_merge(C, P, n);
  }
#endif

  /* Generate code lengths and transform counts into base codes. */
  i = 0;
  for (d = 0; d <= MAX_CODE_LENGTH; d++)
  {
    k = C[d];

    C[d] = c;
    c = ((c + k) << 1);

    while (k != 0)
    {
      assert(i < n);
      L[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] = d;
      i++;
      k--;
    }
  }

  assert(i == n);
}


static void
assign_codes(
  Int C[],
  Int L[],
  Byte B[],
  Int n)
{
  /* Assign prefix-free codes. */
  Int i;
  for (i = 0; i < n; i++)
    L[i] = C[B[i]]++;
}


/* TODO: cleanup */
static void
generate_initial_trees(YBenc_t *s, int nmtf, int alphaSize, Int nGroups)
{
  Int nPart;
  SInt remF, tFreq, aFreq;
  SInt gs, ge, v;

  nPart = nGroups;
  remF  = nmtf;
  gs = 0;
  while (nPart > 0)
  {
    tFreq = remF / nPart;
    ge = gs-1;
    aFreq = 0;
    while (aFreq < tFreq && ge < alphaSize-1)
    {
      ge++;
      aFreq += s->lookup[0][ge];
    }

    if (ge > gs
        && nPart != nGroups && nPart != 1
        && ((nGroups-nPart) % 2 == 1)) {
      aFreq -= s->lookup[0][ge];
      ge--;
    }

    for (v = 0; v < alphaSize; v++)
      if (v >= gs && v <= ge)
        s->length[nPart-1][v] = 0; else
        s->length[nPart-1][v] = 1;

    nPart--;
    gs = ge+1;
    remF -= aFreq;
  }
}

/* Find the tree which takes the least number of bits to encode current group.
   Returns number from 0 to nGroups-1 identifing the selected tree.
*/
static int
find_best_tree(
  const Short *gs,
  SInt nGroups,
  const Long *len)
{
  SInt c, bc;   /* code length, best code length */
  SInt t, bt;   /* tree, best tree */
  Long cp;      /* cost packed */

  /* Compute how many bits it takes to encode current group by each of trees.
     Utilise vector operations and full loop unrolling for best performance.
  */
  cp  = len[gs[ 0]] + len[gs[ 1]] + len[gs[ 2]] + len[gs[ 3]] + len[gs[ 4]];
  cp += len[gs[ 5]] + len[gs[ 6]] + len[gs[ 7]] + len[gs[ 8]] + len[gs[ 9]];
  cp += len[gs[10]] + len[gs[11]] + len[gs[12]] + len[gs[13]] + len[gs[14]];
  cp += len[gs[15]] + len[gs[16]] + len[gs[17]] + len[gs[18]] + len[gs[19]];
  cp += len[gs[20]] + len[gs[21]] + len[gs[22]] + len[gs[23]] + len[gs[24]];
  cp += len[gs[25]] + len[gs[26]] + len[gs[27]] + len[gs[28]] + len[gs[29]];
  cp += len[gs[30]] + len[gs[31]] + len[gs[32]] + len[gs[33]] + len[gs[34]];
  cp += len[gs[35]] + len[gs[36]] + len[gs[37]] + len[gs[38]] + len[gs[39]];
  cp += len[gs[40]] + len[gs[41]] + len[gs[42]] + len[gs[43]] + len[gs[44]];
  cp += len[gs[45]] + len[gs[46]] + len[gs[47]] + len[gs[48]] + len[gs[49]];

  /* At the beginning assume the first tree is the best. */
  bc = cp & 0x3ff;
  bt = 0;

  /* Iterate over other trees (starting from second one) to see
     which one is the best to encode current group. */
  for (t = 1; t < nGroups; t++)
  {
    cp >>= 10;
    c = cp & 0x3ff;
    if (c < bc)
      bc = c, bt = t;
  }

  /* Return our favorite. */
  return bt;
}

static void
increment_freqs(
  Short *gs,
  Int *rf)
{
  rf[gs[ 0]]++; rf[gs[ 1]]++; rf[gs[ 2]]++; rf[gs[ 3]]++; rf[gs[ 4]]++;
  rf[gs[ 5]]++; rf[gs[ 6]]++; rf[gs[ 7]]++; rf[gs[ 8]]++; rf[gs[ 9]]++;
  rf[gs[10]]++; rf[gs[11]]++; rf[gs[12]]++; rf[gs[13]]++; rf[gs[14]]++;
  rf[gs[15]]++; rf[gs[16]]++; rf[gs[17]]++; rf[gs[18]]++; rf[gs[19]]++;
  rf[gs[20]]++; rf[gs[21]]++; rf[gs[22]]++; rf[gs[23]]++; rf[gs[24]]++;
  rf[gs[25]]++; rf[gs[26]]++; rf[gs[27]]++; rf[gs[28]]++; rf[gs[29]]++;
  rf[gs[30]]++; rf[gs[31]]++; rf[gs[32]]++; rf[gs[33]]++; rf[gs[34]]++;
  rf[gs[35]]++; rf[gs[36]]++; rf[gs[37]]++; rf[gs[38]]++; rf[gs[39]]++;
  rf[gs[40]]++; rf[gs[41]]++; rf[gs[42]]++; rf[gs[43]]++; rf[gs[44]]++;
  rf[gs[45]]++; rf[gs[46]]++; rf[gs[47]]++; rf[gs[48]]++; rf[gs[49]]++;
}

Int
YBpriv_prefix(YBenc_t *s, Short *mtfv, Int nmtf)
{
  Int alphaSize;
  Int nGroups;
  Int iter, i;
  Int cost;

  alphaSize = mtfv[nmtf-1]+1;  /* the last mtfv is EOB */
  s->num_selectors = (nmtf + GROUP_SIZE - 1) / GROUP_SIZE;

  /* Decide how many prefix-free trees to use for current block.  The best
     for compression ratio would be to always use the maximal number of trees.
     However, the space it takes to transmit these trees can also be a factor,
     especially if the data being encoded is not very long.  If we use less
     trees for smaller block then the space needed to transmit additional
     trees is traded against the space saved by using more trees. */
  assert(nmtf >= 2);
  if      (nmtf > 2400) nGroups = 6;
  else if (nmtf > 1200) nGroups = 5;
  else if (nmtf >  600) nGroups = 4;
  else if (nmtf >  200) nGroups = 3;
  else if (nmtf >   50) nGroups = 2;
  else                  nGroups = 1;

  /* Complete the last group with dummy symbols. */
  for (i = nmtf; i < s->num_selectors * GROUP_SIZE; i++)
    mtfv[i] = alphaSize;

  /* Grow up an initial forest. */
  generate_initial_trees(s, nmtf, alphaSize, nGroups);

  /* Iterate several times to improve trees. */
  iter = s->prefix_factor;
  while (iter-- > 0)
  {
    Long len_pack[MAX_ALPHA_SIZE+1];
    Short *gs;
    Int v, t;
    Byte *sp;
    /* int tfreq[MAX_TREES]; */

    /* Pack code lengths of all trees into 64-bit integers in order to take
       advantage of 64-bit vector arithmetics.  Each group holds at most
       50 codes, each code is at most 20 bit long, so each group is coded
       by at most 1000 bits.  We can store that in 10 bits. */
    for (v = 0; v < alphaSize; v++)
      len_pack[v] = (((Long)s->length[0][v]      ) |
                     ((Long)s->length[1][v] << 10) |
                     ((Long)s->length[2][v] << 20) |
                     ((Long)s->length[3][v] << 30) |
                     ((Long)s->length[4][v] << 40) |
                     ((Long)s->length[5][v] << 50));
    len_pack[alphaSize] = 0;

    /* bzero(tfreq, nGroups * sizeof(*tfreq); */
    bzero(s->rfreq, nGroups * (MAX_ALPHA_SIZE+1) * sizeof(**s->rfreq));

    sp = s->selector;

    for (gs = mtfv; gs < mtfv+nmtf; gs += GROUP_SIZE)
    {
      /* Check out which prefix-free tree is the best to encode current
         group.  Then increment symbol frequencies for the chosen tree
         and remember the choice in the selector array. */
      t = find_best_tree(gs, nGroups, len_pack);
      assert(t < nGroups);
      increment_freqs(gs, s->rfreq[t]);
      /* tfreq[t]++; */
      Trace((stderr, "prefix: adding selector number %5d of value %d.\n",
             sp - s->selector, t));
      *sp++ = t;
    }

    assert((size_t)(sp - s->selector) == s->num_selectors);
    Trace((stderr, "prefix: adding selector number %5d of value %d "
           "(end marker).\n", sp - s->selector, MAX_TREES));
    *sp = MAX_TREES;

    /* TODO: bring this code back to life, may improove compression level */
    /* Remove unused trees. */
    /* WARNING: removing a tree will most likely invalidate selectors!!
       If this code is to be brought back to life, all selectors
       need to be recomputed after removing a tree.
     */
#if 0
    for (t = 0; t < nGroups; t++)
      if (tfreq[t] == 0)
      {
        nGroups--;
        if (t != nGroups)
          for (v = 0; v < alphaSize; v++)
          {
            s->count[t][v] = s->count[nGroups][v];
            s->length[t][v] = s->length[nGroups][v];
            s->rfreq[t][v] = s->rfreq[nGroups][v];
          }
      }
#endif

    /*--
      Recompute the tables based on the accumulated frequencies.
      --*/
    for (t = 0; t < nGroups; t++)
      make_code_lengths(s->count[t], s->length[t], s->rfreq[t], alphaSize);
  }

  {
    Int t;
    for (t = 0; t < nGroups; t++)
    {
      assign_codes(s->count[t], s->lookup[t], s->length[t], alphaSize);
      s->lookup[t][alphaSize] = 0;
      s->length[t][alphaSize] = 0;
    }
  }

  s->num_trees = nGroups;


  cost = 0;

  {
    Int t, v;
    for (t = 0; t < nGroups; t++)
    {
      Byte *len = s->length[t];
      SInt p, c, d;

      cost += 6;
      cost += s->rfreq[t][0] * len[0];
      p = len[0];
      for (v = 1; v < alphaSize; v++)
      {
        c = len[v];
        assert(c >= 1 && c <= 20);
        d = p-c;
        if (d < 0)
          d = -d;
        cost += 1+2*d;
        p = c;
        cost += s->rfreq[t][v] * len[v];
      }
    }
  }

  /* If there is only one prefix tree in current block, we need to create
     a second dummy tree.  This increases the cost of transmitting the block,
     but unfortunately bzip2 doesn't allow blocks with a single tree. */
  if (s->num_trees == 1)
  {
    Int v;
    s->num_trees++;
    for (v = 0; v < MAX_ALPHA_SIZE; v++)
      s->length[1][v] = MAX_CODE_LENGTH;
    cost += alphaSize + 5;
  }

  return cost;
}

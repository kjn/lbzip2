/*-
  encode.c -- low-level compressor

  Copyright (C) 2011, 2012, 2013 Mikolaj Izdebski

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

#include "common.h"
#include "encode.h"

#include <arpa/inet.h>          /* htonl() */
#include <string.h>             /* memset() */
#include <strings.h>            /* bzero() */


/*
  PREFIX CODING (also called Huffman coding)

  bzip2 file format uses cannonical, prefix-free codes in the last stage of
  coding process. bzip2 predescor -- bzip -- used arithmenic coding instead.
  Any cannonical, prefix-free codes can be used in bzip2 file. One could use
  Shannon or Shannon-Fano codes, but they are usually suboptimal.

  In bzip2 file format the maximal code length is limited to 20, meaning that
  no code longer than 20 bits can exist. For generating such liength-limited
  prefix code bzip2 uses an algorithm based on the original Huffman algorithm,
  but it has several disadvantages. It may require several iterations to
  converge and the generated codes can be suboptimal in some cases.

  One of the best known algorithms for generating optimal length-limited prefix
  code is Package-Merge algorithm. Unfortunatelly it is significantly slower
  and uses more memory than simple algorithms like Huffman algorithm.

  lbzip2 implements a hybrid algorithm. First a lightweight in-place algorithm
  based on Huffman algorithm is used to create optimal prefix codes. Then the
  maximal code length is computed and if it found to exceed the maximal allowed
  length (wich is 20), then these results are discarded and the Package-Merge
  algorithm is used to solve the problem from scratch.
*/


struct encoder_state {
  bool cmap[256];

  uint32_t block_crc;

  uint32_t bwt_idx;
  uint32_t out_expect_len;
  uint32_t nmtf;
  uint32_t nblock;
  uint32_t alpha_size;

  uint32_t max_block_size;
  uint32_t cluster_factor;

  uint8_t *block;
  void *mtfv;

  uint8_t *selector;
  uint8_t *selectorMTF;
  uint32_t num_selectors;
  uint32_t num_trees;
  uint32_t count[MAX_TREES][32];
  /* There is a sentinel symbol added at the end of each alphabet,
     hence the +1s below. */
  uint8_t length[MAX_TREES][MAX_ALPHA_SIZE + 1];
  uint32_t lookup[MAX_TREES][MAX_ALPHA_SIZE + 1];
  uint32_t rfreq[MAX_TREES][MAX_ALPHA_SIZE + 1];

  unsigned tmap_old2new[MAX_TREES];
  unsigned tmap_new2old[MAX_TREES];
};

extern uint32_t crc_table[256];


#define CRC(x) crc = (crc << 8) ^ crc_table[(crc >> 24) ^ (x)]

#define MAX_RUN_LENGTH (4+255)


struct encoder_state *
encoder_init(unsigned long max_block_size, unsigned cluster_factor)
{
  struct encoder_state *s = XMALLOC(struct encoder_state);

  assert(s != 0);
  assert(max_block_size > 0 && max_block_size <= MAX_BLOCK_SIZE);
  assert(cluster_factor > 0 && cluster_factor <= 65535);

  s->max_block_size = max_block_size;
  s->cluster_factor = cluster_factor;

  s->selector = XNMALLOC(18000 + 1 + 1, uint8_t);
  s->selectorMTF = XNMALLOC(18000 + 1 + 7, uint8_t);
  s->block = XNMALLOC(max_block_size + 1, uint8_t);

  bzero(s->cmap, 256u * sizeof(bool));

  return s;
}


void
collect(struct encoder_state *s, const uint8_t *inbuf, size_t *buf_sz)
{
  /* Cache some often used member variables for faster access. */
  size_t avail = *buf_sz;
  const uint8_t *p = inbuf;
  const uint8_t *pLim = p + avail;
  uint8_t *q = s->block;
  uint8_t *qMax = s->block + s->max_block_size - 1;
  unsigned ch, last;
  uint32_t run;
  uint32_t save_crc;
  uint32_t crc = -1;

state0:
  /*=== STATE 0 ===*/
  if (unlikely(q > qMax || p == pLim))
    goto done;
  ch = *p++;
  CRC(ch);

#define S1                                      \
  s->cmap[ch] = true;                           \
  *q++ = ch;                                    \
  if (unlikely (q > qMax || p == pLim))         \
    goto done;                                  \
  last = ch;                                    \
  ch = *p++;                                    \
  CRC (ch);                                     \
  if (unlikely (ch == last))                    \
    goto state2

state1:
  /*=== STATE 1 ===*/
  S1;
  S1;
  S1;
  S1;
  goto state1;

state2:
  /*=== STATE 2 ===*/
  *q++ = ch;
  if (unlikely(q > qMax || p == pLim))
    goto done;
  ch = *p++;
  CRC(ch);
  if (ch != last)
    goto state1;

  /*=== STATE 3 ===*/
  *q++ = ch;

  if (unlikely((q >= qMax && (q > qMax || (p < pLim && *p == last))) ||
               p == pLim))
    goto done;
  ch = *p++;
  CRC(ch);
  if (ch != last)
    goto state1;

  /*=== STATE 4+ ===*/
  assert(q < qMax);
  *q++ = ch;

  /* While the run is shorter than MAX_RUN_LENGTH characters,
     keep trying to append more characters to it. */
  for (run = 4; run < MAX_RUN_LENGTH; run++) {
    /* Check for end of input buffer. */
    if (unlikely(p == pLim)) {
      *q++ = run - 4;
      s->cmap[run - 4] = true;
      goto done;
    }

    /* Fetch the next character. */
    ch = *p++;
    save_crc = crc;
    CRC(ch);

    /* If the character does not match, terminate
       the current run and start a fresh one. */
    if (ch != last) {
      *q++ = run - 4;
      s->cmap[run - 4] = true;
      if (likely(q <= qMax))
        goto state1;

      /* There is no space left to begin a new run.
         Unget the last character and finish. */
      p--;
      crc = save_crc;
      goto done;
    }
  }

  /* The run has reached maximal length,
     so it must be ended prematurely. */
  *q++ = MAX_RUN_LENGTH - 4;
  s->cmap[MAX_RUN_LENGTH - 4] = true;
  goto state0;

done:
  s->nblock = q - s->block;
  s->block_crc = crc;
  *buf_sz -= p - inbuf;
}


/* return ninuse */
static unsigned
make_map_e(uint8_t *cmap, const bool *inuse)
{
  unsigned i, j;

  j = 0;

  for (i = 0; i < 256; i++) {
    int k = inuse[i];

    cmap[i] = j;
    j += k;
  }

  return j;
}


/*---------------------------------------------------*/
/* returns nmtf */
static uint32_t
do_mtf(uint16_t *mtfv, uint32_t *mtffreq, uint8_t *cmap, int32_t nblock,
       int32_t EOB)
{
  uint8_t order[255];
  int32_t i;
  int32_t k;
  int32_t t;
  uint8_t c;
  uint8_t u;
  uint32_t *bwt = (void *)mtfv;
  const uint16_t *mtfv0 = mtfv;

  for (i = 0; i <= EOB; i++)
    mtffreq[i] = 0;

  k = 0;
  u = 0;
  for (i = 0; i < 255; i++)
    order[i] = i + 1;

#define RUN()                                   \
  if (unlikely(k))                              \
    do {                                        \
      mtffreq[*mtfv++ = --k & 1]++;             \
      k >>= 1;                                  \
    } while (k)                                 \

#define MTF()                                   \
  {                                             \
    uint8_t *p = order;                         \
    t  = *p;                                    \
    *p = u;                                     \
    for (;;)                                    \
    {                                           \
      if (c == t) { u = t; break; }             \
      u  = *++p;                                \
      *p = t;                                   \
      if (c == u) break;                        \
      t  = *++p;                                \
      *p = u;                                   \
    }                                           \
    t = p - order + 2;                          \
    *mtfv++ = t;                                \
    mtffreq[t]++;                               \
  }

  for (i = 0; i < nblock; i++) {
    if ((c = cmap[*bwt++]) == u) {
      k++;
      continue;
    }
    RUN();
    MTF();
  }

  RUN();

  *mtfv++ = EOB;
  mtffreq[EOB]++;

  return mtfv - mtfv0;

#undef RUN
#undef MTF
}

size_t
encode(struct encoder_state *s, uint32_t *crc)
{
  uint32_t cost;
  uint32_t pk;
  uint32_t i;
  const uint8_t *sp;            /* selector pointer */
  uint8_t *smp;                 /* selector MTFV pointer */
  uint8_t c;                    /* value before MTF */
  uint8_t j;                    /* value after MTF */
  uint32_t p;                   /* MTF state */
  uint32_t EOB;
  uint8_t cmap[256];


  EOB = make_map_e(cmap, s->cmap) + 1;
  assert(EOB >= 2);
  assert(EOB < 258);

  /* Sort block. */
  assert(s->nblock > 0);
  s->mtfv = XNMALLOC(s->nblock + GROUP_SIZE, uint32_t);

  s->bwt_idx = divbwt(s->block, s->mtfv, s->nblock);
  free(s->block);
  s->nmtf = do_mtf(s->mtfv, s->lookup[0], cmap, s->nblock, EOB);

  cost = 48    /* header */
       + 32    /* crc */
       +  1    /* rand bit */
       + 24    /* bwt index */
       + 00    /* {cmap} */
       +  3    /* nGroups */
       + 15    /* nSelectors */
       + 00    /* {sel} */
       + 00    /* {tree} */
       + 00;   /* {mtfv} */

  cost += generate_prefix_code(s);

  sp = s->selector;
  smp = s->selectorMTF;

  /* A trick that allows to do MTF without branching, using arithmetical
     and logical operations only.  The whole MTF state packed into one
     32-bit integer.
   */

  /* Set up initial MTF state. */
  p = 0x543210;

  assert(*sp < MAX_TREES);
  assert(s->tmap_old2new[*sp] == 0);

  while ((c = *sp) != MAX_TREES) {
    uint32_t v, z, l, h;

    c = s->tmap_old2new[c];
    assert(c < s->num_trees);
    assert((size_t)(sp - s->selector) < s->num_selectors);

    v = p ^ (0x111111 * c);
    z = (v + 0xEEEEEF) & 0x888888;
    l = z ^ (z - 1);
    h = ~l;
    p = (p | l) & ((p << 4) | h | c);
#if GNUC_VERSION >= 30406
    j = (__builtin_ctz(h) >> 2) - 1;
#else
    h &= -h;
    j = !!(h & 0x01010100);
    h |= h >> 4;
    j |= h >> 11;
    j |= h >> 18;
    j &= 7;
#endif
    sp++;
    *smp++ = j;
    cost += j + 1;
  }

  /* Add zero to seven dummy selectors in order to make block size
     multiply of 8 bits. */
  j = cost & 0x7;
  j = (8 - j) & 0x7;
  s->num_selectors += j;
  cost += j;
  while (j--)
    *smp++ = 0;
  assert(cost % 8 == 0);

  /* Calculate the cost of transmitting character map. */
  for (i = 0; i < 16; i++) {
    pk = 0;
    for (j = 0; j < 16; j++)
      pk |= s->cmap[16 * i + j];
    cost += pk << 4;
  }
  cost += 16;  /* Big bucket costs 16 bits on its own. */

  /* Convert cost from bits to bytes. */
  assert(cost % 8 == 0);
  cost >>= 3;

  s->out_expect_len = cost;

  *crc = s->block_crc;

  return cost;
}


/* Sort source alphabet by descending fequency.

   Use plain simple insertion sort because (1) the alphabet is small enough and
   (2) we expect symbols to be already nearly sorted on common data.
*/
static void
sort_alphabet(uint64_t *first, uint64_t *last)
{
  uint64_t t, *a, *b, *b1;

  for (a = first + 1; a < last; ++a) {
    t = *(b1 = a);
    for (b = b1 - 1; *b < t; --b) {
      *b1 = *b;
      if ((b1 = b) == first)
        break;
    }
    *b1 = t;
  }
}

#ifndef PACKAGE_MERGE

/* Build a prefix-free tree.  Because the source alphabet is already sorted,
   we need not to maintain a priority queue -- two normal FIFO queues
   (one for leaves and one for internal nodes) will suffice.
 */
static void
build_tree(uint32_t *restrict T, uint64_t *restrict P, int32_t n)
{
  int32_t r;   /* index of the next tree in the queue */
  int32_t s;   /* index of the next singleton leaf */
  int32_t t;   /**/
  uint64_t w1, w2, w;

  r = n - 1;
  s = n - 1;   /* Start with the last singleton tree. */
  t = n - 1;

  while (t > 0) {
    /* If it's not the first iteration then r < t and  */
    assert(t == n - 1 || (r > t && s < t));

    /* Select the first node to be merged. */
    if (s < 0 || P[r] < P[s]) {
      /* Select an internal node. */
      T[r] = t;
      w1 = P[r--];
    }
    else
      /* Select a singleton leaf node. */
      w1 = P[s--];

    /* Select the second node to be merged. */
    if (s < 0 || (r > t && P[r] < P[s])) {
      T[r] = t;
      w2 = P[r--];
    }
    else
      w2 = P[s--];

    w = (w1 + w2) & ~(uint64_t)0xFF00FFFF;
    w1 &= 0xFF000000;
    w2 &= 0xFF000000;
    if (w2 > w1)
      w1 = w2;
    w2 = P[t] & 0xFFFF;
    P[t--] = w + w1 + 0x01000000 + w2;
  }
}


/* Compute counts from given Huffman tree.  The tree itself is clobbered. */
static void
compute_depths(uint32_t *restrict C, uint32_t *restrict T, uint32_t n)
{
  uint32_t a;    /* total number of nodes at current level */
  uint32_t u;    /* number of internal nodes */
  uint32_t t;    /* current tree node */
  uint32_t d;    /* current node depth */

  T[1] = 0;      /* The root always has depth of 0. */
  C[0] = 0;      /* There are no zero-length codes in bzip2. */
  t = 2;         /* The root is done, advance to the next node (of index 2). */
  d = 1;         /* The root was the last node at depth 0, go deeper. */
  a = 2;         /* At depth of 1 there are always exactly 2 nodes. */

  /* Repeat while we have more nodes. */
  while (d < 32) {
    u = 0;       /* So far we haven't seen any internal nodes at this level. */

    while (t < n && T[T[t]] + 1 == d) {
      assert(a > u);
      u++;
      T[t++] = d;  /* Overwrite parent pointer with node depth. */
    }

    C[d] = a - u;
    d++;
    a = u << 1;
  }

  assert(a == 0);
}

#endif /*!PACKAGE_MERGE */


/* The following is an implementation of the Package-Merge algorithm for
   finding an optimal length-limited prefix-free codeset.
*/

static void
package_merge(uint32_t *restrict C, const uint64_t *restrict Pr, uint32_t n)
{
#define BITS_PER_SYMBOL 9       /* ceil(log2(MAX_ALPHA_SIZE)) */
#define SYMBOLS_PER_WORD 7      /* floor(64 / BITS_PER_SYMBOL) */
#define VECTOR_SIZE 3           /* ceil(MAX_CODE_LENGTH / SYMBOLS_PER_WORD) */
#define QUEUE_SIZE (MAX_ALPHA_SIZE - 1)
/* It can be easily proved by induction that number of elemnts stored
   in the queue is always stricly less elements than alphabet size. */

  uint64_t W[2*QUEUE_SIZE];     /* internal node weights */
  uint64_t P[2*QUEUE_SIZE][VECTOR_SIZE];

  unsigned x;                   /* number of unprocessed singleton nodes */
  unsigned i;                   /* general purpose index */
  unsigned k;                   /* vector index */
  unsigned d;                   /* current node depth */
  unsigned jP;                  /* current index in queue P */
  unsigned jL;                  /* current index in queue L */
  unsigned szP;                 /* current size of queue P */
  unsigned iP;                  /* current offset of head of queue P */
  uint64_t dw;                  /* symbol weight at current depth */

  iP = MAX_CODE_LENGTH % 2 * (MAX_ALPHA_SIZE - 1);
  szP = 0;

  d = VECTOR_SIZE - 1;
  dw = (uint64_t)1 << (MAX_CODE_LENGTH % SYMBOLS_PER_WORD * BITS_PER_SYMBOL);
  for (;;) {
    dw >>= BITS_PER_SYMBOL;
    d += -!dw;
    if (d+1 == 0)
      break;
    dw += -!dw & ((uint64_t)1 << ((SYMBOLS_PER_WORD - 1) * BITS_PER_SYMBOL));

    x = n;
    jP = iP;
    iP ^= MAX_ALPHA_SIZE - 1;
    jL = iP;

    for (jL = iP; x + szP > 1; jL++) {
      if (szP == 0 || (x > 1 && Pr[x-2] < W[jP])) {
        W[jL] = Pr[x-1] + Pr[x-2];
        for (k = 0; k < VECTOR_SIZE; k++)
          P[jL][k] = 0;
        P[jL][d] += 2 * dw;
        x -= 2;
      }
      else if (x == 0 || (szP > 1 && W[jP+1] <= Pr[x-1])) {
        W[jL] = W[jP] + W[jP+1];
        for (k = 0; k < VECTOR_SIZE; k++)
          P[jL][k] = P[jP][k] + P[jP+1][k];
        jP += 2;
        szP -= 2;
      }
      else {
        W[jL] = W[jP] + Pr[x-1];
        for (k = 0; k < VECTOR_SIZE; k++)
          P[jL][k] = P[jP][k];
        P[jL][d] += dw;
        jP++;
        x--;
        szP--;
      }
    }

    szP = jL - iP;
    assert(szP >= n/2);
    assert(szP < n);
  }
  assert(iP == 0);
  assert(szP == n-1);

  k = VECTOR_SIZE * SYMBOLS_PER_WORD;
  for (i = VECTOR_SIZE; i--;) {
    dw = P[szP-1][i];
    for (d = SYMBOLS_PER_WORD * BITS_PER_SYMBOL; d;)
      C[k--] = (dw >> (d -= BITS_PER_SYMBOL)) & 0x1ff;
  }
  C[0] = 0;
}


static void
make_code_lengths(uint32_t C[], uint8_t L[], uint32_t P0[], uint32_t n)
{
  uint32_t i;
  int32_t k;
  int32_t d;
  int32_t c;
  uint64_t P[MAX_ALPHA_SIZE];

  assert(n >= MIN_ALPHA_SIZE);
  assert(n <= MAX_ALPHA_SIZE);

  /* Label weights with sequence numbers.
     Labelling has two main purposes: firstly it allows to sort pairs of weight
     and sequence number more easily; secondly: the package-merge algorithm
     requires weights to be strictly monotonous and putting unique values in
     lower bits assures that. */
  for (i = 0; i < n; i++) {
    /*
       FFFFFFFF00000000 - symbol frequency
       00000000FF000000 - node depth
       0000000000FF0000 - initially one
       000000000000FFFF - symbol
     */
    if (P0[i] == 0)
      P[i] = ((uint64_t)1 << 32) | 0x10000 | (MAX_ALPHA_SIZE - i);
    else
      P[i] = ((uint64_t)P0[i] << 32) | 0x10000 | (MAX_ALPHA_SIZE - i);
  }

  /* Sort weights and sequence numbers together. */
  sort_alphabet(P, P + n);

  {
#ifndef PACKAGE_MERGE
    uint32_t V[MAX_ALPHA_SIZE];

    build_tree(V, P, n);
    compute_depths(C, V, n);
  }

  k = 0;
  for (d = MAX_CODE_LENGTH + 1; d < 32; d++)
    k += C[d];

  /* If any code exceeds length limit, fallback to package-merge algorithm. */
  if (k != 0) {
    for (i = 0; i < n; i++) {
      if (P0[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] == 0)
        P[i] = ((uint64_t)1 << 32) | 0x10000 | (P[i] & 0xFFFF);
      else
        P[i] = ((uint64_t)P0[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] << 32)
            | 0x10000 | (P[i] & 0xFFFF);
    }
#endif

    package_merge(C, P, n);
  }

  /* Generate code lengths and transform counts into base codes. */
  i = 0;
  c = 0;
  for (d = 0; d <= MAX_CODE_LENGTH; d++) {
    k = C[d];

    C[d] = c;
    c = (c + k) << 1;

    while (k != 0) {
      assert(i < n);
      L[MAX_ALPHA_SIZE - (P[i] & 0xFFFF)] = d;
      i++;
      k--;
    }
  }
  assert(c == (1UL << (MAX_CODE_LENGTH + 1)));
  assert(i == n);
}


static void
assign_codes(uint32_t C[], uint32_t L[], uint8_t B[], uint32_t n)
{
  /* Assign prefix-free codes. */
  uint32_t i;

  for (i = 0; i < n; i++)
    L[i] = C[B[i]]++;
}


/* Create initial mapping of symbols to trees.

   The goal is to divide all as symbols [0,as) into nt equivalence classes (EC)
   [0,nt) such that standard deviation of symbol frequencies in classes is
   minimal. We use a kind of a heuristic to achieve that. There might exist a
   better way to achieve that, but this one seems to be good (and fast) enough.

   If the symbol v belongs to the equivalence class t then set s->length[t][v]
   to zero. Otherwise set it to 1.
*/
static void
generate_initial_trees(struct encoder_state *s, unsigned nm, unsigned nt)
{
  unsigned a, b;   /* range [a,b) of symbols forming current EC */
  unsigned freq;   /* symbol frequency */
  unsigned cum;    /* cumulative frequency */
  unsigned as;     /* effective alphabet size (alphabet size minus number
                      of symbols with frequency equal to zero) */
  unsigned t;      /* current tree */

  /* Equivalence classes are initially empty. */
  memset(s->length, 1, sizeof(s->length));

  /* Determine effective alphabet size. */
  as = 0;
  for (a = 0, cum = 0; cum < nm; a++) {
    freq = s->lookup[0][a];
    cum += freq;
    as += min(freq, 1);
  }
  assert(cum == nm);

  /* Bound number of EC by number of symbols. Each EC is non-empty, so number
     of symbol EC must be <= number of symbols. */
  nt = min(nt, as);

  /* For each equivalence class: */
  a = 0;
  for (t = 0; nt > 0; t++, nt--) {
    assert(nm > 0);
    assert(as >= nt);

    /* Find a range of symbols which total count is roughly proportional to one
       nt-th of all values. */
    freq = s->lookup[0][a];
    cum = freq;
    as -= min(freq, 1);
    b = a+1;
    while (as > nt-1 && cum * nt < nm) {
      freq = s->lookup[0][b];
      cum += freq;
      as -= min(freq, 1);
      b++;
    }
    if (cum > freq && (2*cum - freq) * nt > 2*nm) {
      cum -= freq;
      as += min(freq, 1);
      b--;
    }
    assert(a < b);
    assert(cum > 0);
    assert(cum <= nm);
    assert(as >= nt-1);
    Trace(("Tree %u: EC=[%3u,%3u), |EC|=%3u, cum=%6u", t, a, b, b-a, cum));

    /* Now [a,b) is our range -- assign it to equivalence class t. */
    bzero(&s->length[t][a], b - a);
    a = b;
    nm -= cum;
  }
  assert(as == 0);
  assert(nm == 0);
}

/* Find the tree which takes the least number of bits to encode current group.
   Return number from 0 to nt-1 identifying the selected tree.
*/
static int
find_best_tree(const uint16_t *gs, unsigned nt, const uint64_t *len)
{
  unsigned c, bc;   /* code length, best code length */
  unsigned t, bt;   /* tree, best tree */
  uint64_t cp;      /* cost packed */
  unsigned i;

  /* Compute how many bits it takes to encode current group by each of trees.
     Utilize vector operations for best performance. Let's hope the compiler
     unrolls the loop for us.
   */
  cp = 0;
  for (i = 0; i < GROUP_SIZE; i++)
    cp += len[gs[i]];

  /* At the beginning assume the first tree is the best. */
  bc = cp & 0x3ff;
  bt = 0;

  /* Iterate over other trees (starting from second one) to see
     which one is the best to encode current group. */
  for (t = 1; t < nt; t++) {
    cp >>= 10;
    c = cp & 0x3ff;
    if (c < bc)
      bc = c, bt = t;
  }

  /* Return our favorite. */
  return bt;
}


/* Compute bit cost of transmitting a single tree and all symbols it codes. */
static uint32_t
transmission_cost(const uint8_t *length, const uint32_t *rfreq, uint32_t as)
{
  unsigned v;
  unsigned p;
  uint32_t cost;

  /* Compute cost of transmiting RUNA symbols. */
  cost = 6;  /* fixed code length (5bits) and delta code (1bit) */
  p = length[0];
  cost += rfreq[0] * p;  /* cost of transmitting prefix codes */

  /* Compute cost of transmiting all other symbols, from RUNB to EOB. */
  for (v = 1; v < as; v++) {
    int c, d;
    c = length[v];
    assert(c >= 1 && c <= 20);
    d = p - c;
    if (d < 0)
      d = -d;
    cost += 1 + 2 * d;  /* cost of transmitting delta code ... */
    p = c;
    cost += rfreq[v] * length[v];  /* ... and prefix codes */
  }

  return cost;
}


/* The main function generating prefix code for the whole block.

   Input: MTF values
   Output: trees and selectors

   What this function does:
    1) decides how many trees to generate
    2) divides groups into equivalence classes (using Expectation-Maximization
       algorithm, which is a heuristic usually giving suboptimal results)
    3) generates an optimal prefix tree for each class (with a hubrid algorithm
       consisting of Huffman algorithm and Package-Merge algorithm)
    4) generates selectors
    5) sorts trees by their first occurence in selector sequence
    6) computes and returns cost (in bits) of transmitting trees and codes
*/
unsigned
generate_prefix_code(struct encoder_state *s)
{
  uint32_t as;
  uint32_t nt;
  uint32_t iter, i;
  uint32_t cost;

  uint16_t *mtfv = s->mtfv;
  uint32_t nm = s->nmtf;

  as = mtfv[nm - 1] + 1;       /* the last mtfv is EOB */
  s->num_selectors = (nm + GROUP_SIZE - 1) / GROUP_SIZE;

  /* Decide how many prefix-free trees to use for current block.  The best
     for compression ratio would be to always use the maximal number of trees.
     However, the space it takes to transmit these trees can also be a factor,
     especially if the data being encoded is not very long.  If we use less
     trees for smaller block then the space needed to transmit additional
     trees is traded against the space saved by using more trees.
  */
  assert(nm >= 2);
  nt = (nm > 2400 ? 6 :
        nm > 1200 ? 5 :
        nm >  600 ? 4 :
        nm >  300 ? 3 :
        nm >  150 ? 2 : 1);

  /* Complete the last group with dummy symbols. */
  for (i = nm; i < s->num_selectors * GROUP_SIZE; i++)
    mtfv[i] = as;

  /* Grow up an initial forest. */
  generate_initial_trees(s, nm, nt);

  /* Perform a few iterations of the Expectation-Maximization algorithm to
     improve trees.
  */
  iter = s->cluster_factor;
  while (iter-- > 0) {
    uint64_t len_pack[MAX_ALPHA_SIZE + 1];
    uint16_t *gs;
    uint32_t v, t;
    uint8_t *sp;

    /* Pack code lengths of all trees into 64-bit integers in order to take
       advantage of 64-bit vector arithmetic.  Each group holds at most
       50 codes, each code is at most 20 bit long, so each group is coded
       by at most 1000 bits.  We can store that in 10 bits. */
    for (v = 0; v < as; v++)
      len_pack[v] = (((uint64_t)s->length[0][v]      ) +
                     ((uint64_t)s->length[1][v] << 10) +
                     ((uint64_t)s->length[2][v] << 20) +
                     ((uint64_t)s->length[3][v] << 30) +
                     ((uint64_t)s->length[4][v] << 40) +
                     ((uint64_t)s->length[5][v] << 50));
    len_pack[as] = 0;

    sp = s->selector;

    /* (E): Expectation step -- estimate likehood. */
    bzero(s->rfreq, nt * sizeof(*s->rfreq));
    for (gs = mtfv; gs < mtfv + nm; gs += GROUP_SIZE) {
      /* Check out which prefix-free tree is the best to encode current
         group.  Then increment symbol frequencies for the chosen tree
         and remember the choice in the selector array. */
      t = find_best_tree(gs, nt, len_pack);
      assert(t < nt);
      *sp++ = t;
      for (i = 0; i < GROUP_SIZE; i++)
        s->rfreq[t][gs[i]]++;
    }

    assert((size_t)(sp - s->selector) == s->num_selectors);
    *sp = MAX_TREES;  /* sentinel */

    /* (M): Maximization step -- maximize expectations. */
    for (t = 0; t < nt; t++)
      make_code_lengths(s->count[t], s->length[t], s->rfreq[t], as);
  }

  cost = 0;

  /* Reorder trees. This also removes unused trees. */
  {
    /* Only lowest nt bits are used, from 0 to nt-1. If i-th bit is set then
       i-th tree exists but hasn't been seen yet. */
    unsigned not_seen = (1 << nt) - 1;
    unsigned t, v;
    uint8_t *sp = s->selector;

    nt = 0;
    while (not_seen > 0 && (t = *sp++) < MAX_TREES) {
      if (not_seen & (1 << t)) {
        not_seen -= 1 << t;
        s->tmap_old2new[t] = nt;
        s->tmap_new2old[nt] = t;
        nt++;

        /* Create lookup tables for this tree. These tables are used by the
           transmiter to quickly send codes for MTF values. */
        assign_codes(s->count[t], s->lookup[t], s->length[t], as);
        s->lookup[t][as] = 0;
        s->length[t][as] = 0;

        /* Compute cost of transmiting the tree and all symbols it codes. */
        cost += transmission_cost(s->length[t], s->rfreq[t], as);
      }
    }

    /* If there is only one prefix tree in current block, we need to create
       a second dummy tree.  This increases the cost of transmitting the block,
       but unfortunately bzip2 doesn't allow blocks with a single tree. */
    assert(nt >= 1);
    if (nt == 1) {
      nt = 2;
      t = s->tmap_new2old[0] ^ 1;
      s->tmap_old2new[t] = 1;
      s->tmap_new2old[1] = t;
      for (v = 0; v < MAX_ALPHA_SIZE; v++)
        s->length[t][v] = MAX_CODE_LENGTH;
      cost += as + 5;
    }
  }

  s->num_trees = nt;
  return cost;
}


#define SEND(n,v)                               \
  b = (b << (n)) | (v);                         \
  if ((k += (n)) >= 32) {                       \
    uint32_t w = (uint32_t)(b >> (k -= 32));    \
    *p++ = htonl(w);                            \
  }

void
transmit(struct encoder_state *s, void *buf)
{
  uint64_t b;
  unsigned k;
  uint8_t *sp;
  unsigned t;
  unsigned v;
  uint16_t *mtfv;
  uint32_t *p;
  unsigned ns;
  unsigned as;
  uint32_t gr;


  /* Initialize bit buffer. */
  b = 0;
  k = 0;
  p = buf;

  /* Transmit block metadata. */
  SEND(24, 0x314159);
  SEND(24, 0x265359);
  SEND(32, s->block_crc ^ 0xFFFFFFFF);
  SEND(1, 0);                   /* non-rand */
  SEND(24, s->bwt_idx);         /* bwt primary index */

  /* Transmit character map. */
  {
    unsigned pack[16];
    unsigned pk;
    unsigned i;
    unsigned j;
    unsigned big = 0;

    for (i = 0; i < 16; i++) {
      pk = 0;
      for (j = 0; j < 16; j++)
        pk = (pk << 1) + s->cmap[16 * i + j];
      pack[i] = pk;
      big = (big << 1) + !!pk;
    }

    SEND(16, big);
    for (i = 0; i < 16; i++)
      if (pack[i]) {
        SEND(16, pack[i]);
      }
  }

  /* Transmit selectors. */
  assert(s->num_trees >= MIN_TREES && s->num_trees <= MAX_TREES);
  SEND(3, s->num_trees);
  ns = s->num_selectors;
  SEND(15, ns);
  sp = s->selectorMTF;
  while (ns--) {
    v = 1 + *sp++;
    SEND(v, (1 << v) - 2);
  }

  mtfv = s->mtfv;
  as = mtfv[s->nmtf - 1] + 1;
  ns = (s->nmtf + GROUP_SIZE - 1) / GROUP_SIZE;

  /* Transmit prefix trees. */
  for (t = 0; t < s->num_trees; t++) {
    int32_t a, c;
    uint8_t *len = s->length[s->tmap_new2old[t]];

    a = len[0];
    SEND(6, a << 1);
    for (v = 1; v < as; v++) {
      c = len[v];
      while (a < c) {
        SEND(2, 2);
        a++;
      }
      while (a > c) {
        SEND(2, 3);
        a--;
      }
      SEND(1, 0);
    }
  }

  /* Transmit prefix codes. */
  for (gr = 0; gr < ns; gr++) {
    unsigned i;          /* symbol index in group */
    const uint32_t *L;   /* symbol-to-code lookup table */
    const uint8_t *B;    /* code lengths */
    unsigned mv;         /* symbol (MTF value) */

    t = s->selector[gr];
    L = s->lookup[t];
    B = s->length[t];

    for (i = 0; i < GROUP_SIZE; i++) {
      mv = *mtfv++;
      SEND(B[mv], L[mv]);
    }
  }

  /* Flush  */
  assert(k % 8 == 0);
  assert(k / 8 == s->out_expect_len % 4);
  assert(p == (uint32_t *)buf + s->out_expect_len / 4);
  SEND(31, 0);

  free(s->selector);
  free(s->selectorMTF);
  free(s->mtfv);
  free(s);
}

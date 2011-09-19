/* minbzcat - minimalistic bzcat
   Copyright (C) 2011 Mikolaj Izdebski

   Permission to use, copy, modify, and/or distribute this software for any
   purpose with or without fee is hereby granted, provided that the above
   copyright notice and this permission notice appear in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <stdio.h>
#include <stdlib.h>

int bb, bk;
unsigned long crctab[256], tt[900000];
unsigned long crc, mbs, rnd, bs, idx, as, nt, ns, nm;
unsigned char blk[900000], len[6][259], sel[32767], mtf[256];
unsigned short count[21], sorted[258], mv[900050];

unsigned short tab[512] = { 
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

void bad() {
  fprintf(stderr, "bad bz2 file\n");
  exit(EXIT_FAILURE);
}

unsigned long get(int n) {
  unsigned long x = 0;
  while (n--) {
    if (!bk-- && (bb = getchar()) == EOF) {
      if (ferror(stdin)) perror("read");
      else fprintf(stderr, "unexpected eof\n");
      exit(EXIT_FAILURE);
    }
    x = 2*x + ((bb >> (bk &= 7)) & 1);
  }
  return x;
}

void init_crc() {
  int i, k;
  for (i = 0; i < 256; i++) {
    crctab[i] = (unsigned long)i << 24;
    for (k = 0; k < 8; k++)
      crctab[i] = ((crctab[i] << 1) & 0xFFFFFFFF) ^
        (0x04C11DB7L & -(crctab[i] >> 31));
  }
}

void make_tree(int t) {
  unsigned short u[21], i, s, a;
  for (i = 0; i <= 20; i++)
    count[i] = 0;
  for (i = 0; i < as; i++)
    count[len[t][i]]++;
  for (a = 1, s = 0, i = 0; i <= 20; i++) {
    u[i] = s;
    a *= 2;
    if (count[i] > a) bad();
    a -= count[i];
    s += count[i];
  }
  for (i = 0; i < as; i++)
    sorted[u[len[t][i]]++] = i;
}

unsigned get_sym() {
  int s = 0, x = 0, k = 0;
  do {
    if (k == 20) bad();
    x = 2*x + get(1);
    s += count[++k];
  } while ((x -= count[k]) >= 0);
  return sorted[s + x];
}

void bmp() {
  unsigned i, j;
  unsigned short b = get(16);
  as = 0;
  for (i = 0; i < 16; i++) {
    if (b & 0x8000) {
      unsigned short s = get(16);
      for (j = 0; j < 16; j++) {
        if (s & 0x8000)
          mtf[as++] = 16*i + j;
        s *= 2;
      }
    }
    b *= 2;
  }
  as += 2;
}

void smtf() {
  unsigned g;
  for (g = 0; g < ns; g++) {
    sel[g] = 0;
    while (sel[g] < nt && get(1))
      sel[g]++;
    if (sel[g] == nt) bad();
  }
  if (ns > 18001)
    ns = 18001;
}

void trees() {
  unsigned t,s;
  for (t = 0; t < nt; t++) {
    len[t][0] = get(5);
    for (s = 0; s < as; s++) {
      goto in;
      do {
        len[t][s] += 1 - 2*get(1);
      in:
        if (len[t][s] < 1 || len[t][s] > 20) bad();
      } while (get(1));
      len[t][s+1] = len[t][s];
    }
  }
}

void data() {
  unsigned g,i,t;
  unsigned m[6];
  for (i = 0; i < 6; i++)
    m[i] = i;
  nm = 0;
  for (g = 0; g < ns; g++) {
    i = sel[g];
    t = m[i];
    while (i-- > 0)
      m[i+1] = m[i];
    m[0] = t;
    make_tree(t);
    for (i = 0; i < 50; i++)
      if ((mv[nm++] = get_sym()) == as-1)
        return;
  }
  bad();
}

void retr() {
  rnd = get(1);
  idx = get(24);
  bmp();
  nt = get(3);
  if (nt < 2 || nt > 6) bad();
  ns = get(15);
  smtf();
  trees();
  data();
}

void imtf() {
  unsigned i,s,t,r,h;
  bs = r = h = 0;
  for (i = 0; i < nm; i++) {
    s = mv[i];
    if (s <= 1) {
      r += 1 << (h + s);
      h++;
    } else {
      if (bs+r > mbs) bad();
      while (r--)
        tt[bs++] = mtf[0];
      if (s == as-1)
        break;
      t = mtf[--s];
      while (s-- > 0)
        mtf[s+1] = mtf[s];
      mtf[0] = t;
      h = 0;
      r = 1;
    }
  }
}

void ibwt() {
  unsigned long i, c, f[256];
  if (idx >= bs) bad();
  for (i = 0; i < 256; i++)
    f[i] = 0;
  for (i = 0; i < bs; i++)
    f[tt[i]]++;
  for (i = c = 0; i < 256; i++)
    f[i] = (c += f[i]) - f[i];
  for (i = 0; i < bs; i++)
    tt[f[tt[i] & 0xFF]++] |= i << 8;
  idx = tt[idx];
  for (i = 0; i < bs; i++) {
    idx = tt[idx >> 8];
    blk[i] = idx;
  }
}

void derand() {
  unsigned long i = 0, j = 617;
  while (rnd && j < bs) {
    blk[j] ^= 1;
    i = (i + 1) & 0x1FF;
    j += tab[i];
  }
}

void write(int c) {
  crc = ((crc << 8) & 0xFFFFFFFF) ^ crctab[(crc >> 24) ^ c];
  if (putchar(c) == EOF)
    perror("write"), exit(EXIT_FAILURE);
}

void emit() {
  unsigned long i, r, c, d;
  r = 0;
  c = -1;
  for (i = 0; i < bs; i++) {
    d = c;
    c = blk[i];
    write(c);
    if (c != d)
      r = 1;
    else { 
      r++;
      if (r >= 4) {
        unsigned j;
        if (++i == bs) bad();
        for (j = 0; j < blk[i]; j++)
          write(c);
        r = 0;
      }
    }
  }
}

int main() {
  unsigned long t = 0, c;
  init_crc();
  if (get(24) != 0x425A68) bad();
  do {
    c = 0;
    t = get(8) - 0x31;
    if (t >= 9) bad();
    mbs = 100000 * (t+1);
    while ((t = get(16)) == 0x3141) {
      if (get(32) != 0x59265359) bad();
      t = get(32);
      retr();
      imtf();
      ibwt();
      derand();
      crc = 0xFFFFFFFF;
      emit();
      if ((crc ^ 0xFFFFFFFF) != t) bad();
      c = ((c << 1) & 0xFFFFFFFF) ^ (c >> 31) ^ t;
    }
    if (t != 0x1772) bad();
    if (get(32) != 0x45385090) bad();
    if (get(32) != c) bad();
    bk = 0;
  } while (getchar() == 0x42 && getchar() == 0x5A && getchar() == 0x68);
  return EXIT_SUCCESS;
}

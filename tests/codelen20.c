/* Copyright (C) 2010, 2013 Mikolaj Izdebski */

#include <stdio.h>

unsigned b,k;

p(x,n) {
  b = (b << n) + x, k += n;
  while (k >= 8)
    putchar((b >> (k -= 8)) & 0xff);
}
pb(b) { p(b,8); }

/*
  Alphabet used:

  258 symbols (2 RUN, 255 MTFV, 1 EOB)

  symbol  code_len
  0-126     7
  127       8
  128       9
  129      10
  130      11
  131      12
  132      13
  133      18
  134-257  20

  Kraft inequality:
  127*2^-7 + 2^-8 + 2^-9 + 2^-10 + 2^-11 + 2^-12 + 2^-13 + 2^-18 + 124*2^-20 = 1

  symbol   delta code   (bin)          (hex)
  0-126       0          0              0
  127-132    +1          100            4
  133        +5          10101010100    554
  134        +2          10100          14
  135-257     0          0              0
*/
main() {
  int i,j;
  pb('B'), pb('Z'), pb('h'), pb('9');     /* magic */

  p(0x314159,24), p(0x265359, 24);        /* block header */
  p(0x81B0,16), p(0x2D8B,16);             /* block crc */
  p(0,1);   /* block randomised? */
  p(0,24);  /* bwt index */
  for (i=16+256; i--;) p(1,1);  /* bitmap */
  p(2,3);                       /* 2 groups */
  p(1,15);                      /* 1 selector */
  p(0,1);                       /* selector MTF value */
  for (j=2; j--;) {             /* code lens for both groups */
    p(7,5);                     /* first 126 codes are 7-bit */
    for (i=  0; i<=126; i++) p(0,1);       /* delta  0 */
    for (i=127; i<=132; i++) p(4,3);       /* delta +1 */
    for (i=133; i<=133; i++) p(0x554,11);  /* delta +5 */
    for (i=134; i<=134; i++) p(0x14,5);    /* delta +2 */
    for (i=135; i<=257; i++) p(0,1);       /* delta  0 */
  }

  p(1+'A',7);                       /* MTF value */
  p(0xFFFFF,20);                    /* 20-bit EOB code */
  p(0x177245,24), p(0x385090, 24);  /* end of stream header */
  p(0x81B0,16), p(0x2D8B,16);       /* combined crc */
  p(0,7);                           /* padding */
  return 0;
}

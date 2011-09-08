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

#include "private.h"


struct YBobs_s
{
  YBcrc_t crc;
};


struct YBenc_s
{
  int rle_state;
  Int rle_character;
  Int block_crc;

  Byte *cmap;
  Int ninuse;

  Int bwt_idx;
  Int out_expect_len;
  Int nmtf;
  Int nblock;
  Int alpha_size;

  Int max_block_size;
  Int shallow_factor;
  Int prefix_factor;

  Byte *block;
  Short *mtfv;

  Byte *selector;
  Byte *selectorMTF;
  Int num_selectors;
  Int num_trees;
  Int count[MAX_TREES][32];
  /* There is a sentinel symbol added at the end of each alphabet,
     hence the +1s below. */
  Byte length[MAX_TREES][MAX_ALPHA_SIZE+1];
  Int lookup[MAX_TREES][MAX_ALPHA_SIZE+1];
  Int rfreq[MAX_TREES][MAX_ALPHA_SIZE+1];
};


void YBpriv_block_sort(YBenc_t *s);
Int YBpriv_prefix(YBenc_t *s, Short *mtfv, Int nmtf);

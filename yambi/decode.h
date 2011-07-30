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

#include "private.h"


#define HUFF_START_WIDTH 10

/* IMTF_ROW_WIDTH and IMTF_NUM_ROWS must be positive
   and IMTF_NUM_ROWS * IMTF_ROW_WIDTH == 256. */
#define IMTF_ROW_WIDTH 16
#define IMTF_NUM_ROWS 16
#define IMTF_SLIDE_LENGTH 8192


struct Tree {
  Short start[1 << HUFF_START_WIDTH];
  Long base[MAX_CODE_LENGTH + 2];  /* two sentinels (at first and last pos) */
  Int count[MAX_CODE_LENGTH + 1];  /* one sentinel (at first pos) */
  Short perm[MAX_ALPHA_SIZE];
};

struct YBibs_s
{
  YBdec_t *dec;

  int recv_state;
  Int max_block_size;
  Int next_crc;
  unsigned next_shift;
  Int crc;
  int canceled;

  Long save_v;
  int save_w;
  Short save_big;
  Short save_small;
  int save_i;
  int save_t;
  Int save_s;
  int save_r;
  Int save_j;
  struct Tree *save_T;
  Short save_x;
  int save_k;
  int save_g;
  int save_togo;
  Int save_magic1;
  Int save_magic2;
  int save_has_block;

  Byte selector[32767];
  int num_trees;
  int num_selectors;
  int mtf[MAX_TREES];
  struct Tree tree[MAX_TREES];
};


struct YBdec_s
{
  YBibs_t *ibs;

  /* Block sequence number modulo 32. It's required only to compute
     the stream CRC */
  unsigned block_shift;

  /* Stuff for UnRLE. */
  Int rle_index;  /* current index in the IBWT list */
  Int rle_avail;  /* compressed bytes still available in the IBWT list */
  Int rle_crc;    /* CRC of uncompr. data computed so far */
  Int rle_state;  /* current state of the UnRLE FSA */
  Byte rle_char;  /* byte the state transition depends on it) */
  Byte rle_prev;  /* byte associated to the current FSA state */

  Int rand;            /* rand flag (1 if block is randomised, 0 otherwise) */
  Int bwt_idx;         /* BWT primary index */
  Int block_size;      /* compressed block size */
  Int max_block_size;  /* max allowed compr. block size */
  Int num_mtfv;        /* number of MTF values */
  Int alpha_size;      /* number of distinct prefix codes */
  Int expect_crc;      /* expected block CRC */

  /* Stuff for IMTF. */
  Byte *imtf_row[IMTF_NUM_ROWS];
  Byte imtf_slide[IMTF_SLIDE_LENGTH];

  /* Big arrays. */
  Short tt16[900050];
  Int tt[900000];
};


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

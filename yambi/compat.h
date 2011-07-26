/*-
  Copyright (C) 2011 Mikolaj Izdebski.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef YAMBI_COMPAT_H
#define YAMBI_COMPAT_H

#define BZ_RUN               0
#define BZ_FLUSH             1
#define BZ_FINISH            2

#define BZ_OK                0
#define BZ_RUN_OK            1
#define BZ_FLUSH_OK          2
#define BZ_FINISH_OK         3
#define BZ_STREAM_END        4
#define BZ_SEQUENCE_ERROR    (-1)
#define BZ_PARAM_ERROR       (-2)
#define BZ_MEM_ERROR         (-3)
#define BZ_DATA_ERROR        (-4)
#define BZ_DATA_ERROR_MAGIC  (-5)
#define BZ_IO_ERROR          (-6)
#define BZ_UNEXPECTED_EOF    (-7)
#define BZ_OUTBUFF_FULL      (-8)
#define BZ_CONFIG_ERROR      (-9)

/* Order of members of this struct should be preserved
   to retain binary compatibility with libbz2 applications.
*/
typedef struct {
  char *next_in;
  unsigned avail_in;
  unsigned total_in_lo32;
  unsigned total_in_hi32;

  char *next_out;
  unsigned avail_out;
  unsigned total_out_lo32;
  unsigned total_out_hi32;

  void *state;
  void *(*bzalloc)(void *,int,int);
  void (*bzfree)(void *,void *);
  void *opaque;
} bz_stream;

int BZ2_like_bzDecompressInit(bz_stream *strm, int verbosity, int small);
int BZ2_like_bzDecompress(bz_stream* strm);
int BZ2_like_bzDecompressEnd(bz_stream *strm);

#endif

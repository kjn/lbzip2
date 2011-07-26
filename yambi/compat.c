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

#include "compat.h"
#include "yambi.h"

#include <stdlib.h>


#define UPDATE(vv, kk)                                  \
  do                                                    \
  {                                                     \
    unsigned long lo = strm->total_ ## vv ## _lo32;     \
    unsigned long hi = strm->total_ ## vv ## _hi32;     \
    unsigned long inc = (kk);                           \
                                                        \
    strm->next_ ## vv += inc;                           \
    strm->avail_ ## vv -= inc;                          \
                                                        \
    lo = (lo + inc) & 0xFFFFFFFFUL;                     \
    hi = (hi + (lo < inc)) & 0xFFFFFFFFUL;              \
                                                        \
    strm->total_ ## vv ## _lo32 = lo;                   \
    strm->total_ ## vv ## _hi32 = hi;                   \
  }                                                     \
  while (0)
struct DState {
  YBibs_t *ibs;
  YBdec_t *dec;
  int emit;
};

#if 0
# include <stdio.h>
# define PP(s) (void)fprintf(stderr, s "\n")
#else
# define PP(s)
#endif


int
BZ2_like_bzDecompressInit(bz_stream* strm,
                     int verbosity,
                     int small)
{
  struct DState *s;

  /* Perform parameter checking mandated by bzip2 documentation. */
  if (strm == 0 ||
      verbosity < 0 || verbosity > 4 ||
      (small != 0 && small != 1))
    return BZ_PARAM_ERROR;

  /* Allocate the internal state. */
  if (strm->bzalloc != 0)
    s = (struct DState *)strm->bzalloc(strm->opaque, sizeof(struct DState), 1);
  else
    s = (struct DState *)malloc(sizeof(struct DState));
  if (s == 0)
    return BZ_MEM_ERROR;

  s->ibs = YBibs_init();
  s->dec = YBdec_init();

  if (s->ibs == NULL || s->dec == NULL)
  {
    if (s->ibs)
      YBibs_destroy(s->ibs);
    if (s->dec)
      YBdec_destroy(s->dec);
    if (strm->bzfree != 0)
      strm->bzfree(strm->opaque, s);
    else
      free(s);
    return BZ_MEM_ERROR;
  }

  s->emit = 0;

  strm->state = s;
  strm->total_in_lo32 = 0;
  strm->total_in_hi32 = 0;
  strm->total_out_lo32 = 0;
  strm->total_out_hi32 = 0;

  return BZ_OK;
}


int
BZ2_like_bzDecompress(bz_stream *strm)
{
  struct DState *s;
  ptrdiff_t rv;

  if (strm == 0 || strm->state == 0 || strm->avail_out < 1)
    return BZ_PARAM_ERROR;

  s = (struct DState *)strm->state;

  while (1)
  {
    if (s->emit)
    {
      rv = YBdec_emit(s->dec, strm->next_out, strm->avail_out);

      if (rv == YB_UNDERFLOW)
      {
        PP("emit underflow");
        UPDATE(out, strm->avail_out);
        return BZ_OK;
      }

      if (rv < 0)
      {
        PP("emit data error");
        return BZ_DATA_ERROR;
      }

      PP("emit successfull");
      UPDATE(out, strm->avail_out - rv);

      YBdec_join(s->dec);

      s->emit = 0;
    }

    rv = YBibs_recv(s->ibs, s->dec, strm->next_in, strm->avail_in);

    if (rv == YB_EMPTY)
    {
      PP("decode empty");
      UPDATE(in, 14 - strm->total_in_lo32);
      return BZ_STREAM_END;
    }

    if (rv == YB_DONE)
    {
      rv = YBibs_check(s->ibs);

      if (rv == YB_OK)
      {
        PP("decode finished");
        return BZ_STREAM_END;
      }

      PP("decode crc error");
      return BZ_DATA_ERROR;
    }

    if (rv == YB_UNDERFLOW)
    {
      PP("decode underflow");
      UPDATE(in, strm->avail_in);
      return BZ_OK;
    }

    if (rv < 0)
    {
      PP("decode data error");
      return BZ_DATA_ERROR;
    }

    PP("decode successfull");
    UPDATE(in, strm->avail_in - rv);

    rv = YBdec_work(s->dec);
    if (rv < 0)
    {
      PP("work data error");
      return BZ_DATA_ERROR;
    }

    PP("work successfull");
    s->emit = 1;
  }
}


int
BZ2_like_bzDecompressEnd(bz_stream *strm)
{
  struct DState *s;

  if (strm == 0 || strm->state == 0)
    return BZ_PARAM_ERROR;

  s = (struct DState *)strm->state;

  YBibs_destroy(s->ibs);
  YBdec_destroy(s->dec);

  if (strm->bzfree)
    strm->bzfree(strm->opaque, strm->state);
  else
    free(strm->state);

  strm->state = 0;

  return BZ_OK;
}


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */

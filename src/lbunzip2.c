/*-
  Copyright (C) 2011 Mikolaj Izdebski
  Copyright (C) 2008, 2009, 2010 Laszlo Ersek

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <arpa/inet.h>    /* ntohl() */
#include <assert.h>       /* assert() */
#include <signal.h>       /* SIGUSR2 */
#include <string.h>       /* memcpy() */
#include <stdlib.h>       /* free() */

#include "xalloc.h"       /* xmalloc() */
#include "yambi.h"        /* YBdec_t */

#include "main.h"         /* pname */
#include "lbunzip2.h"     /* lbunzip2() */
#include "pqueue.h"       /* struct pqueue */


/* 48 bit mask for bzip2 block header and end of stream marker. */
static const uint64_t
    magic_mask = (uint64_t)0xFFFFlu << 32 | (uint64_t)0xFFFFFFFFlu;

/* 48 bit bzip2 block header. */
static const uint64_t
    magic_hdr = (uint64_t)0x3141lu << 32 | (uint64_t)0x59265359lu;

/*
  We assume that there exists an upper bound on the size of any bzip2 block,
  i.e. we don't try to support arbitarly large blocks.
*/
#define MX_SPLIT ((size_t)(1024u * 1024u))

struct s2w_blk                   /* Splitter to workers. */
{
  uintmax_t id;                  /* Block serial number as read from stdin. */
  struct s2w_blk *next;          /* First part of next block belongs to us. */
  unsigned refno;                /* Threads not yet done with this block. */
  size_t loaded;                 /* Number of bytes in compr. */
  uint32_t compr[MX_SPLIT / 4u]; /* Data read from stdin. */
};


struct w2w_blk_id
{
  uintmax_t s2w_blk_id,     /* Stdin block index. */
      bzip2_blk_id;         /* Bzip2 block index within stdin block. */
  int last_bzip2;           /* Last bzip2 for stdin block. */
};


struct w2w_blk              /* Worker to workers. */
{
  struct w2w_blk_id id;     /* Stdin blk idx & bzip2 blk idx in former. */
  YBdec_t *ybdec;           /* Partly decompressed block. */
};


static int
w2w_blk_cmp(const void *v_a, const void *v_b)
{
  const struct w2w_blk_id *a,
      *b;

  a = &((const struct w2w_blk *)v_a)->id;
  b = &((const struct w2w_blk *)v_b)->id;

  return
        a->s2w_blk_id   < b->s2w_blk_id   ? -1
      : a->s2w_blk_id   > b->s2w_blk_id   ?  1
      : a->bzip2_blk_id < b->bzip2_blk_id ? -1
      : a->bzip2_blk_id > b->bzip2_blk_id ?  1
      : 0;
}


struct sw2w_q                /* Splitter and workers to workers queue. */
{
  struct cond proceed;       /* See below. */
  struct s2w_blk *next_scan; /* Scan this stdin block for bzip2 blocks. */
  int eof;                   /* Splitter done with producing s2w_blk's. */
  struct pqueue deco_q;      /* Queue of bzip2 streams to decompress. */
  unsigned scanning;         /* # of workers currently producing w2w_blk's. */
};
/*
  The monitor "proceed" is associated with two predicates, because any worker
  can be in either of two needs to proceed (see work_get_first() and
  work_get_second()). We don't use two condition variables because
  - with two variables, all broadcast sites would have to consider both
    variables,
  - one of the predicates is stricter and implies the weaker one,
  - it's a rare occurrence that the weaker proceed predicate (!B) holds and the
    stricter one (!A) does not, so spurious wakeups are rare.

  The proceed predicate for work_get_first():
  !A: !empty(deco_q) || 0 != next_scan || (eof && 0u == scanning)

  Necessary condition so there is any worker blocking in work_get_first():
  A: empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning)

  The proceed predicate for work_get_second():
  !B: !empty(deco_q) || 0 != next_scan || eof

  Necessary condition so there is any worker blocking in work_get_second():
  B: empty(deco_q) && 0 == next_scan && !eof

  B is stricter than A, B implies A,
  !A is stricter than !B, !A implies !B.

  Below, X denotes the value of the predicate or a protected member evaluated
  just after entering the monitor, before any changes are made to the protected
  members. X' denotes the value of the predicate or a protected member
  evaluated just before leaving the monitor, after all changes are made.

  We want to send as few as possible broadcasts. We can omit the broadcast if,
  before making any changes to the protected fields, neither of the necessary
  conditions for blocking hold: !A && !B, because then no worker can block.
  Otherwise (A || B), there may be a worker blocking. In that case, we only
  need to send the broadcast if we produced new tasks (enabled the proceed
  predicate) for those possibly blocking. (If we didn't enable the proceed
  predicate, then it makes no sense to wake them up.) So we'll send the
  broadcast iff this holds:

  (A && !A') || (B && !B')

  (
    (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
    && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
  )
  || (
    (empty(deco_q) && 0 == next_scan && !eof)
    && (!empty(deco_q') || 0 != next_scan' || eof')
  )

  A spurious wakeup happens when we send the broadcast because of B && !B' (the
  proceed predicate for work_get_second() becomes true from false), but !A'
  (the proceed predicate for work_get_first() evaluated after the changes) is
  false. This is rare:

  B && !B' && !!A'
  B && !B' && A'

  (empty(deco_q) && 0 == next_scan && !eof)
  && (!empty(deco_q') || 0 != next_scan' || eof')
  && (empty(deco_q') && 0 == next_scan' && (!eof' || 0u < scanning'))

  from A':

  empty(deco_q')
  0 == next_scan'

  (empty(deco_q) && 0 == next_scan && !eof)
  && (0 || 0 || eof')
  && (empty(deco_q') && 0 == next_scan' && (!eof' || 0u < scanning'))

  (empty(deco_q) && 0 == next_scan && !eof)
  && eof'
  && (empty(deco_q') && 0 == next_scan' && (!eof' || 0u < scanning'))

  For this to be true, 1 == eof' must hold.

  (empty(deco_q) && 0 == next_scan && !eof)
  && eof'
  && (empty(deco_q') && 0 == next_scan' && (0 || 0u < scanning'))

  (empty(deco_q) && 0 == next_scan && !eof)
  && eof'
  && (empty(deco_q') && 0 == next_scan' && (0u < scanning'))

  empty(deco_q)
  && 0 == next_scan
  && !eof
  && eof'
  && empty(deco_q')
  && 0 == next_scan'
  && 0u < scanning'

  This means, that before the change, there was nothing to decompress, nothing
  to scan, and we didn't reach EOF:

  empty(deco_q) && 0 == next_scan && !eof

  and furthermore, after the change, there is still nothing to decompress,
  nothing to scan, some workers are still scanning, and we reached EOF:

  eof' && empty(deco_q') && 0 == next_scan' && 0u < scanning'

  The key is the EOF transition. That happens only once in the lifetime of the
  process.
*/


static void
sw2w_q_init(struct sw2w_q *sw2w_q, unsigned num_worker)
{
  assert(0u < num_worker);
  xinit(&sw2w_q->proceed);
  sw2w_q->next_scan = 0;
  sw2w_q->eof = 0;
  pqueue_init(&sw2w_q->deco_q, w2w_blk_cmp);
  sw2w_q->scanning = num_worker;
}


static void
sw2w_q_uninit(struct sw2w_q *sw2w_q)
{
  assert(0u == sw2w_q->scanning);
  pqueue_uninit(&sw2w_q->deco_q);
  assert(0 != sw2w_q->eof);
  assert(0 == sw2w_q->next_scan);
  xdestroy(&sw2w_q->proceed);
}


struct w2m_blk_id
{
  struct w2w_blk_id w2w_blk_id; /* Stdin blk idx & bzip2 blk idx in former. */
  uintmax_t decompr_blk_id;     /* Decompressed block for bzip2 block. */
  int last_decompr;             /* Last decompressed for bzip2 block. */
};


struct w2m_blk_nid      /* Block needed for resuming writing. */
{
  uintmax_t s2w_blk_id, /* Stdin block index. */
      bzip2_blk_id,     /* Bzip2 block index within stdin block. */
      decompr_blk_id;   /* Decompressed block for bzip2 block. */
};


static int
w2m_blk_id_eq(const struct w2m_blk_id *id, const struct w2m_blk_nid *nid)
{
  return
         id->w2w_blk_id.s2w_blk_id   == nid->s2w_blk_id
      && id->w2w_blk_id.bzip2_blk_id == nid->bzip2_blk_id
      && id->decompr_blk_id          == nid->decompr_blk_id;
}


/* Worker decompression output granularity. */
#define MX_DECOMPR ((size_t)(1024u * 1024u))

struct w2m_blk                       /* Workers to muxer. */
{
  struct w2m_blk_id id;              /* Block index. */
  struct w2m_blk *next;              /* Next block in list (unordered). */
  size_t produced;                   /* Number of bytes in decompr. */
  char unsigned decompr[MX_DECOMPR]; /* Data to write to stdout. */
};


static int
w2m_blk_cmp(const void *v_a, const void *v_b)
{
  const struct w2m_blk_id *a,
      *b;

  a = &((const struct w2m_blk *)v_a)->id;
  b = &((const struct w2m_blk *)v_b)->id;

  return
        a->w2w_blk_id.s2w_blk_id   < b->w2w_blk_id.s2w_blk_id   ? -1
      : a->w2w_blk_id.s2w_blk_id   > b->w2w_blk_id.s2w_blk_id   ?  1
      : a->w2w_blk_id.bzip2_blk_id < b->w2w_blk_id.bzip2_blk_id ? -1
      : a->w2w_blk_id.bzip2_blk_id > b->w2w_blk_id.bzip2_blk_id ?  1
      : a->decompr_blk_id          < b->decompr_blk_id          ? -1
      : a->decompr_blk_id          > b->decompr_blk_id          ?  1
      : 0;
}


struct w2m_q
{
  struct cond av_or_ex_or_rel; /* New w2m_blk/s2w_blk av. or workers exited. */
  struct w2m_blk_nid needed;   /* Block needed for resuming writing. */
  struct w2m_blk *head;        /* Block list (unordered). */
  unsigned working,            /* Number of workers still running. */
      num_rel;                 /* Released s2w_blk's to return to splitter. */
};
/*
  There's something to do:
  (0 != head && list contains needed) || 0u < num_rel || 0u == working

  There's nothing to do for now (so block):
  (0 == head || list doesn't contain needed) && 0u == num_rel && 0u < working
*/


static void
w2m_q_init(struct w2m_q *w2m_q, unsigned num_worker)
{
  assert(0u < num_worker);
  xinit(&w2m_q->av_or_ex_or_rel);
  w2m_q->needed.s2w_blk_id = 0u;
  w2m_q->needed.bzip2_blk_id = 0u;
  w2m_q->needed.decompr_blk_id = 0u;
  w2m_q->head = 0;
  w2m_q->working = num_worker;
  w2m_q->num_rel = 0u;
}


static void
w2m_q_uninit(struct w2m_q *w2m_q)
{
  assert(0u == w2m_q->num_rel);
  assert(0u == w2m_q->working);
  assert(0 == w2m_q->head);
  assert(0u == w2m_q->needed.decompr_blk_id);
  assert(0u == w2m_q->needed.bzip2_blk_id);
  xdestroy(&w2m_q->av_or_ex_or_rel);
}


struct m2s_q         /* Muxer to splitter. */
{
  struct cond av;    /* Free slot available. */
  unsigned num_free; /* Number of free slots. */
};


static void
m2s_q_init(struct m2s_q *m2s_q, unsigned num_free)
{
  assert(0u < num_free);
  xinit(&m2s_q->av);
  m2s_q->num_free = num_free;
}


static void
m2s_q_uninit(struct m2s_q *m2s_q, unsigned num_free)
{
  assert(m2s_q->num_free == num_free);
  xdestroy(&m2s_q->av);
}


static void
split(struct m2s_q *m2s_q, struct sw2w_q *sw2w_q, struct filespec *ispec)
{
  struct s2w_blk *atch_scan;
  uintmax_t id;
  size_t vacant;

  atch_scan = 0;
  id = 0u;

  do {
    struct s2w_blk *s2w_blk;

    /* Grab a free slot. */
    xlock_pred(&m2s_q->av);
    while (0u == m2s_q->num_free) {
      xwait(&m2s_q->av);
    }
    --m2s_q->num_free;
    xunlock(&m2s_q->av);
    s2w_blk = xmalloc(sizeof *s2w_blk);

    /* Fill block. */
    vacant = sizeof s2w_blk->compr;
    xread(ispec, (void *)s2w_blk->compr, &vacant);

    if (sizeof s2w_blk->compr == vacant) {
      /* Empty input block. */
      free(s2w_blk);
      s2w_blk = 0;
    }
    else {
      s2w_blk->id = id++;
      s2w_blk->next = 0;
      /*
        References:
          - next_decompr always,
          - current tail -> new next if not first.
      */
      s2w_blk->refno = 1u + (unsigned)(0 != atch_scan);
      s2w_blk->loaded = sizeof s2w_blk->compr - vacant;
    }

    xlock(&sw2w_q->proceed);
    assert(!sw2w_q->eof);
    /*
      (
        (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
        && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
      )
      || (
        (empty(deco_q) && 0 == next_scan && !eof)
        && (!empty(deco_q') || 0 != next_scan' || eof')
      )

      --> !eof

      (
        (empty(deco_q) && 0 == next_scan && (1 || 0u < scanning))
        && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
      )
      || (
        (empty(deco_q) && 0 == next_scan && 1)
        && (!empty(deco_q') || 0 != next_scan' || eof')
      )

      (
        (empty(deco_q) && 0 == next_scan)
        && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
      )
      || (
        (empty(deco_q) && 0 == next_scan)
        && (!empty(deco_q') || 0 != next_scan' || eof')
      )
    */
    if (0 == sw2w_q->next_scan) {
      sw2w_q->next_scan = s2w_blk;

      /*
        --> 0 == next_scan
        --> 0 != next_scan' || eof'

        (
          (empty(deco_q) && 1)
          && (!empty(deco_q') || 0 != next_scan' || (1 && 0u == scanning'))
        )
        || (
          (empty(deco_q) && 1)
          && (!empty(deco_q') || 1)
        )

        (
          (empty(deco_q))
          && (!empty(deco_q') || 0 != next_scan' || (0u == scanning'))
        )
        || (empty(deco_q))

        (empty(deco_q))
        && (
          (!empty(deco_q') || 0 != next_scan' || (0u == scanning'))
          || 1
        )

        (empty(deco_q))
      */
      if (pqueue_empty(&sw2w_q->deco_q)) {
        xbroadcast(&sw2w_q->proceed);
      }
    }
    /*
      Otherwise:
      --> 0 == (0 == next_scan)

      (
        (empty(deco_q) && 0)
        && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
      )
      || (
        (empty(deco_q) && 0)
        && (!empty(deco_q') || 0 != next_scan' || eof')
      )

      0
    */

    if (0 != atch_scan) {
      assert(0u < atch_scan->refno);
      atch_scan->next = s2w_blk;
    }

    if (0u == vacant) {
      xunlock(&sw2w_q->proceed);
      atch_scan = s2w_blk;
    }
    else {
      sw2w_q->eof = 1;
      xunlock(&sw2w_q->proceed);
    }
  } while (0u == vacant);

  if (sizeof atch_scan->compr == vacant) {
    xlock(&m2s_q->av);
    ++m2s_q->num_free;
    xunlock(&m2s_q->av);
  }
}


struct split_arg
{
  struct m2s_q *m2s_q;
  struct sw2w_q *sw2w_q;
  struct filespec *ispec;
};


static void *
split_wrap(void *v_split_arg)
{
  struct split_arg *split_arg;

  split_arg = v_split_arg;

  split(split_arg->m2s_q, split_arg->sw2w_q, split_arg->ispec);
  return 0;
}


static void
work_decompr(struct w2w_blk *w2w_blk, struct w2m_q *w2m_q,
    struct filespec *ispec)
{
  int ybret;
  uintmax_t decompr_blk_id;
  void *obuf;
  size_t oleft;

  decompr_blk_id = 0u;

  ybret = YBdec_work(w2w_blk->ybdec);
  if (ybret != YB_OK)
    log_fatal("%s: %s%s%s: data error while decompressing block: %s\n",
        pname, ispec->sep, ispec->fmt, ispec->sep, YBerr_detail(ybret));

  do {
    struct w2m_blk *w2m_blk;

    w2m_blk = xmalloc(sizeof *w2m_blk);

    obuf = w2m_blk->decompr;
    oleft = sizeof w2m_blk->decompr;
    ybret = YBdec_emit(w2w_blk->ybdec, obuf, &oleft);
    if (ybret == YB_OK) {
    }
    else if (YB_UNDERFLOW != ybret)
      log_fatal("%s: %s%s%s: data error while emitting block: %s\n",
          pname, ispec->sep, ispec->fmt, ispec->sep, YBerr_detail(ybret));

    w2m_blk->id.w2w_blk_id = w2w_blk->id;
    w2m_blk->id.decompr_blk_id = decompr_blk_id++;
    w2m_blk->id.last_decompr = (ybret == YB_OK);
    w2m_blk->produced = sizeof w2m_blk->decompr - oleft;

    /*
      Push decompressed sub-block to muxer.

      If the muxer blocks, then this is true before the changes below:
      (if this is true before the changes, then the muxer may be blocking:)
      (0 == head || list doesn't cont. needed) && 0u == num_rel && 0u < working
      (0 == head || list doesn't cont. needed) && 0u == num_rel && 1
      (0 == head || list doesn't cont. needed) && 0u == num_rel

      Furthermore, if this is true after the changes, it must be woken up:
      (0 != head' && list' contains needed) || 0u < num_rel' || 0u == working'
      (1          && list' contains needed) || 0u < num_rel  || 0u == working
                     list' contains needed  || 0             || 0
                     list' contains needed

      Full condition for wakeup:
      (0 == head || list doesn't cont. needed)
          && 0u == num_rel && list' contains needed

      We don't know (0 == head || list doesn't cont. needed), assume it's 1:
      1 && 0u == num_rel && list' contains needed
           0u == num_rel && list' contains needed

      Using the previous assumption,
           0u == num_rel && (list' == list + needed)
    */
    xlock(&w2m_q->av_or_ex_or_rel);
    assert(0u < w2m_q->working);
    w2m_blk->next = w2m_q->head;
    w2m_q->head = w2m_blk;
    if (0u == w2m_q->num_rel && w2m_blk_id_eq(&w2m_blk->id, &w2m_q->needed)) {
      xsignal(&w2m_q->av_or_ex_or_rel);
    }
    xunlock(&w2m_q->av_or_ex_or_rel);
  } while (YB_UNDERFLOW == ybret);

  YBdec_destroy(w2w_blk->ybdec);
}


static void
work_oflush(struct w2w_blk **p_w2w_blk, uintmax_t s2w_blk_id,
    uintmax_t *bzip2_blk_id, int last_bzip2, struct sw2w_q *sw2w_q)
{
  struct w2w_blk *w2w_blk;

  w2w_blk = *p_w2w_blk;

  w2w_blk->id.s2w_blk_id = s2w_blk_id;
  w2w_blk->id.bzip2_blk_id = (*bzip2_blk_id)++;
  w2w_blk->id.last_bzip2 = last_bzip2;

  /* Push mostly reconstructed bzip2 stream to workers. */
  xlock(&sw2w_q->proceed);
  assert(0u < sw2w_q->scanning);
  /*
    (
      (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
      && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
    )
    || (
      (empty(deco_q) && 0 == next_scan && !eof)
      && (!empty(deco_q') || 0 != next_scan' || eof')
    )

    --> 1 == (0u < scanning),
    --> scanning' = scanning, 0 == (0u == scanning')

    (
      (empty(deco_q) && 0 == next_scan && (!eof || 1))
      && (!empty(deco_q') || 0 != next_scan' || (eof' && 0))
    )
    || (
      (empty(deco_q) && 0 == next_scan && !eof)
      && (!empty(deco_q') || 0 != next_scan' || eof')
    )

    (
      (empty(deco_q) && 0 == next_scan)
      && (!empty(deco_q') || 0 != next_scan')
    )
    || (
      (empty(deco_q) && 0 == next_scan && !eof)
      && (!empty(deco_q') || 0 != next_scan' || eof')
    )

    (empty(deco_q) && 0 == next_scan)
    && (
      (!empty(deco_q') || 0 != next_scan')
      || (!eof && (!empty(deco_q') || 0 != next_scan' || eof'))
    )

    --> next_scan' = next_scan
    --> eof' = eof
    --> 1 == (!empty(deco_q'))

    (empty(deco_q) && 0 == next_scan)
    && (
      (1 || 0 != next_scan)
      || (!eof && (1 || 0 != next_scan || eof))
    )

    empty(deco_q) && 0 == next_scan
  */
  if (pqueue_empty(&sw2w_q->deco_q) && 0 == sw2w_q->next_scan) {
    xbroadcast(&sw2w_q->proceed);
  }
  pqueue_insert(&sw2w_q->deco_q, w2w_blk);
  xunlock(&sw2w_q->proceed);

  *p_w2w_blk = 0;
}


static struct s2w_blk *
work_get_first(struct sw2w_q *sw2w_q, struct w2m_q *w2m_q,
    struct filespec *ispec)
{
  /* Hold "xlock_pred(&sw2w_q->proceed)" on entry. Will hold on return. */

  int loop;

  loop = 0;
  assert(0u < sw2w_q->scanning);

  --sw2w_q->scanning;
  for (;;) {
    /* Decompression enjoys absolute priority over scanning. */
    if (!pqueue_empty(&sw2w_q->deco_q)) {
      /*
        (
          (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
          && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
        )
        || (
          (empty(deco_q) && 0 == next_scan && !eof)
          && (!empty(deco_q') || 0 != next_scan' || eof')
        )

        --> 0 == (empty(deco_q))

        (
          (0 && 0 == next_scan && (!eof || 0u < scanning))
          && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
        )
        || (
          (0 && 0 == next_scan && !eof)
          && (!empty(deco_q') || 0 != next_scan' || eof')
        )

        0
      */
      struct w2w_blk *deco;

      deco = pqueue_peek(&sw2w_q->deco_q);
      pqueue_pop(&sw2w_q->deco_q);
      xunlock(&sw2w_q->proceed);

      work_decompr(deco, w2m_q, ispec);
      free(deco);

      xlock_pred(&sw2w_q->proceed);
    }
    else {
      if (0 != sw2w_q->next_scan) {
        /*
          (
            (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
            && (
              !empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning')
            )
          )
          || (
            (empty(deco_q) && 0 == next_scan && !eof)
            && (!empty(deco_q') || 0 != next_scan' || eof')
          )

          --> 0 == (0 == next_scan)

          (
            (empty(deco_q) && 0 && (!eof || 0u < scanning))
            && (
              !empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning')
            )
          )
          || (
            (empty(deco_q) && 0 && !eof)
            && (!empty(deco_q') || 0 != next_scan' || eof')
          )

          0
        */
        ++sw2w_q->scanning;
        return sw2w_q->next_scan;
      }

      if (sw2w_q->eof && 0u == sw2w_q->scanning) {
        /*
          (
            (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
            && (
              !empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning')
            )
          )
          || (
            (empty(deco_q) && 0 == next_scan && !eof)
            && (!empty(deco_q') || 0 != next_scan' || eof')
          )

          -->                          1 == (empty(deco_q))
          --> deco_q' == deco_q,       0 == (!empty(deco_q'))
          -->                          1 == (0 == next_scan)
          --> next_scan' == next_scan, 0 == (0 != next_scan')
          -->              1 == eof
          --> eof' == eof, 1 == eof'

          (
            (1 && 1 && (0 || 0u < scanning))
            && (
              0 || 0 || (1 && 0u == scanning')
            )
          )
          || (
            (1 && 1 && 0)
            && (0 || 0 || 1)
          )

          (0u < scanning) && (0u == scanning')

          --> 1 == (0u == scanning')

          (0u < scanning) && (1)

          0u < scanning

          ---o---

          --> The following statement is always true.

          (1 == loop && scanning' == scanning) || (0 == loop && 0u < scanning)

          --> 0u == scanning'

          (1 == loop && 0u == scanning) || (0 == loop && 0u < scanning)

          (0 == loop) <=> (0u < scanning)
        */
        if (0 == loop) {
          xbroadcast(&sw2w_q->proceed);
        }
        return 0;
      }

      /*
        (
          (empty(deco_q) && 0 == next_scan && (!eof || 0u < scanning))
          && (!empty(deco_q') || 0 != next_scan' || (eof' && 0u == scanning'))
        )
        || (
          (empty(deco_q) && 0 == next_scan && !eof)
          && (!empty(deco_q') || 0 != next_scan' || eof')
        )

        --> 1 == (empty(deco_q))
        --> 1 == (0 == next_scan)
        --> 0 == (eof && 0u == scanning')
        --> eof == eof', 0 == (eof' && 0u == scanning')
        --> deco_q == deco_q', 0 == (!empty(deco_q'))
        --> next_scan == next_scan', 0 == (0 != next_scan')

        (
          (1 && 1 && (!eof || 0u < scanning))
          && (0 || 0 || (0))
        )
        || (
          (1 && 1 && !eof)
          && (0 || 0 || eof')
        )

        !eof && eof'

        --> eof == eof'

        !eof && eof

        0
      */
      xwait(&sw2w_q->proceed);
    }

    loop = 1;
  }
}


static void
work_release(struct s2w_blk *s2w_blk, struct sw2w_q *sw2w_q,
    struct w2m_q *w2m_q)
{
  assert(0u < s2w_blk->refno);
  if (0u == --s2w_blk->refno) {
    assert(s2w_blk != sw2w_q->next_scan);
    xunlock(&sw2w_q->proceed);
    free(s2w_blk);
    xlock(&w2m_q->av_or_ex_or_rel);
    if (0u == w2m_q->num_rel++) {
      xsignal(&w2m_q->av_or_ex_or_rel);
    }
    xunlock(&w2m_q->av_or_ex_or_rel);
  }
  else {
    xunlock(&sw2w_q->proceed);
  }
}


static struct s2w_blk *
work_get_second(struct s2w_blk *s2w_blk, struct sw2w_q *sw2w_q,
    struct w2m_q *w2m_q, struct filespec *ispec)
{
  xlock_pred(&sw2w_q->proceed);

  for (;;) {
    /* Decompression enjoys absolute priority over scanning. */
    if (!pqueue_empty(&sw2w_q->deco_q)) {
      struct w2w_blk *deco;

      deco = pqueue_peek(&sw2w_q->deco_q);
      pqueue_pop(&sw2w_q->deco_q);
      xunlock(&sw2w_q->proceed);

      work_decompr(deco, w2m_q, ispec);
      free(deco);

      xlock_pred(&sw2w_q->proceed);
    }
    else {
      if (0 != sw2w_q->next_scan || sw2w_q->eof) {
        struct s2w_blk *next;

        assert(0 == sw2w_q->next_scan || 0 != s2w_blk->next);
        /*
          If "next_scan" is non-NULL: "next_scan" became a pointer to the
          current first element to scan either by assuming the values of the
          "next" pointers of the elements that were once pointed to by
          "next_scan" (including the one we're now trying to get the next one
          for), or by being updated by the splitter, but then the splitter
          updated "atch_scan->next" too. Thus no such "next" pointer can be 0.
          Also, "next_scan" becomes non-NULL no later than "s2w_blk->next",
          see split().

          If "next_scan" is NULL and so we're here because the splitter hit
          EOF: we'll return NULL iff we're trying to get the next input block
          for the last input block.
        */

        next = s2w_blk->next;
        work_release(s2w_blk, sw2w_q, w2m_q);
        return next;
      }

      xwait(&sw2w_q->proceed);
    }
  }
}


static void
work(struct sw2w_q *sw2w_q, struct w2m_q *w2m_q, struct filespec *ispec)
{
  struct s2w_blk *s2w_blk;
  uintmax_t first_s2w_blk_id;
  unsigned ibitbuf,
      ibits_left;
  size_t ipos;

  uintmax_t bzip2_blk_id;

  struct w2w_blk *w2w_blk;

  unsigned bit;
  uint64_t search;
  int ybret;

again:
  xlock_pred(&sw2w_q->proceed);
  s2w_blk = work_get_first(sw2w_q, w2m_q, ispec);
  if (0 == s2w_blk) {
    xunlock(&sw2w_q->proceed);

    /* Notify muxer when last worker exits. */
    xlock(&w2m_q->av_or_ex_or_rel);
    if (0u == --w2m_q->working && 0u == w2m_q->num_rel && 0 == w2m_q->head) {
      xsignal(&w2m_q->av_or_ex_or_rel);
    }
    xunlock(&w2m_q->av_or_ex_or_rel);
    return;
  }
  sw2w_q->next_scan = s2w_blk->next;
  xunlock(&sw2w_q->proceed);

  first_s2w_blk_id = s2w_blk->id;
  ibits_left = 0u;
  ipos = 0u;
  assert(0u < s2w_blk->loaded);

  bzip2_blk_id = 0u;
  w2w_blk = 0;
  search = -1;

  do {  /* never seen magic */
    if (0 == ibits_left) {
      if (s2w_blk->loaded / 4u == ipos) {
        if (sizeof s2w_blk->compr == s2w_blk->loaded) {
          log_fatal("%s: %s%s%s: missing bzip2 block header in full first"
              " input block\n", pname, ispec->sep, ispec->fmt, ispec->sep);
        }

        /* Short first input block without a bzip2 block header. */
        assert(sizeof s2w_blk->compr > s2w_blk->loaded);

        xlock(&sw2w_q->proceed);
        assert(0 == s2w_blk->next);
        assert(sw2w_q->eof);
        work_release(s2w_blk, sw2w_q, w2m_q);

        assert(0 == w2w_blk);
        goto again;
      }

      ibitbuf = ntohl(s2w_blk->compr[ipos]);
      ibits_left = 32u;
      ipos++;
    }

    bit = ibitbuf >> --ibits_left & 1u;
    search = (search << 1 | bit) & magic_mask;
  } while (magic_hdr != search);  /* never seen magic */

  w2w_blk = xmalloc(sizeof *w2w_blk);
  w2w_blk->ybdec = YBdec_init();

  for (;;) {  /* first block */
    do {  /* in bzip2 */
      ybret = YBdec_retrieve(w2w_blk->ybdec, s2w_blk->compr, &ipos,
          s2w_blk->loaded / 4u, &ibitbuf, &ibits_left);
      if (YB_UNDERFLOW == ybret) {
        if (sizeof s2w_blk->compr > s2w_blk->loaded) {
          log_fatal("%s: %s%s%s: unterminated bzip2 block in short first"
              " input block\n", pname, ispec->sep, ispec->fmt, ispec->sep);
        }
        assert(sizeof s2w_blk->compr == s2w_blk->loaded);

        s2w_blk = work_get_second(s2w_blk, sw2w_q, w2m_q, ispec);

        if (0 == s2w_blk) {
          log_fatal("%s: %s%s%s: unterminated bzip2 block in full first"
              " input block\n", pname, ispec->sep, ispec->fmt, ispec->sep);
        }

        ipos = 0u;
        assert(0u < s2w_blk->loaded);
        goto in_second;
      }
      else if (YB_OK == ybret) {
        work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 0, sw2w_q);

        w2w_blk = xmalloc(sizeof *w2w_blk);
        w2w_blk->ybdec = YBdec_init();
      }
      else if (YB_DONE == ybret) {
        break;
      }
      else {
        log_fatal("%s: %s%s%s: data error while retrieving block: %s\n",
            pname, ispec->sep, ispec->fmt, ispec->sep, YBerr_detail(ybret));
      }
    } while (1);  /* in bzip2 */

    search = -1;
    do {  /* out bzip2 */
      if (0 == ibits_left) {
        if (s2w_blk->loaded / 4u == ipos) {
          if (sizeof s2w_blk->compr > s2w_blk->loaded) {
            xlock(&sw2w_q->proceed);
            assert(0 == s2w_blk->next);
            assert(sw2w_q->eof);
            work_release(s2w_blk, sw2w_q, w2m_q);
            s2w_blk = 0;
          }
          else {
            assert(sizeof s2w_blk->compr == s2w_blk->loaded);
            s2w_blk = work_get_second(s2w_blk, sw2w_q, w2m_q, ispec);
          }

          if (0 == s2w_blk) {
            work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 1, sw2w_q);
            goto again;
          }

          ipos = 0u;
          assert(0u < s2w_blk->loaded);
          goto out_second;
        }

        ibitbuf = ntohl(s2w_blk->compr[ipos]);
        ibits_left = 32u;
        ipos++;
      }

      bit = ibitbuf >> --ibits_left & 1u;
      search = (search << 1 | bit) & magic_mask;
    } while (magic_hdr != search);  /* out bzip2 */

    work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 0, sw2w_q);

    w2w_blk = xmalloc(sizeof *w2w_blk);
    w2w_blk->ybdec = YBdec_init();
  }  /* first block */

  for (;;) {  /* second block */
    do {  /* in bzip2 */
    in_second:
      ybret = YBdec_retrieve(w2w_blk->ybdec, s2w_blk->compr, &ipos,
          s2w_blk->loaded / 4u, &ibitbuf, &ibits_left);
      if (YB_UNDERFLOW == ybret) {
        log_fatal("%s: %s%s%s: %s second input block\n", pname, ispec->sep,
            ispec->fmt, ispec->sep, sizeof s2w_blk->compr == s2w_blk->loaded ?
            "missing bzip2 block header in full" :
            "unterminated bzip2 block in short");
      }
      else if (YB_OK == ybret) {
        if ((size_t)((48u + ibits_left + 7u) / 8u) <= 4u * ipos) {
          xlock(&sw2w_q->proceed);
          work_release(s2w_blk, sw2w_q, w2m_q);
          work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 1, sw2w_q);
          goto again;
        }

        work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 0, sw2w_q);

        w2w_blk = xmalloc(sizeof *w2w_blk);
        w2w_blk->ybdec = YBdec_init();
      }
      else if (YB_DONE == ybret) {
        break;
      }
      else {
        log_fatal("%s: %s%s%s: data error while retrieving block: %s\n",
            pname, ispec->sep, ispec->fmt, ispec->sep, YBerr_detail(ybret));
      }
    } while (1);  /* in bzip2 */

    search = -1;
    do {  /* out bzip2 */
      if (0 == ibits_left) {
        if (s2w_blk->loaded / 4u == ipos) {
          if (sizeof s2w_blk->compr == s2w_blk->loaded) {
            log_fatal("%s: %s%s%s: missing bzip2 block header in full"
                " second input block\n", pname, ispec->sep, ispec->fmt,
                ispec->sep);
          }

          /* Terminated bzip2 block at end of short second input block. */
          xlock(&sw2w_q->proceed);
          assert(0 == s2w_blk->next);
          assert(sw2w_q->eof);
          work_release(s2w_blk, sw2w_q, w2m_q);

          work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 1, sw2w_q);
          goto again;
        }

      out_second:
        ibitbuf = ntohl(s2w_blk->compr[ipos]);
        ibits_left = 32u;
        ipos++;
      }

      bit = ibitbuf >> --ibits_left & 1u;
      search = (search << 1 | bit) & magic_mask;

    } while (magic_hdr != search);  /* out bzip2 */

    if ((size_t)((48u + ibits_left + 7u) / 8u) <= 4u * ipos) {
      xlock(&sw2w_q->proceed);
      work_release(s2w_blk, sw2w_q, w2m_q);
      work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 1, sw2w_q);
      goto again;
    }

    work_oflush(&w2w_blk, first_s2w_blk_id, &bzip2_blk_id, 0, sw2w_q);

    w2w_blk = xmalloc(sizeof *w2w_blk);
    w2w_blk->ybdec = YBdec_init();
  }  /* second block */
}


struct work_arg
{
  struct sw2w_q *sw2w_q;
  struct w2m_q *w2m_q;
  struct filespec *ispec;
};


static void *
work_wrap(void *v_work_arg)
{
  struct work_arg *work_arg;

  work_arg = v_work_arg;
  work(work_arg->sw2w_q, work_arg->w2m_q, work_arg->ispec);
  return 0;
}


static void
mux(struct w2m_q *w2m_q, struct m2s_q *m2s_q, struct filespec *ospec)
{
  struct pqueue reord;
  struct w2m_blk_nid reord_needed;
  unsigned working;

  reord_needed.s2w_blk_id = 0u;
  reord_needed.bzip2_blk_id = 0u;
  reord_needed.decompr_blk_id = 0u;
  pqueue_init(&reord, w2m_blk_cmp);

  xlock_pred(&w2m_q->av_or_ex_or_rel);
  do {
    struct w2m_blk *w2m_blk;
    unsigned num_rel;

    for (;;) {
      w2m_blk = w2m_q->head;
      working = w2m_q->working;
      num_rel = w2m_q->num_rel;

      if (0 != w2m_blk || 0u == working || 0u < num_rel) {
        break;
      }
      xwait(&w2m_q->av_or_ex_or_rel);
    }

    w2m_q->head = 0;
    w2m_q->num_rel = 0u;
    xunlock(&w2m_q->av_or_ex_or_rel);

    if (0u < num_rel) {
      xlock(&m2s_q->av);
      if (0u == m2s_q->num_free) {
        xsignal(&m2s_q->av);
      }
      m2s_q->num_free += num_rel;
      xunlock(&m2s_q->av);
    }

    /* Merge sub-blocks fetched this time into priority queue. */
    while (0 != w2m_blk) {
      struct w2m_blk *next;

      pqueue_insert(&reord, w2m_blk);

      next = w2m_blk->next;
      w2m_blk->next = 0;
      w2m_blk = next;
    }

    /*
      Write out initial continuous sequence of reordered sub-blocks. Go on
      until the queue becomes empty or the next sub-block is found to be
      missing.
    */
    while (!pqueue_empty(&reord)) {
      struct w2m_blk *reord_w2m_blk;

      reord_w2m_blk = pqueue_peek(&reord);
      if (!w2m_blk_id_eq(&reord_w2m_blk->id, &reord_needed)) {
        break;
      }

      /* Write out "reord_w2m_blk". */
      xwrite(ospec, reord_w2m_blk->decompr, reord_w2m_blk->produced);

      if (reord_w2m_blk->id.last_decompr) {
        if (reord_w2m_blk->id.w2w_blk_id.last_bzip2) {
          ++reord_needed.s2w_blk_id;
          reord_needed.bzip2_blk_id = 0u;
        }
        else {
          ++reord_needed.bzip2_blk_id;
        }

        reord_needed.decompr_blk_id = 0u;
      }
      else {
        ++reord_needed.decompr_blk_id;
      }

      /* Release "reord_w2m_blk". */
      pqueue_pop(&reord);
      free(reord_w2m_blk);
    }

    if (0u == working) {
      xlock(&w2m_q->av_or_ex_or_rel);
    }
    else {
      xlock_pred(&w2m_q->av_or_ex_or_rel);
    }

    w2m_q->needed = reord_needed;
  } while (0u < working);
  xunlock(&w2m_q->av_or_ex_or_rel);

  assert(0u == reord_needed.decompr_blk_id);
  assert(0u == reord_needed.bzip2_blk_id);
  pqueue_uninit(&reord);
}


static void
lbunzip2(unsigned num_worker, unsigned num_slot, int print_cctrs,
    struct filespec *ispec, struct filespec *ospec)
{
  struct sw2w_q sw2w_q;
  struct w2m_q w2m_q;
  struct m2s_q m2s_q;
  struct split_arg split_arg;
  pthread_t splitter;
  struct work_arg work_arg;
  pthread_t *worker;
  unsigned i;

  sw2w_q_init(&sw2w_q, num_worker);
  w2m_q_init(&w2m_q, num_worker);
  m2s_q_init(&m2s_q, num_slot);

  split_arg.m2s_q = &m2s_q;
  split_arg.sw2w_q = &sw2w_q;
  split_arg.ispec = ispec;
  xcreate(&splitter, split_wrap, &split_arg);

  work_arg.sw2w_q = &sw2w_q;
  work_arg.w2m_q = &w2m_q;
  work_arg.ispec = ispec;

  assert(0u < num_worker);
  assert(SIZE_MAX / sizeof *worker >= num_worker);
  worker = xmalloc(num_worker * sizeof *worker);
  for (i = 0u; i < num_worker; ++i) {
    xcreate(&worker[i], work_wrap, &work_arg);
  }

  mux(&w2m_q, &m2s_q, ospec);

  i = num_worker;
  do {
    xjoin(worker[--i]);
  } while (0u < i);
  free(worker);

  xjoin(splitter);

  if (print_cctrs) {
    log_info(
        "%s: %s%s%s: condvar counters:\n"
#define FW ((int)sizeof(long unsigned) * (int)CHAR_BIT / 3 + 1)
        "%s: any worker tried to consume from splitter or workers: %*lu\n"
        "%s: any worker stalled                                  : %*lu\n"
        "%s: muxer tried to consume from workers                 : %*lu\n"
        "%s: muxer stalled                                       : %*lu\n"
        "%s: splitter tried to consume from muxer                : %*lu\n"
        "%s: splitter stalled                                    : %*lu\n",
        pname, ispec->sep, ispec->fmt, ispec->sep,
        pname, FW, sw2w_q.proceed.ccount,
        pname, FW, sw2w_q.proceed.wcount,
        pname, FW, w2m_q.av_or_ex_or_rel.ccount,
        pname, FW, w2m_q.av_or_ex_or_rel.wcount,
        pname, FW, m2s_q.av.ccount,
        pname, FW, m2s_q.av.wcount
#undef FW
    );
  }

  m2s_q_uninit(&m2s_q, num_slot);
  w2m_q_uninit(&w2m_q);
  sw2w_q_uninit(&sw2w_q);
}


void *
lbunzip2_wrap(void *v_lbunzip2_arg)
{
  struct lbunzip2_arg *lbunzip2_arg;

  lbunzip2_arg = v_lbunzip2_arg;
  lbunzip2(
      lbunzip2_arg->num_worker,
      lbunzip2_arg->num_slot,
      lbunzip2_arg->print_cctrs,
      lbunzip2_arg->ispec,
      lbunzip2_arg->ospec
  );

  xraise(SIGUSR2);
  return 0;
}

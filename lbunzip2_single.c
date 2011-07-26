/* lbunzip2_single.c,v 1.17 2009/11/29 01:37:35 lacos Exp */

#include <assert.h>          /* assert() */
#include <unistd.h>          /* read() */
#include <errno.h>           /* errno */
#include <bzlib.h>           /* BZ2_bzDecompressInit() */
#include <signal.h>          /* SIGUSR2 */

#include "main.h"            /* pname */
#include "lbunzip2_single.h" /* lbunzip2_single() */


/* Splitter output granularity. */
#define MX_SPLIT ((size_t)(1024u * 1024u))

struct s2w_blk
{
  struct s2w_blk *next;          /* Next in list, ordered. */
  size_t present;                /* Number of bytes in compr, may be zero. */
  char unsigned compr[MX_SPLIT]; /* Compressed data. */
};


struct s2w_q            /* Splitter to single worker. */
{
  struct cond av;       /* Input block available. */
  struct s2w_blk *head, /* Ordered list of input blocks, short block is EOF. */
      *tail;
};


static void
s2w_q_init(struct s2w_q *s2w_q)
{
  xinit(&s2w_q->av);
  s2w_q->head = 0;
  s2w_q->tail = 0;
}


static void
s2w_q_uninit(struct s2w_q *s2w_q)
{
  assert(0 == s2w_q->tail);
  assert(0 == s2w_q->head);
  xdestroy(&s2w_q->av);
}


/* Worker output granularity. */
#define MX_DECOMPR ((size_t)(1024u * 1024u))

struct w2m_blk
{
  struct w2m_blk *next;              /* Next in list, ordered. */
  size_t produced;                   /* Number of bytes in decompr below. */
  char unsigned decompr[MX_DECOMPR]; /* Decompressed sub-block. */
};


struct w2m_q                   /* Single worker to muxer. */
{
  struct cond av_or_rel_or_ex; /* New w2m_blk/s2w_blk av. or worker exited. */
  struct w2m_blk *head,        /* Ordered list of sub-blocks. */
      *tail;
  unsigned num_rel;            /* Released s2w_blk's to return to splitter. */
  int ex;                      /* Worker exited, exit. */
};


static void
w2m_q_init(struct w2m_q *w2m_q)
{
  xinit(&w2m_q->av_or_rel_or_ex);
  w2m_q->head = 0;
  w2m_q->tail = 0;
  w2m_q->num_rel = 0u;
  w2m_q->ex = 0;
}


static void
w2m_q_uninit(struct w2m_q *w2m_q)
{
  assert(0 != w2m_q->ex);
  assert(0u == w2m_q->num_rel);
  assert(0 == w2m_q->tail);
  assert(0 == w2m_q->head);
  xdestroy(&w2m_q->av_or_rel_or_ex);
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
split(struct m2s_q *m2s_q, struct s2w_q *s2w_q, int infd, const char *isep,
    const char *ifmt)
{
  int first;
  size_t vacant;

  first = 1;
  do {
    struct s2w_blk *s2w_blk;

    xlock_pred(&m2s_q->av);
    while (0u == m2s_q->num_free) {
      xwait(&m2s_q->av);
    }
    --m2s_q->num_free;
    xunlock(&m2s_q->av);

    s2w_blk = xalloc(sizeof *s2w_blk);

    vacant = sizeof s2w_blk->compr;
    {
      ssize_t rd;

      do {
        rd = read(infd, s2w_blk->compr + (sizeof s2w_blk->compr - vacant),
            vacant > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : vacant);
      } while (0 < rd && 0u < (vacant -= (size_t)rd));

      /* Read error. */
      if (-1 == rd) {
        log_fatal("%s: read(%s%s%s): %s\n", pname, isep, ifmt, isep,
            err2str(errno));
      }
    }

    s2w_blk->next = 0;
    s2w_blk->present = sizeof s2w_blk->compr - vacant;

    if (first) {
      if (0u == s2w_blk->present) {
        log_fatal("%s: %s%s%s: file empty\n", pname, isep, ifmt, isep);
      }
      first = 0;
    }

    xlock(&s2w_q->av);
    if (0 == s2w_q->head) {
      assert(0 == s2w_q->tail);
      xsignal(&s2w_q->av);
      s2w_q->head = s2w_blk;
    }
    else {
      assert(0 != s2w_q->tail);
      s2w_q->tail->next = s2w_blk;
    }
    s2w_q->tail = s2w_blk;
    xunlock(&s2w_q->av);
  } while (0u == vacant);
}


struct split_arg
{
  struct m2s_q *m2s_q;
  struct s2w_q *s2w_q;
  int infd;
  const char *isep,
      *ifmt;
};


static void *
split_wrap(void *v_split_arg)
{
  struct split_arg *split_arg;

  split_arg = v_split_arg;
  split(split_arg->m2s_q, split_arg->s2w_q,
      split_arg->infd, split_arg->isep, split_arg->ifmt);
  return 0;
}

static void
work_locked_push(struct w2m_q *w2m_q, struct w2m_blk *w2m_blk)
{
  if (0 == w2m_q->head) {
    assert(0 == w2m_q->tail);
    w2m_q->head = w2m_blk;
  }
  else {
    assert(0 != w2m_q->tail);
    w2m_q->tail->next = w2m_blk;
  }
  w2m_q->tail = w2m_blk;
}


static void
work(struct s2w_q *s2w_q, struct w2m_q *w2m_q, const char *isep,
    const char *ifmt)
{
  int bzret;
  struct w2m_blk *w2m_blk;
  bz_stream strm;
  int full_s2w_blk;

  bzret = BZ_STREAM_END;
  w2m_blk = 0;
  do {
    struct s2w_blk *s2w_blk;
    size_t ileft;

    /* Grab next input block. */
    xlock_pred(&s2w_q->av);
    while (0 == s2w_q->head) {
      assert(0 == s2w_q->tail);
      xwait(&s2w_q->av);
    }
    assert(0 != s2w_q->tail);
    s2w_blk = s2w_q->head;
    s2w_q->head = s2w_blk->next;
    if (0 == s2w_q->head) {
      s2w_q->tail = 0;
    }
    xunlock(&s2w_q->av);

    full_s2w_blk = (sizeof s2w_blk->compr == s2w_blk->present);

    /* Feed entire input block to decompressor. */
    ileft = s2w_blk->present;
    while (0u < ileft || (!full_s2w_blk && BZ_OK == bzret && 0 == w2m_blk)) {
      /* Provide compressed input. */
      strm.next_in = (char *)(s2w_blk->compr + (s2w_blk->present - ileft));
      strm.avail_in = ileft;

      /* Provide output space. */
      if (0 == w2m_blk) {
        w2m_blk = xalloc(sizeof *w2m_blk);

        w2m_blk->next = 0;
        w2m_blk->produced = 0u;
      }
      strm.next_out = (char *)(w2m_blk->decompr + w2m_blk->produced);
      strm.avail_out = sizeof w2m_blk->decompr - w2m_blk->produced;

      /* (Re)initialize decompressor if necessary. */
      if (BZ_STREAM_END == bzret) {
        strm.bzalloc = lbzallocf;
        strm.bzfree = lbzfreef;
        strm.opaque = 0;

        bzret = BZ2_bzDecompressInit(
            &strm,
            0, /* verbosity */
            0  /* small */
        );
        assert(BZ_MEM_ERROR == bzret || BZ_OK == bzret);

        if (BZ_MEM_ERROR == bzret) {
          log_fatal("%s: %s%s%s: BZ2_bzDecompressInit(): BZ_MEM_ERROR\n",
              pname, isep, ifmt, isep);
        }
      }

      bzret = BZ2_bzDecompress(&strm);

      switch (bzret) {
#define CASE(x) case x: \
                  log_fatal("%s: %s%s%s: BZ2_bzDecompress(): " #x "\n", \
                      pname, isep, ifmt, isep)
        CASE(BZ_DATA_ERROR);
        CASE(BZ_DATA_ERROR_MAGIC);
        CASE(BZ_MEM_ERROR);
#undef CASE

        case BZ_STREAM_END:
        case BZ_OK:
          break;

        default:
          assert(0);
      }

      ileft = strm.avail_in;
      w2m_blk->produced = sizeof w2m_blk->decompr - strm.avail_out;

      /* Push decompressed sub-block if sub-block full. */
      if (0u == strm.avail_out) {
        int was_empty;

        xlock(&w2m_q->av_or_rel_or_ex);
        was_empty = (0 == w2m_q->head);
        work_locked_push(w2m_q, w2m_blk);
        if (was_empty && 0u == w2m_q->num_rel) {
          xsignal(&w2m_q->av_or_rel_or_ex);
        }
        xunlock(&w2m_q->av_or_rel_or_ex);

        w2m_blk = 0;
      }

      if (BZ_STREAM_END == bzret) {
        int tmp;

        tmp = BZ2_bzDecompressEnd(&strm);
        assert(BZ_OK == tmp);
      }
    }

    /* Release input block. */
    (*freef)(s2w_blk);
    xlock(&w2m_q->av_or_rel_or_ex);
    if (0u == w2m_q->num_rel++ && 0 == w2m_q->head) {
      xsignal(&w2m_q->av_or_rel_or_ex);
    }
    xunlock(&w2m_q->av_or_rel_or_ex);
  } while (full_s2w_blk);

  assert(BZ_STREAM_END == bzret || BZ_OK == bzret);
  if (BZ_OK == bzret) {
    log_fatal("%s: %s%s%s: premature EOF\n", pname, isep, ifmt, isep);
  }

  {
    int was_empty;

    xlock(&w2m_q->av_or_rel_or_ex);
    was_empty = (0 == w2m_q->head);
    if (0 != w2m_blk) {
      work_locked_push(w2m_q, w2m_blk);
    }
    w2m_q->ex = 1;
    if (was_empty && 0u == w2m_q->num_rel) {
      xsignal(&w2m_q->av_or_rel_or_ex);
    }
    xunlock(&w2m_q->av_or_rel_or_ex);
  }
}


struct work_arg
{
  struct s2w_q *s2w_q;
  struct w2m_q *w2m_q;
  const char *isep,
      *ifmt;
};


static void *
work_wrap(void *v_work_arg)
{
  struct work_arg *work_arg;

  work_arg = v_work_arg;
  work(work_arg->s2w_q, work_arg->w2m_q, work_arg->isep, work_arg->ifmt);
  return 0;
}


static void
mux(struct w2m_q *w2m_q, struct m2s_q *m2s_q, int outfd, const char *osep,
    const char *ofmt)
{
  for (;;) {
    struct w2m_blk *w2m_blk;
    unsigned num_rel;

    xlock_pred(&w2m_q->av_or_rel_or_ex);
    for (;;) {
      w2m_blk = w2m_q->head;
      num_rel = w2m_q->num_rel;

      if (0 != w2m_blk || 0u < num_rel || w2m_q->ex) {
        break;
      }
      xwait(&w2m_q->av_or_rel_or_ex);
    }

    w2m_q->head = 0;
    w2m_q->tail = 0;
    w2m_q->num_rel = 0u;
    xunlock(&w2m_q->av_or_rel_or_ex);

    if (0 == w2m_blk && 0u == num_rel) {
      break;
    }

    if (0u < num_rel) {
      xlock(&m2s_q->av);
      if (0u == m2s_q->num_free) {
        xsignal(&m2s_q->av);
      }
      m2s_q->num_free += num_rel;
      xunlock(&m2s_q->av);
    }

    while (0 != w2m_blk) {
      struct w2m_blk *next;

      if (-1 != outfd) {
        char unsigned *dp;
        size_t oleft;

        dp = w2m_blk->decompr;
        oleft = w2m_blk->produced;
        while (oleft > 0u) {
          ssize_t written;

          written = write(outfd, dp, oleft > (size_t)SSIZE_MAX
              ? (size_t)SSIZE_MAX : oleft);
          if (-1 == written) {
            log_fatal("%s: write(%s%s%s): %s\n", pname, osep, ofmt, osep,
                err2str(errno));
          }

          oleft -= (size_t)written;
          dp += written;
        }
      }

      next = w2m_blk->next;
      (*freef)(w2m_blk);
      w2m_blk = next;
    }
  }
}


static void
lbunzip2_single(unsigned num_slot, int print_cctrs, int infd, const char *isep,
    const char *ifmt, int outfd, const char *osep, const char *ofmt)
{
  struct s2w_q s2w_q;
  struct w2m_q w2m_q;
  struct m2s_q m2s_q;
  struct split_arg split_arg;
  pthread_t splitter;
  struct work_arg work_arg;
  pthread_t worker;

  s2w_q_init(&s2w_q);
  w2m_q_init(&w2m_q);
  m2s_q_init(&m2s_q, num_slot);

  split_arg.m2s_q = &m2s_q;
  split_arg.s2w_q = &s2w_q;
  split_arg.infd = infd;
  split_arg.isep = isep;
  split_arg.ifmt = ifmt;
  xcreate(&splitter, split_wrap, &split_arg);

  work_arg.s2w_q = &s2w_q;
  work_arg.w2m_q = &w2m_q;
  work_arg.isep = isep;
  work_arg.ifmt = ifmt;
  xcreate(&worker, work_wrap, &work_arg);

  mux(&w2m_q, &m2s_q, outfd, osep, ofmt);

  xjoin(worker);
  xjoin(splitter);

  if (print_cctrs) {
    log_info(
        "%s: %s%s%s: condvar counters:\n"
#define FW ((int)sizeof(long unsigned) * (int)CHAR_BIT / 3 + 1)
        "%s: worker tried to consume from splitter: %*lu\n"
        "%s: worker stalled                       : %*lu\n"
        "%s: muxer tried to consume from worker   : %*lu\n"
        "%s: muxer stalled                        : %*lu\n"
        "%s: splitter tried to consume from muxer : %*lu\n"
        "%s: splitter stalled                     : %*lu\n",
        pname, isep, ifmt, isep,
        pname, FW, s2w_q.av.ccount,
        pname, FW, s2w_q.av.wcount,
        pname, FW, w2m_q.av_or_rel_or_ex.ccount,
        pname, FW, w2m_q.av_or_rel_or_ex.wcount,
        pname, FW, m2s_q.av.ccount,
        pname, FW, m2s_q.av.wcount
#undef FW
    );
  }

  m2s_q_uninit(&m2s_q, num_slot);
  w2m_q_uninit(&w2m_q);
  s2w_q_uninit(&s2w_q);
}


void *
lbunzip2_single_wrap(void *v_lbunzip2_single_arg)
{
  struct lbunzip2_single_arg *lbunzip2_single_arg;

  lbunzip2_single_arg = v_lbunzip2_single_arg;
  lbunzip2_single(
      lbunzip2_single_arg->num_slot,
      lbunzip2_single_arg->print_cctrs,
      lbunzip2_single_arg->infd,
      lbunzip2_single_arg->isep,
      lbunzip2_single_arg->ifmt,
      lbunzip2_single_arg->outfd,
      lbunzip2_single_arg->osep,
      lbunzip2_single_arg->ofmt
  );

  xraise(SIGUSR2);
  return 0;
}

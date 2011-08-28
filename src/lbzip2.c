/* lbzip2.c,v 1.26 2009/12/20 19:24:06 lacos Exp */

#include <config.h>

#include <assert.h>       /* assert() */
#include <signal.h>       /* SIGUSR2 */
#include <sys/time.h>     /* gettimeofday() */
#include <unistd.h>       /* isatty() */

#include "yambi.h"        /* YBenc_t */

#include "main.h"         /* pname */
#include "lbzip2.h"       /* lbzip2() */
#include "lacos_rbtree.h" /* struct lacos_rbtree_node */


/*
  The "struct hack", originally employed below for s2w_blk.plain and
  w2m_blk.compr, is undefined behavior in C89. Thus it was a portability bug
  here. See Clive D.W. Feather's messages in the comp.lang.c.moderated Usenet
  newsgroup, with message identifiers <clcm-20091217-0002@plethora.net> and
  <clcm-20091217-0003@plethora.net>.

  Even though SUSv2 seems to advocate the struct hack in the msgrcv() and
  msgsnd() descriptions (see mymsg.mtext), I still rather don't apply it here.
  The original member declarations, now replaced by unnamed memory regions, are
  saved for documentation purposes. 20-Dec-2009 lacos
*/

struct s2w_blk            /* Splitter to workers. */
{
  uint64_t id;            /* Block serial number as read from infd. */
  struct s2w_blk *next;   /* Next in queue. */
  size_t loaded;          /* # of bytes in plain, may be 0 for 1st. */
#if 0
  char unsigned plain[1]; /* Data read from infd, allocated: sizeof_plain. */
#endif
};


struct s2w_q
{
  struct cond av_or_eof; /* New block available or splitter done. */
  struct s2w_blk *tail,  /* Splitter will append here. */
      *head;             /* Next ready worker shall compress this. */
  int eof;               /* Splitter done. */
};


static void
s2w_q_init(struct s2w_q *s2w_q)
{
  xinit(&s2w_q->av_or_eof);
  s2w_q->tail = 0;
  s2w_q->head = 0;
  s2w_q->eof = 0;
}


static void
s2w_q_uninit(struct s2w_q *s2w_q)
{
  assert(0 != s2w_q->eof);
  assert(0 == s2w_q->head);
  assert(0 == s2w_q->tail);
  xdestroy(&s2w_q->av_or_eof);
}


struct w2m_blk            /* Workers to muxer. */
{
  uint64_t id;            /* Block index as read from infd. */
  size_t uncompr_size;    /* Plain (uncompressed) data size. */
  struct w2m_blk *next;   /* Next block in list (unordered). */
  size_t n_subblocks;
  struct {
    void *buf;
    size_t size;
    YBcrc_t crc;
  } subblock[2];
};


static int
w2m_blk_cmp(const void *v_a, const void *v_b)
{
  uint64_t a,
      b;

  a = ((const struct w2m_blk *)v_a)->id;
  b = ((const struct w2m_blk *)v_b)->id;

  return
        a < b ? -1
      : a > b ?  1
      : 0;
}


struct w2m_q
{
  struct cond av_or_exit; /* New block available or all workers exited. */
  uint64_t needed;        /* Block needed for resuming writing. */
  struct w2m_blk *head;   /* Block list (unordered). */
  unsigned working;       /* Number of workers still running. */
};


static void
w2m_q_init(struct w2m_q *w2m_q, unsigned num_worker)
{
  assert(0u < num_worker);
  xinit(&w2m_q->av_or_exit);
  w2m_q->needed = 0u;
  w2m_q->head = 0;
  w2m_q->working = num_worker;
}


static void
w2m_q_uninit(struct w2m_q *w2m_q)
{
  assert(0u == w2m_q->working);
  assert(0 == w2m_q->head);
  xdestroy(&w2m_q->av_or_exit);
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
split(struct m2s_q *m2s_q, struct s2w_q *s2w_q, struct filespec *ispec,
    const size_t sizeof_plain)
{
  uint64_t id;
  int eof;

  id = 0u;
  do {
    struct s2w_blk *s2w_blk;
    size_t vacant;

    /* Grab a free slot. */
    xlock_pred(&m2s_q->av);
    while (0u == m2s_q->num_free) {
      xwait(&m2s_q->av);
    }
    --m2s_q->num_free;
    xunlock(&m2s_q->av);
    s2w_blk = xalloc(sizeof(struct s2w_blk) + sizeof_plain);

    /* Fill block. */
    vacant = sizeof_plain;
    xread(ispec, (char unsigned *)(s2w_blk + 1), &vacant);
    eof = (0 < vacant);

    if (sizeof_plain == vacant) {
      /* EOF on first read. */
      (*freef)(s2w_blk);
      xlock(&m2s_q->av);
      ++m2s_q->num_free;
      xunlock(&m2s_q->av);
    }
    else {
      s2w_blk->id = id;
      s2w_blk->next = 0;
      s2w_blk->loaded = sizeof_plain - vacant;
    }

    /* We either push a block, or set EOF, or both. */
    assert(sizeof_plain > vacant || eof);

    xlock(&s2w_q->av_or_eof);
    if (0 == s2w_q->head) {
      xbroadcast(&s2w_q->av_or_eof);
    }

    if (sizeof_plain > vacant) {
      if (0 == s2w_q->tail) {
        s2w_q->head = s2w_blk;
      }
      else {
        s2w_q->tail->next = s2w_blk;
      }
      s2w_q->tail = s2w_blk;
    }
    s2w_q->eof = eof;
    xunlock(&s2w_q->av_or_eof);

    /*
      If we didn't push a block, then this is bogus, but then we did set EOF,
      so it doesn't matter, because we'll leave immediately.
    */
    ++id;
  } while (!eof);
}


struct split_arg
{
  struct m2s_q *m2s_q;
  struct s2w_q *s2w_q;
  struct filespec *ispec;
  size_t sizeof_plain;
};


static void *
split_wrap(void *v_split_arg)
{
  struct split_arg *split_arg;

  split_arg = v_split_arg;

  split(
      split_arg->m2s_q,
      split_arg->s2w_q,
      split_arg->ispec,
      split_arg->sizeof_plain
  );
  return 0;
}


static void
work_compr(struct s2w_blk *s2w_blk, struct w2m_q *w2m_q, int bs100k,
    int exponential)
{
  struct w2m_blk *w2m_blk;
  char unsigned *ibuf;    /* pointer to the next input byte */
  size_t ileft;           /* input bytes left (not yet consumed) */
  size_t sub_i;           /* current subblock index */

  w2m_blk = xalloc(sizeof(struct w2m_blk));

  ibuf = (char unsigned *)(s2w_blk + 1);
  ileft = s2w_blk->loaded;
  assert(ileft > 0);

  sub_i = 0;
  do {
    size_t size;      /* compressed block size */
    size_t consumed;  /* number of input bytes consumed */
    void *buf;        /* allocated output buffer */
    YBenc_t *enc;     /* encoder */

    assert(sub_i <= 2);

    /*
      Allocate a yambi encoder with given block size and default parameters.
    */
    enc = YBenc_init(bs100k * 100000, exponential ? 0 : YB_DEFAULT_SHALLOW,
        YB_DEFAULT_PREFIX);

    /* Collect as much data as we can. */
    consumed = ileft;
    YBenc_collect(enc, ibuf, &ileft);
    consumed -= ileft;
    ibuf += consumed;

    /* Do the hard work. */
    size = YBenc_work(enc, &w2m_blk->subblock[sub_i].crc);

    /*
      Now we know the exact compressed block size. Allocate the output buffer
      and transmit the block into it.
    */
    buf = xalloc(size);
    YBenc_transmit(enc, buf);

    /* The encoder is no longer needed. Release memory. */
    YBenc_destroy(enc);

    w2m_blk->subblock[sub_i].buf = buf;
    w2m_blk->subblock[sub_i].size = size;
    sub_i++;
  } while (ileft > 0);

  w2m_blk->n_subblocks = sub_i;
  w2m_blk->id = s2w_blk->id;
  w2m_blk->uncompr_size = s2w_blk->loaded;

  /* Push block to muxer. */
  xlock(&w2m_q->av_or_exit);
  w2m_blk->next = w2m_q->head;
  w2m_q->head = w2m_blk;
  if (w2m_blk->id == w2m_q->needed) {
    xsignal(&w2m_q->av_or_exit);
  }
  xunlock(&w2m_q->av_or_exit);
}


static void
work(struct s2w_q *s2w_q, struct w2m_q *w2m_q, int bs100k, int exponential)
{
  for (;;) {
    struct s2w_blk *s2w_blk;

    /* Grab a block to work on. */
    xlock_pred(&s2w_q->av_or_eof);
    while (0 == s2w_q->head && !s2w_q->eof) {
      xwait(&s2w_q->av_or_eof);
    }
    if (0 == s2w_q->head) {
      /* No blocks available and splitter exited. */
      xunlock(&s2w_q->av_or_eof);
      break;
    }
    s2w_blk = s2w_q->head;
    s2w_q->head = s2w_blk->next;
    if (0 == s2w_q->head) {
      s2w_q->tail = 0;
    }
    xunlock(&s2w_q->av_or_eof);

    work_compr(s2w_blk, w2m_q, bs100k, exponential);
    (*freef)(s2w_blk);
  }

  /* Notify muxer when last worker exits. */
  xlock(&w2m_q->av_or_exit);
  if (0u == --w2m_q->working && 0 == w2m_q->head) {
    xsignal(&w2m_q->av_or_exit);
  }
  xunlock(&w2m_q->av_or_exit);
}


struct work_arg
{
  struct s2w_q *s2w_q;
  struct w2m_q *w2m_q;
  int bs100k,
      exponential;
};


static void *
work_wrap(void *v_work_arg)
{
  struct work_arg *work_arg;

  work_arg = v_work_arg;
  work(
      work_arg->s2w_q,
      work_arg->w2m_q,
      work_arg->bs100k,
      work_arg->exponential
  );
  return 0;
}


static void *
reord_alloc(size_t size, void *ignored)
{
  return xalloc(size);
}


static void
reord_dealloc(void *ptr, void *ignored)
{
  (*freef)(ptr);
}


/* If tv1 < tv2, return tv2 - tv1. Otherwise return 0. */
static double
timeval_diff(struct timeval tv1, struct timeval tv2)
{
  return (tv1.tv_sec <= tv2.tv_sec) ? tv2.tv_sec - tv1.tv_sec +
      0.000001 * ((double)tv2.tv_usec - (double)tv1.tv_usec) : 0;
}


static void
mux(struct w2m_q *w2m_q, struct m2s_q *m2s_q, struct filespec *ispec,
    struct filespec *ospec, int bs100k, int verbose)
{
  struct lacos_rbtree_node *reord;
  uint64_t reord_needed;
  char unsigned buffer[YB_HEADER_SIZE > YB_TRAILER_SIZE ? YB_HEADER_SIZE
      : YB_TRAILER_SIZE];
  YBobs_t *obs;
  off_t uncompr_total;
  struct timeval start_time,
      last_time;
  int progress;

  /*
    Progress info is displayed only if all the following conditions are met:
     1) the user has specified -v or --verbose option
     2) stderr is connected to a terminal device
     3) the input file is a regular file
     4) the input file is nonempty

    Although SUSv2-compliant gettimeofday() should always return 0,
    many systems document possibility of returning -1 on failure.
    The list of systems that may return nonzero values includes:
    GNU, Linux, NetBSD, FreeBSD, SysV, OSX, SunOS, z/OS, AIX, HP-UX.
    If we get a non-zero value, we simply don't display the progress info.
  */
  if ((progress = verbose && (0 < ispec->size) && isatty(STDERR_FILENO) &&
      0 == gettimeofday(&start_time, 0))) {
    last_time = start_time;
    log_info("%s: progress: %.2f%%\r", pname, 0.0);
  }

  /* Init obs and write out stream header. */
  obs = YBobs_init(100000 * bs100k, buffer);
  xwrite(ospec, buffer, YB_HEADER_SIZE);

  reord = 0;
  reord_needed = 0u;
  uncompr_total = 0;

  xlock_pred(&w2m_q->av_or_exit);
  for (;;) {
    struct w2m_blk *w2m_blk;

    /* Grab all available compressed blocks in one step. */
    while (0 == w2m_q->head && 0u < w2m_q->working) {
      xwait(&w2m_q->av_or_exit);
    }

    if (0 == w2m_q->head) {
      /* w2m_q is empty and all workers exited */
      break;
    }

    w2m_blk = w2m_q->head;
    w2m_q->head = 0;
    xunlock(&w2m_q->av_or_exit);

    /* Merge blocks fetched this time into tree. */
    do {
      int tmp;
      struct lacos_rbtree_node *new_node;
      struct w2m_blk *next;

      tmp = lacos_rbtree_insert(
          &reord,      /* new_root */
          &new_node,   /* new_node */
          w2m_blk,     /* new_data */
          w2m_blk_cmp, /* cmp() */
          reord_alloc, /* alloc() */
          0            /* alloc_ctl */
      );
      /* Identifier collision shouldn't happen, and see reord_alloc() too. */
      assert(0 == tmp);

      next = w2m_blk->next;
      w2m_blk->next = 0;
      w2m_blk = next;
    } while (0 != w2m_blk);

    /*
      Write out initial continuous sequence of reordered blocks. Go on until
      the tree becomes empty or the next block is found to be missing.
    */
    do {
      struct lacos_rbtree_node *reord_head;
      struct w2m_blk *reord_w2m_blk;
      size_t sub_i;

      reord_head = lacos_rbtree_min(reord);
      assert(0 != reord_head);

      reord_w2m_blk = *(void **)reord_head;
      if (reord_w2m_blk->id != reord_needed) {
        break;
      }

      /* Write out "reord_w2m_blk". */
      sub_i = 0;
      do {
        xwrite(ospec, reord_w2m_blk->subblock[sub_i].buf,
            reord_w2m_blk->subblock[sub_i].size);

        (*freef)(reord_w2m_blk->subblock[sub_i].buf);
        YBobs_join(obs, &reord_w2m_blk->subblock[sub_i].crc);
      } while (++sub_i < reord_w2m_blk->n_subblocks);

      ++reord_needed;

      xlock(&m2s_q->av);
      if (0u == m2s_q->num_free++) {
        xsignal(&m2s_q->av);
      }
      xunlock(&m2s_q->av);

      uncompr_total += reord_w2m_blk->uncompr_size;
      if (progress) {
        struct timeval time_now;

        if (0 == gettimeofday(&time_now, 0) &&
            timeval_diff(last_time, time_now) >= 0.1) {
          double completed,
              elapsed;

          last_time = time_now;
          elapsed = timeval_diff(start_time, time_now);
          completed = (double)uncompr_total / ispec->size;

          if (elapsed < 5)
            log_info("%s: progress: %.2f%%\r", pname, 100 * completed);
          else
            log_info("%s: progress: %.2f%%, ETA: %.0f s    \r",
                pname, 100 * completed, elapsed * (1 / completed - 1));
        }
      }

      lacos_rbtree_delete(
          &reord,        /* new_root */
          reord_head,    /* old_node */
          0,             /* old_data */
          reord_dealloc, /* dealloc() */
          0              /* alloc_ctl */
      );

      /* Release "reord_w2m_blk". */
      (*freef)(reord_w2m_blk);
    } while (0 != reord);
  
    xlock_pred(&w2m_q->av_or_exit);
    w2m_q->needed = reord_needed;
  }
  xunlock(&w2m_q->av_or_exit);

  /* Write out stream trailer. */
  YBobs_finish(obs, buffer);
  xwrite(ospec, buffer, YB_TRAILER_SIZE);

  YBobs_destroy(obs);

  assert(0 == reord);
}


static void
lbzip2(unsigned num_worker, unsigned num_slot, int print_cctrs,
    struct filespec *ispec, struct filespec *ospec, int bs100k, int verbose,
    int exponential)
{
  struct s2w_q s2w_q;
  struct w2m_q w2m_q;
  struct m2s_q m2s_q;
  struct split_arg split_arg;
  pthread_t splitter;
  struct work_arg work_arg;
  pthread_t *worker;
  unsigned i;

  assert(1 <= bs100k && bs100k <= 9);
  assert(exponential == !!exponential);
  assert(verbose == !!verbose);

  s2w_q_init(&s2w_q);
  w2m_q_init(&w2m_q, num_worker);
  m2s_q_init(&m2s_q, num_slot);

  if ((size_t)-1 < (unsigned)bs100k * 100000u) {
    log_fatal("%s: %s%s%s: size_t overflow in sizeof_plain\n", pname,
        ispec->sep, ispec->fmt, ispec->sep);
  }
  if ((size_t)-1 - sizeof(struct s2w_blk) < (unsigned)bs100k * 100000u) {
    log_fatal("%s: %s%s%s: size_t overflow in sizeof_s2w_blk\n", pname,
        ispec->sep, ispec->fmt, ispec->sep);
  }

  split_arg.m2s_q = &m2s_q;
  split_arg.s2w_q = &s2w_q;
  split_arg.ispec = ispec;
  split_arg.sizeof_plain = (unsigned)bs100k * 100000u;
  xcreate(&splitter, split_wrap, &split_arg);

  work_arg.s2w_q = &s2w_q;
  work_arg.w2m_q = &w2m_q;
  work_arg.bs100k = bs100k;
  work_arg.exponential = exponential;

  assert(0u < num_worker);
  assert((size_t)-1 / sizeof *worker >= num_worker);
  worker = xalloc(num_worker * sizeof *worker);
  for (i = 0u; i < num_worker; ++i) {
    xcreate(&worker[i], work_wrap, &work_arg);
  }

  mux(&w2m_q, &m2s_q, ispec, ospec, bs100k, verbose);

  i = num_worker;
  do {
    xjoin(worker[--i]);
  } while (0u < i);
  (*freef)(worker);

  xjoin(splitter);

  /*
    I know about the "%N$*M$lu" conversion specification, but the Tru64 system
    I tested on chokes on it, even though it is certified UNIX 98 (I believe):

    $ uname -s -r -v -m
    OSF1 V5.1 2650 alpha
    $ c89 -V
    Compaq C V6.5-011 on Compaq Tru64 UNIX V5.1B (Rev. 2650)
    Compiler Driver V6.5-003 (sys) cc Driver

    http://www.opengroup.org/openbrand/register/brand2700.htm
  */
  if (print_cctrs) {
    log_info(
        "%s: %s%s%s: condvar counters:\n"
#define FW ((int)sizeof(long unsigned) * (int)CHAR_BIT / 3 + 1)
        "%s: any worker tried to consume from splitter: %*lu\n"
        "%s: any worker stalled                       : %*lu\n"
        "%s: muxer tried to consume from workers      : %*lu\n"
        "%s: muxer stalled                            : %*lu\n"
        "%s: splitter tried to consume from muxer     : %*lu\n"
        "%s: splitter stalled                         : %*lu\n",
        pname, ispec->sep, ispec->fmt, ispec->sep,
        pname, FW, s2w_q.av_or_eof.ccount,
        pname, FW, s2w_q.av_or_eof.wcount,
        pname, FW, w2m_q.av_or_exit.ccount,
        pname, FW, w2m_q.av_or_exit.wcount,
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
lbzip2_wrap(void *v_lbzip2_arg)
{
  struct lbzip2_arg *lbzip2_arg;

  lbzip2_arg = v_lbzip2_arg;
  lbzip2(
      lbzip2_arg->num_worker,
      lbzip2_arg->num_slot,
      lbzip2_arg->print_cctrs,
      lbzip2_arg->ispec,
      lbzip2_arg->ospec,
      lbzip2_arg->bs100k,
      lbzip2_arg->verbose,
      lbzip2_arg->exponential
  );

  xraise(SIGUSR2);
  return 0;
}

/*-
  main.c -- main module

  Copyright (C) 2011, 2012 Mikolaj Izdebski
  Copyright (C) 2008, 2009, 2010 Laszlo Ersek

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <unistd.h>          /* unlink() */
#include <signal.h>          /* kill() */
#include <stdlib.h>          /* strtol() */
#include <stdarg.h>          /* va_list */
#include <stdio.h>           /* vfprintf() */
#include <string.h>          /* strcpy() */
#include <errno.h>           /* errno */
#include <assert.h>          /* assert() */
#include <sys/stat.h>        /* lstat() */
#include <fcntl.h>           /* open() */

#include "stat-time.h"       /* get_stat_atime() */
#include "utimens.h"         /* fdutimens() */
#include "xalloc.h"          /* xmalloc() */

#include "main.h"            /* pname */
#include "lbunzip2.h"        /* lbunzip2_wrap() */
#include "lbzip2.h"          /* lbzip2_wrap() */


#define EX_OK   0
#define EX_FAIL 1
#define EX_WARN 4


/* Utilities that can be called/accessed from multiple threads. */

/* (I) The treatment for fatal errors. */

static pthread_t main_thread;

/* Pathname of the current regular file being written to. */
static char *opathn;

static pid_t pid;


static void
__attribute__((noreturn))
bailout(void)
{
  sigset_t tmp_set;

  if (pthread_equal(pthread_self(), main_thread)) {
    if (0 != opathn) {
      (void)unlink(opathn);
    }

    if (0 == sigemptyset(&tmp_set) && 0 == sigaddset(&tmp_set, SIGPIPE)
        && 0 == sigaddset(&tmp_set, SIGXFSZ)) {
      (void)pthread_sigmask(SIG_UNBLOCK, &tmp_set, 0);
    }
  }
  else {
    if (0 == sigemptyset(&tmp_set) && 0 == sigpending(&tmp_set)) {
      int chk;

      chk = sigismember(&tmp_set, SIGPIPE);
      if (0 == chk || (1 == chk && 0 == kill(pid, SIGPIPE))) {
        chk = sigismember(&tmp_set, SIGXFSZ);
        if ((0 == chk || (1 == chk && 0 == kill(pid, SIGXFSZ)))
            && 0 == kill(pid, SIGUSR1)) {
          pthread_exit(0);
        }
      }
    }
  }

  _exit(EX_FAIL);
}


/* (II) Logging utilities. */

const char *pname;
static int warned;


static void
log_generic(const struct filespec *fs, int code, const char *fmt, va_list args,
    int nl) __attribute__((format(printf, 3, 0)));

static void
log_generic(const struct filespec *fs, int code, const char *fmt, va_list args,
    int nl)
{
  if (0 > fprintf(stderr, "%s: ", pname)
      || (fs && 0 > fprintf(stderr, "%s%s%s: ", fs->sep, fs->fmt, fs->sep))
      || 0 > vfprintf(stderr, fmt, args)
      || (0 != code && 0 > fprintf(stderr, ": %s", strerror(code)))
      || (nl && 0 > fprintf(stderr, "\n"))
      || 0 != fflush(stderr))
    bailout();
}


#define DEF(proto, f, x, warn, bail, nl)        \
  void proto                                    \
  {                                             \
    va_list args;                               \
                                                \
    flockfile(stderr);                          \
    va_start(args, fmt);                        \
    log_generic(f, x, fmt, args, nl);           \
                                                \
    if (!bail) {                                \
      va_end(args);                             \
      if (warn)                                 \
        warned = 1;                             \
      funlockfile(stderr);                      \
    }                                           \
    else                                        \
      bailout();                                \
  }

static void display(const char *fmt, ...) PRINTF_FMT(0);

DEF(info  (                                 const char *fmt, ...), 0,0,0,0,1)
DEF(infof (const struct filespec *f,        const char *fmt, ...), f,0,0,0,1)
DEF(infox (                          int x, const char *fmt, ...), 0,x,0,0,1)
DEF(infofx(const struct filespec *f, int x, const char *fmt, ...), f,x,0,0,1)
DEF(warn  (                                 const char *fmt, ...), 0,0,1,0,1)
DEF(warnf (const struct filespec *f,        const char *fmt, ...), f,0,1,0,1)
DEF(warnx (                          int x, const char *fmt, ...), 0,x,1,0,1)
DEF(warnfx(const struct filespec *f, int x, const char *fmt, ...), f,x,1,0,1)
DEF(fail  (                                 const char *fmt, ...), 0,0,0,1,1)
DEF(failf (const struct filespec *f,        const char *fmt, ...), f,0,0,1,1)
DEF(failx (                          int x, const char *fmt, ...), 0,x,0,1,1)
DEF(failfx(const struct filespec *f, int x, const char *fmt, ...), f,x,0,1,1)
DEF(display( /* WITH NO ADVANCING :) */     const char *fmt, ...), 0,0,0,0,0)

#undef DEF


/* Called when one of xalloc functions fails. */
void
xalloc_die(void)
{
  failx(errno, "xalloc");
}


void
progress_init(struct progress *p, int verbose, uintmax_t file_size)
{
  /*
    Progress info is displayed only if all the following conditions are met:
     1) the user has specified -v or --verbose option
     2) stderr is connected to a terminal device
     3) the input file is a regular file
     4) the input file is nonempty
  */
  p->enabled = (verbose && 0u < file_size && isatty(STDERR_FILENO));
  if (p->enabled) {
    p->size = file_size;
    p->processed = 0u;
    gettime(&p->start_time);
    p->next_time = p->start_time;
    display("progress: %.2f%%\r", 0.0);
  }
}


void
progress_update(struct progress *p, uintmax_t chunk_size)
{
  struct timespec time_now;
  double completed,
      elapsed;
  static const double UPDATE_INTERVAL = 0.1;

  if (!p->enabled)
    return;

  p->processed += chunk_size;
  assert(p->size >= p->processed);

  gettime(&time_now);
  if (0 < timespec_cmp(p->next_time, time_now))
    return;

  p->next_time = timespec_add(time_now, dtotimespec(UPDATE_INTERVAL));
  elapsed = timespectod(timespec_sub(time_now, p->start_time));
  completed = (double)p->processed / p->size;

  if (elapsed < 5)
    display("progress: %.2f%%\r", 100 * completed);
  else
    display("progress: %.2f%%, ETA: %.0f s    \r", 100 * completed,
        elapsed * (1 / completed - 1));
}


void
progress_finish(struct progress *p)
{
  if (!p->enabled)
    return;

  assert(p->size == p->processed);
}


/* (III) Threading utilities. */

static void
failxc(int x, const char *fname)
{
  if (0 != x) {
    failx(x, "%s", fname);
  }
}

#define pthread(fn, args) failxc(pthread_ ## fn args, "pthread_" #fn "()")


void
xinit(struct cond *cond)
{
  pthread_mutexattr_t attr;

  pthread(mutexattr_init, (&attr));
  pthread(mutexattr_settype, (&attr, PTHREAD_MUTEX_ERRORCHECK));
  pthread(mutex_init, (&cond->lock, &attr));
  pthread(mutexattr_destroy, (&attr));
  pthread(cond_init, (&cond->cond, 0));

  cond->ccount = 0lu;
  cond->wcount = 0lu;
}


void
xdestroy(struct cond *cond)
{
  pthread(cond_destroy, (&cond->cond));
  pthread(mutex_destroy, (&cond->lock));
}


void
xlock(struct cond *cond)
{
  pthread(mutex_lock, (&cond->lock));
}


void
xlock_pred(struct cond *cond)
{
  xlock(cond);
  ++cond->ccount;
}


void
xunlock(struct cond *cond)
{
  pthread(mutex_unlock, (&cond->lock));
}


void
xwait(struct cond *cond)
{
  ++cond->wcount;
  pthread(cond_wait, (&cond->cond, &cond->lock));
  ++cond->ccount;
}


void
xsignal(struct cond *cond)
{
  pthread(cond_signal, (&cond->cond));
}


void
xbroadcast(struct cond *cond)
{
  pthread(cond_broadcast, (&cond->cond));
}


void
xcreate(pthread_t *thread, void *(*routine)(void *), void *arg)
{
  pthread(create, (thread, 0, routine, arg));
}


void
xjoin(pthread_t thread)
{
  pthread(join, (thread, 0));
}


void
xraise(int sig)
{
  if (-1 == kill(pid, sig)) {
    failx(errno, "kill()");
  }
}


/* (IV) File I/O utilities. */

void
xread(struct filespec *ispec, char unsigned *buffer, size_t *vacant)
{
  assert(0 < *vacant);

  do {
    ssize_t rd;

    rd = read(ispec->fd, buffer, *vacant > (size_t)SSIZE_MAX ?
        (size_t)SSIZE_MAX : *vacant);

    /* End of file. */
    if (0 == rd)
      break;

    /* Read error. */
    if (-1 == rd) {
      failfx(ispec, errno, "read()");
    }

    *vacant -= (size_t)rd;
    buffer += (size_t)rd;
    ispec->total += (size_t)rd;
  } while (0 < *vacant);
}


void
xwrite(struct filespec *ospec, const char unsigned *buffer, size_t size)
{
  assert(0 < size);
  ospec->total += size;

  if (-1 != ospec->fd) {
    do {
      ssize_t wr;

      wr = write(ospec->fd, buffer, size > (size_t)SSIZE_MAX ?
          (size_t)SSIZE_MAX : size);

      /* Write error. */
      if (-1 == wr) {
        failfx(ospec, errno, "write()");
      }

      size -= (size_t)wr;
      buffer += (size_t)wr;
    } while (0 < size);
  }
}


/* Private stuff, only callable/accessible for the main thread. */

enum outmode
{
  OM_STDOUT,  /* Write all output to stdout, -c. */
  OM_DISCARD, /* Discard output, -t. */
  OM_REGF     /* Write output to files; neither of -t/-c present. */
};

struct opts
{
  unsigned num_worker;  /* Start this many worker threads, -n. */
  enum outmode outmode; /* How to store output, -c/-t. */
  int decompress,       /* Run in bunzip2 mode, -d/-z. */
      bs100k,           /* Block size switch for compression, -1 .. -9. */
      force,            /* Open anything / break links / remove output, -f. */
      keep,             /* Don't rm FILE oprnds / open rf with >1 links, -k. */
      verbose,          /* Print a msg. each time when starting a muxer, -v. */
      print_cctrs;      /* Print condition variable counters, -S. */
};

/* Backlog factor for all workers together. */
static const unsigned blf = 4u;

/* Names of other recognized environment variables. */
static const char * const ev_name[] = { "LBZIP2", "BZIP2", "BZIP" };

/* Separator characters in environment variable values. No escaping. */
static const char envsep[] = " \t";


static long
xstrtol(const char *str, const char *source, long lower, long upper)
{
  long tmp;
  char *endptr;

  errno = 0;
  tmp = strtol(str, &endptr, 10);
  if ('\0' == *str || '\0' != *endptr || 0 != errno
      || tmp < lower || tmp > upper) {
    fail("failed to parse \"%s\" from %s as a long in [%ld..%ld],"
        " specify \"-h\" for help\n", str, source, lower, upper);
  }

  return tmp;
}


/*
  The usage message, displayed when user gives us `--help' option.

  The following macro definition was generated by pretty-usage.pl script.
  To alter the message, simply edit and run pretty-usage.pl. It will patch
  the macro definition automatically.
*/
#define USAGE_STRING "%s%s%s%s%s%s", "Usage:\n1. PROG [-n WTHRS] [-k|-c|-t] [-\
d|-z] [-1 .. -9] [-f] [-v] [-S] {FILE}\n2. PROG -h|-V\n\nRecognized PROG names\
:\n\n  bunzip2, lbunzip2  : Decompress. Forceable with `-d'.\n  bzcat, lbzcat \
     : Decompress to stdout. Forceable with `-cd'.\n  <otherwise>        : Com\
press. Forceable with `-z'.\n\nEnvironment variables:\n\n  LBZIP2, BZIP2,\n  B\
ZIP               : Insert arguments between PROG and the rest of the\n       \
                command line. Tokens are separated by spaces and tabs;\n      \
                ", " no escaping.\n\nOptions:\n\n  -n WTHRS           : Set th\
e number of (de)compressor threads to WTHRS, where\n                       WTH\
RS is a positive integer.\n  -k, --keep         : Don't remove FILE operands. \
Open regular input files\n                       with more than one link.\n  -\
c, --stdout       : Write output to stdout even with FILE operands. Implies\n \
                      `-k'. Incompatible with `-t'.\n  -t, --test         : Te\
st decompression; discard output instead of writing it\n                ", "  \
     to files or stdout. Implies `-k'. Incompatible with\n                    \
   `-c'.\n  -d, --decompress   : Force decompression over the selection by PRO\
G.\n  -z, --compress     : Force compression over the selection by PROG.\n  -1\
 .. -9           : Set the compression block size to 100K .. 900K.\n  --fast  \
           : Alias for `-1'.\n  --best             : Alias for `-9'. This is t\
he default.\n  -f, --force        : Open non-regular input files. Open input f\
iles with more\n                       than one", " link. Try to remove each o\
utput file before\n                       opening it.\n  -v, --verbose      : \
Log each (de)compression start to stderr. Display\n                       comp\
ression ratio and space savings. Display progress\n                       info\
rmation if stderr is connected to a terminal.\n  -S                 : Print co\
ndition variable statistics to stderr.\n  -s, --small, -q,\n  --quiet,\n  --re\
petitive-fast,\n  --repetitive-best,\n  --exponential      : Accepted for comp\
atibility, otherwise ign", "ored.\n  -h, --help         : Print this help to s\
tdout and exit.\n  -L, --license, -V,\n  --version          : Print version in\
formation to stdout and exit.\n\nOperands:\n\n  FILE               : Specify f\
iles to compress or decompress. If no FILE is\n                       given, w\
ork as a filter. FILEs with `.bz2', `.tbz',\n                       `.tbz2' an\
d `.tz2' name suffixes will be skipped when\n                       compressin\
g. When decompressing, `.bz2' suffixes will be\n                       removed\
 i", "n output filenames; `.tbz', `.tbz2' and `.tz2'\n                       s\
uffixes will be replaced by `.tar'; other filenames\n                       wi\
ll be suffixed with `.out'.\n"


static void _Noreturn
usage(void)
{
  if (0 > printf(USAGE_STRING))
    failx(errno, "printf()");
  if (0 != fclose(stdout))
    failx(errno, "fclose(stdout)");
  _exit(EX_OK);
}


static void _Noreturn
version(void)
{
  if (0 > printf("%s version %s\n\n%s%s", PACKAGE_NAME, PACKAGE_VERSION,
      "Copyright (C) 2011, 2012 Mikolaj Izdebski\n"
      "Copyright (C) 2008, 2009, 2010 Laszlo Ersek\n"
      "\n"
      "This program is free software: you can redistribute it and/or modify\n"
      "it under the terms of the GNU General Public License as published by\n"
      "the Free Software Foundation, either version 3 of the License, or\n"
      "(at your option) any later version.\n"
      "\n",
      "This program is distributed in the hope that it will be useful,\n"
      "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
      "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
      "GNU General Public License for more details.\n"
      "\n"
      "You should have received a copy of the GNU General Public License\nal"
      "ong with this program.  If not, see <http://www.gnu.org/licenses/>.\n"))
    failx(errno, "printf()");
  if (0 != fclose(stdout))
    failx(errno, "fclose(stdout)");
  _exit(EX_OK);
}


struct arg
{
  struct arg *next;
  const char *val;
};


static void
opts_outmode(struct opts *opts, char ch)
{
  assert('c' == ch || 't' == ch);
  if (('c' == ch ? OM_DISCARD : OM_STDOUT) == opts->outmode) {
    fail("\"-c\" and \"-t\" are incompatible, specify \"-h\" for help");
  }
  if ('c' == ch) {
    opts->outmode = OM_STDOUT;
  }
  else {
    opts->outmode = OM_DISCARD;
    opts->decompress = 1;
  }
}


static void
opts_decompress(struct opts *opts, char ch)
{
  assert('d' == ch || 'z' == ch);
  opts->decompress = ('d' == ch);
  if (OM_DISCARD == opts->outmode) {
    opts->outmode = OM_REGF;
  }
}


static void
opts_setup(struct opts *opts, struct arg **operands, size_t argc, char **argv)
{
  struct arg **link_at;
  long mx_worker;

  /*
    Create a homogeneous argument list from the recognized environment
    variables and from the command line.
  */
  *operands = 0;
  link_at = operands;
  {
    size_t ofs;

    for (ofs = 0u; ofs < sizeof ev_name / sizeof ev_name[0]; ++ofs) {
      char *ev_val;

      ev_val = getenv(ev_name[ofs]);
      if (0 != ev_val) {
        char *tok;

        for (tok = strtok(ev_val, envsep); 0 != tok; tok = strtok(0, envsep)) {
          struct arg *arg;

          arg = xmalloc(sizeof *arg);
          arg->next = 0;
          arg->val = tok;
          *link_at = arg;
          link_at = &arg->next;
        }
      }
    }

    for (ofs = 1u; ofs < argc; ++ofs) {
      struct arg *arg;

      arg = xmalloc(sizeof *arg);
      arg->next = 0;
      arg->val = argv[ofs];
      *link_at = arg;
      link_at = &arg->next;
    }
  }


  /* Effectuate option defaults. */
#ifdef _SC_THREAD_THREADS_MAX
  mx_worker = sysconf(_SC_THREAD_THREADS_MAX);
  if (-1L == mx_worker)
#endif
    mx_worker = LONG_MAX;
  if (UINT_MAX < (long unsigned)mx_worker) {
    mx_worker = UINT_MAX;
  }
  if (UINT_MAX / blf < (unsigned)mx_worker) {
    mx_worker = UINT_MAX / blf;
  }
  if (SIZE_MAX / sizeof(pthread_t) < (unsigned)mx_worker) {
    mx_worker = SIZE_MAX / sizeof(pthread_t);
  }

  opts->num_worker = 0u;
  opts->outmode = OM_REGF;
  opts->decompress = 0;
  if (0 == strcmp("bunzip2", pname) || 0 == strcmp("lbunzip2", pname)) {
    opts->decompress = 1;
  }
  else {
    if (0 == strcmp("bzcat", pname) || 0 == strcmp("lbzcat", pname)) {
      opts->outmode = OM_STDOUT;
      opts->decompress = 1;
    }
  }
  opts->bs100k = 9;
  opts->force = 0;
  opts->keep = 0;
  opts->verbose = 0;
  opts->print_cctrs = 0;


  /*
    Process and remove all arguments that are options or option arguments. The
    remaining arguments are the operands.
  */
  link_at = operands;
  {
    enum
    {
      AS_CONTINUE,  /* Continue argument processing. */
      AS_STOP,      /* Processing finished because of "--". */
      AS_USAGE,     /* Processing finished because user asked for help. */
      AS_VERSION    /* Processing finished because user asked for version. */
    } args_state;
    struct arg *arg,
        *next;

    args_state = AS_CONTINUE;
    for (arg = *operands; 0 != arg && AS_CONTINUE == args_state; arg = next) {
      const char *argscan;

      argscan = arg->val;
      if ('-' != *argscan) {
        /* This is an operand, keep it. */
        link_at = &arg->next;
        next = arg->next;
      }
      else {
        /* This argument holds options and possibly an option argument. */
        ++argscan;

        if ('-' == *argscan) {
          ++argscan;

          if ('\0' == *argscan) {
            args_state = AS_STOP;
          }
          else if (0 == strcmp("stdout", argscan)) {
            opts_outmode(opts, 'c');
          }
          else if (0 == strcmp("test", argscan)) {
            opts_outmode(opts, 't');
          }
          else if (0 == strcmp("decompress", argscan)) {
            opts_decompress(opts, 'd');
          }
          else if (0 == strcmp("compress", argscan)) {
            opts_decompress(opts, 'z');
          }
          else if (0 == strcmp("fast", argscan)) {
            opts->bs100k = 1;
          }
          else if (0 == strcmp("best", argscan)) {
            opts->bs100k = 9;
          }
          else if (0 == strcmp("force", argscan)) {
            opts->force = 1;
          }
          else if (0 == strcmp("keep", argscan)) {
            opts->keep = 1;
          }
          else if (0 == strcmp("verbose", argscan)) {
            opts->verbose = 1;
          }
          else if (0 == strcmp("help", argscan)) {
            args_state = AS_USAGE;
          }
          else if (0 == strcmp("license", argscan)
              || 0 == strcmp("version", argscan)) {
            args_state = AS_VERSION;
          }
          else if (0 != strcmp("small", argscan)
              && 0 != strcmp("quiet", argscan)
              && 0 != strcmp("repetitive-fast", argscan)
              && 0 != strcmp("repetitive-best", argscan)
              && 0 != strcmp("exponential", argscan)) {
            fail("unknown option \"%s\", specify \"-h\" for help", arg->val);
          }
        } /* long option */
        else {
          int cont;

          cont = 1;
          do {
            switch (*argscan) {
              case '\0': cont = 0; break;

              case 'c': case 't':
                opts_outmode(opts, *argscan);
                break;

              case 'd': case 'z':
                opts_decompress(opts, *argscan);
                break;

              case '1': case '2': case '3': case '4': case '5': case '6':
              case '7': case '8': case '9':
                opts->bs100k = *argscan - '0';
                break;

              case 'f': opts->force = 1; break;

              case 'k': opts->keep = 1; break;

              case 'v': opts->verbose = 1; break;

              case 'S': opts->print_cctrs = 1; break;

              case 's': case 'q':
                break;

              case 'h':
                args_state = AS_USAGE;
                cont = 0;
                break;

              case 'L': case 'V':
                args_state = AS_VERSION;
                cont = 0;
                break;

              case 'n':
                ++argscan;

                if ('\0' == *argscan) {
                  /* Drop this argument, as it wasn't an operand. */
                  next = arg->next;
                  free(arg);
                  *link_at = next;

                  /* Move to next argument, which is an option argument. */
                  arg = next;
                  if (0 == arg) {
                    fail("option \"-%.1s\" requires an argument,"
                        " specify \"-h\" for help", argscan - 1);
                  }
                  argscan = arg->val;
                }

                opts->num_worker = xstrtol(argscan, "\"-n\"", 1L, mx_worker);
                cont = 0;
                break;

              default:
                fail("unknown option \"-%.1s\", specify \"-h\" for"
                    " help", argscan);
            } /* switch (*argscan) */

            ++argscan;
          } while (cont);
        } /* cluster of short options */

        /* This wasn't an operand, drop it. */
        next = arg->next;
        free(arg);
        *link_at = next;
      } /* argument holds options */
    } /* arguments loop */

    if (AS_USAGE == args_state || AS_VERSION == args_state) {
      for (arg = *operands; 0 != arg; arg = next) {
        next = arg->next;
        free(arg);
      }
      if (AS_USAGE == args_state)
        usage();
      else
        version();
    }
  } /* process arguments */


  /* Finalize options. */
  if (OM_REGF == opts->outmode && 0 == *operands) {
    opts->outmode = OM_STDOUT;
  }

  if (opts->decompress) {
    if (0 == *operands && isatty(STDIN_FILENO)) {
      fail("won't read compressed data from a terminal, specify"
          " \"-h\" for help");
    }
  }
  else {
    if (OM_STDOUT == opts->outmode && isatty(STDOUT_FILENO)) {
      fail("won't write compressed data to a terminal, specify"
          " \"-h\" for help");
    }
  }

  if (0u == opts->num_worker) {
#ifdef _SC_NPROCESSORS_ONLN
    long num_online;

    num_online = sysconf(_SC_NPROCESSORS_ONLN);
    if (-1 == num_online) {
      fail("number of online processors unavailable, specify \"-h\""
          " for help");
    }
    assert(1L <= num_online);
    opts->num_worker = (mx_worker < num_online) ? mx_worker : num_online;
#else
    fail("WORKER-THREADS not set, specify \"-h\" for help");
#endif
  }
}


static void
xsigemptyset(sigset_t *set)
{
  if (-1 == sigemptyset(set)) {
    failx(errno, "sigemptyset()");
  }
}


static void
xsigaddset(sigset_t *set, int signo)
{
  if (-1 == sigaddset(set, signo)) {
    failx(errno, "sigaddset()");
  }
}


static void
xsigmask(int how, const sigset_t *set, sigset_t *oset)
{
  pthread(sigmask, (how, set, oset));
}


static void
xsigaction(int sig, void (*handler)(int))
{
  struct sigaction act;

  act.sa_handler = handler;
  xsigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  if (-1 == sigaction(sig, &act, 0)) {
    failx(errno, "sigaction()");
  }
}


enum caught_sig
{
  CS_INT = 1,
  CS_TERM,
  CS_USR1,
  CS_USR2
};

static volatile sig_atomic_t caught_sig;


static void
sighandler(int sig)
{
  /* sig_atomic_t is nowhere required to be able to hold signal values. */
  switch (sig) {
    case SIGINT:
      caught_sig = CS_INT;
      break;

    case SIGTERM:
      caught_sig = CS_TERM;
      break;

    case SIGUSR1:
      caught_sig = CS_USR1;
      break;

    case SIGUSR2:
      caught_sig = CS_USR2;
      break;

    default:
      assert(0);
  }
}


/*
  Dual purpose:
  a) Is the current operand already compressed?
  b) What decompressed suffix corresponds to the current compressed suffix?
*/
struct suffix
{
  const char *compr;   /* Suffix of compressed file. */
  size_t compr_len;    /* Its length (not size). */
  const char *decompr; /* Suffix of decompressed file. */
  size_t decompr_len;  /* Its length (not size). */
  int chk_compr;       /* Whether "compr" is suited for purpose "a". */
};

#define SUF(c, dc, c1) { c, sizeof c - 1u, dc, sizeof dc - 1u, c1 }
static const struct suffix suffix[]= {
    SUF(".bz2",  "",     1),
    SUF(".tbz2", ".tar", 1),
    SUF(".tbz",  ".tar", 1),
    SUF(".tz2",  ".tar", 1),
    SUF("",      ".out", 0)
};
#undef SUF


/*
  If "decompr_pathname" is NULL: check if "compr_pathname" has a compressed
  suffix. If "decompr_pathname" is not NULL: allocate and format a pathname
  for storing the decompressed output -- this always returns 1.
*/
static int
suffix_xform(const char *compr_pathname, char **decompr_pathname)
{
  size_t len,
      ofs;

  len = strlen(compr_pathname);
  for (ofs = 0u; ofs < sizeof suffix / sizeof suffix[0]; ++ofs) {
    if ((suffix[ofs].chk_compr || 0 != decompr_pathname)
        && len >= suffix[ofs].compr_len) {
      size_t prefix_len;

      prefix_len = len - suffix[ofs].compr_len;
      if (0 == strcmp(compr_pathname + prefix_len, suffix[ofs].compr)) {
        if (0 != decompr_pathname) {
          if (SIZE_MAX - prefix_len < suffix[ofs].decompr_len + 1u) {
            fail("\"%s\": size_t overflow in dpn_alloc\n", compr_pathname);
          }
          *decompr_pathname
              = xmalloc(prefix_len + suffix[ofs].decompr_len + 1u);
          (void)memcpy(*decompr_pathname, compr_pathname, prefix_len);
          (void)strcpy(*decompr_pathname + prefix_len, suffix[ofs].decompr);
        }
        return 1;
      }
    }
  }
  assert(0 == decompr_pathname);
  return 0;
}


/*
  If input is unavailable (skipping), return -1.

  Otherwise:
    - return 0,
    - store the file descriptor to read from (might be -1 if discarding),
    - if input is coming from a successfully opened FILE operand, fill in
      "*sbuf" via fstat() -- but "*sbuf" may be modified without this, too,
    - set up "ispec->sep" and "ispec->fmt" for logging; the character arrays
      pointed to by them won't need to be released (or at least not through
      these aliases).
*/
static int
input_init(const struct arg *operand, enum outmode outmode, int decompress,
    int force, int keep, struct stat *sbuf, struct filespec *ispec)
{
  ispec->total = 0u;

  if (0 == operand) {
    ispec->fd  = STDIN_FILENO;
    ispec->sep = "";
    ispec->fmt = "stdin";
    ispec->size = 0u;
    return 0;
  }

  if (!force) {
    if (-1 == lstat(operand->val, sbuf)) {
      warnx(errno, "skipping \"%s\": lstat()", operand->val);
      return -1;
    }

    if (!S_ISREG(sbuf->st_mode)) {
      warn("skipping \"%s\": not a regular file", operand->val);
      return -1;
    }

    if (OM_REGF == outmode && !keep && sbuf->st_nlink > (nlink_t)1) {
      warn("skipping \"%s\": more than one links", operand->val);
      return -1;
    }
  }

  if (!decompress && suffix_xform(operand->val, 0)) {
    warn("skipping \"%s\": compressed suffix", operand->val);
  }
  else {
    int infd;

    infd = open(operand->val, O_RDONLY | O_NOCTTY);
    if (-1 == infd) {
      warnx(errno, "skipping \"%s\": open()", operand->val);
    }
    else {
      if (-1 != fstat(infd, sbuf)) {
        ispec->fd  = infd;
        ispec->sep = "\"";
        ispec->fmt = operand->val;
        assert(0 <= sbuf->st_size);
        ispec->size = sbuf->st_size;
        return 0;
      }

      warnx(errno, "skipping \"%s\": fstat()", operand->val);
      if (-1 == close(infd)) {
        failx(errno, "close(\"%s\")", operand->val);
      }
    }
  }

  return -1;
}


static void
input_oprnd_rm(const struct arg *operand)
{
  assert(0 != operand);

  if (-1 == unlink(operand->val) && ENOENT != errno) {
    warnx(errno, "unlink(\"%s\")", operand->val);
  }
}


static void
input_uninit(struct filespec *ispec)
{
  if (-1 == close(ispec->fd)) {
    failx(errno, "close(%s%s%s)", ispec->sep, ispec->fmt, ispec->sep);
  }
}


/*
  If skipping (output unavailable), return -1.

  Otherwise:
    - return 0,
    - store the file descriptor to write to (might be -1 if discarding),
    - if we write to a regular file, store the dynamically allocated output
      pathname,
    - set up "ospec->sep" and "ospec->fmt" for logging; the character arrays
      pointed to by them won't need to be released (or at least not through
      these aliases).
*/
static int
output_init(const struct arg *operand, enum outmode outmode, int decompress,
    int force, const struct stat *sbuf, struct filespec *ospec,
    char **output_pathname)
{
  ospec->total = 0u;

  switch (outmode) {
    case OM_STDOUT:
      ospec->fd  = STDOUT_FILENO;
      ospec->sep = "";
      ospec->fmt = "stdout";
      return 0;

    case OM_DISCARD:
      ospec->fd  = -1;
      ospec->sep = "";
      ospec->fmt = "the bit bucket";
      return 0;

    case OM_REGF:
      assert(0 != operand);

      {
        char *tmp;

        if (decompress) {
          (void)suffix_xform(operand->val, &tmp);
        }
        else {
          size_t len;

          len = strlen(operand->val);
          if (SIZE_MAX - sizeof ".bz2" < len) {
            fail("\"%s\": size_t overflow in cpn_alloc\n", operand->val);
          }
          tmp = xmalloc(len + sizeof ".bz2");
          (void)memcpy(tmp, operand->val, len);
          (void)strcpy(tmp + len, ".bz2");
        }

        if (force && -1 == unlink(tmp) && ENOENT != errno) {
          /*
            This doesn't warrant a warning in itself, just an explanation if
            the following open() fails.
          */
          infox(errno, "unlink(\"%s\")", tmp);
        }

        ospec->fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL,
            sbuf->st_mode & (S_IRUSR | S_IWUSR));

        if (-1 == ospec->fd) {
          warnx(errno, "skipping \"%s\": open(\"%s\")", operand->val, tmp);
          free(tmp);
        }
        else {
          *output_pathname = tmp;
          ospec->sep = "\"";
          ospec->fmt = tmp;
          return 0;
        }
      }
      break;

    default:
      assert(0);
  }

  return -1;
}


static void
output_regf_uninit(int outfd, const struct stat *sbuf, char **output_pathname)
{
  assert(0 != *output_pathname);

  if (-1 == fchown(outfd, sbuf->st_uid, sbuf->st_gid)) {
    /* File stays with euid:egid, and at most 0600. */
    warnx(errno, "fchown(\"%s\")", *output_pathname);
  }
  else {
    if (sbuf->st_mode & (S_ISUID | S_ISGID | S_ISVTX)) {
      warn("\"%s\": won't restore any of setuid, setgid, sticky",
          *output_pathname);
    }

    if (-1 == fchmod(outfd, sbuf->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO))) {
      /* File stays with orig-uid:orig-gid, and at most 0600. */
      warnx(errno, "fchmod(\"%s\")", *output_pathname);
    }
  }

  {
    struct timespec ts[2];

    ts[0] = get_stat_atime(sbuf);
    ts[1] = get_stat_mtime(sbuf);

    if (-1 == fdutimens(outfd, *output_pathname, ts)) {
      warnx(errno, "fdutimens(\"%s\")", *output_pathname);
    }
  }

  if (-1 == close(outfd)) {
    failx(errno, "close(\"%s\")", *output_pathname);
  }

  free(*output_pathname);
  *output_pathname = 0;
}


static void
process(const struct opts *opts, unsigned num_slot, struct filespec *ispec,
    struct filespec *ospec, const sigset_t *unblocked)
{
  /*
    We could wait for signals with either sigwait() or sigsuspend(). SUSv2
    states about sigwait() that its effect on signal actions is unspecified.
    SUSv3 still claims the same.

    The SUSv2 description of sigsuspend() talks about both the thread and the
    whole process being suspended until a signal arrives, although thread
    suspension seems much more likely from the wording. They note that they
    filed a clarification request for this. SUSv3 cleans this up and chooses
    thread suspension which was more logical anyway.

    I favor sigsuspend() because I need to re-raise SIGTERM and SIGINT, and
    unspecified action behavior with sigwait() seems messy.

    13-OCT-2009 lacos
  */

  union
  {
    struct lbunzip2_arg lbunzip2;
    struct lbzip2_arg   lbzip2;
  } muxer_arg;
  pthread_t muxer;

  if (opts->verbose) {
    info(opts->decompress ? "decompressing %s%s%s to %s%s%s" :
        "compressing %s%s%s to %s%s%s", ispec->sep, ispec->fmt, ispec->sep,
        ospec->sep, ospec->fmt, ospec->sep);
  }

  if (opts->decompress) {
    muxer_arg.lbunzip2.num_worker = opts->num_worker;
    muxer_arg.lbunzip2.num_slot = num_slot;
    muxer_arg.lbunzip2.print_cctrs = opts->print_cctrs;
    muxer_arg.lbunzip2.ispec = ispec;
    muxer_arg.lbunzip2.ospec = ospec;
    muxer_arg.lbunzip2.verbose = opts->verbose;
    xcreate(&muxer, lbunzip2_wrap, &muxer_arg.lbunzip2);
  }
  else {
    muxer_arg.lbzip2.num_worker = opts->num_worker;
    muxer_arg.lbzip2.num_slot = num_slot;
    muxer_arg.lbzip2.print_cctrs = opts->print_cctrs;
    muxer_arg.lbzip2.ispec = ispec;
    muxer_arg.lbzip2.ospec = ospec;
    muxer_arg.lbzip2.bs100k = opts->bs100k;
    muxer_arg.lbzip2.verbose = opts->verbose;
    xcreate(&muxer, lbzip2_wrap, &muxer_arg.lbzip2);
  }

  /* Unblock signals, wait for them, then block them again. */
  {
    int ret;

    ret = sigsuspend(unblocked);
    assert(-1 == ret && EINTR == errno);
  }

  switch (caught_sig) {
    case CS_INT:
    case CS_TERM:
      if (0 != opathn) {
        (void)unlink(opathn);
        /*
          Don't release "opathn" -- the muxer might encounter a write error and
          access *opathn via "ofmt" for error reporting before the main thread
          re-raises the signal here. This is a deliberate leak, but we're on
          our (short) way out anyway. 16-Feb-2010 lacos
        */
        opathn = 0;
      }

      {
        int sig;
        sigset_t mask;

        sig = (CS_INT == caught_sig) ? SIGINT : SIGTERM;
        /*
          We might have inherited a SIG_IGN from the parent, but that would
          make no sense here. 24-OCT-2009 lacos
        */
        xsigaction(sig, SIG_DFL);
        xraise(sig);

        xsigemptyset(&mask);
        xsigaddset(&mask, sig);
        xsigmask(SIG_UNBLOCK, &mask, 0);
      }
      /*
        We shouldn't reach this point, but if we do for some reason, fall
        through.
      */

    case CS_USR1:
      /* Error from a non-main thread via bailout(). */
      bailout();

    case CS_USR2:
      /* Muxer thread joined other sub-threads and finished successfully. */
      break;

    default:
      assert(0);
  }

  xjoin(muxer);
}


static void
sigs_mod(int block_n_catch, sigset_t *oset)
{
  void (*handler)(int);

  if (block_n_catch) {
    sigset_t mask;

    xsigemptyset(&mask);
    xsigaddset(&mask, SIGINT);
    xsigaddset(&mask, SIGTERM);
    xsigaddset(&mask, SIGUSR1);
    xsigaddset(&mask, SIGUSR2);
    xsigmask(SIG_BLOCK, &mask, oset);

    handler = sighandler;
  }
  else {
    handler = SIG_DFL;
  }

  xsigaction(SIGINT,  handler);
  xsigaction(SIGTERM, handler);
  xsigaction(SIGUSR1, handler);
  xsigaction(SIGUSR2, handler);

  if (!block_n_catch) {
    xsigmask(SIG_SETMASK, oset, 0);
  }
}


int
main(int argc, char **argv)
{
  struct opts opts;
  struct arg *operands;
  unsigned num_slot;
  static char stderr_buf[BUFSIZ];

  main_thread = pthread_self();
  pid = getpid();
  pname = strrchr(argv[0], '/');
  pname = pname ? pname + 1 : argv[0];
  setbuf(stderr, stderr_buf);

  /*
    SIGPIPE and SIGXFSZ will be blocked in all sub-threads during the entire
    lifetime of the process. Any EPIPE or EFBIG write() condition will be
    handled just as before lbzip2-0.23, when these signals were ignored.
    However, starting with lbzip2-0.23, SIGPIPE and/or SIGXFSZ will be
    generated for the offending thread(s) in addition. bailout(), when called
    by such sub-threads in response to EPIPE or EFBIG, or when called by other
    sub-threads failing concurrently (but a bit later) for any other reason,
    will forward (regenerate) the pending signal(s) for the whole process. The
    main thread will unblock these signals right before exiting with EX_FAIL in
    bailout(). 2010-03-03 lacos
  */
  {
    sigset_t base_mask;

    xsigemptyset(&base_mask);
    xsigaddset(&base_mask, SIGPIPE);
    xsigaddset(&base_mask, SIGXFSZ);
    xsigmask(SIG_BLOCK, &base_mask, 0);
  }

  opts_setup(&opts, &operands, argc, argv);

  assert(UINT_MAX / blf >= opts.num_worker);
  num_slot = opts.num_worker * blf;

  do {
    /* Process operand. */
    {
      int ret;
      struct stat instat;
      struct filespec ispec;

      ret = input_init(operands, opts.outmode, opts.decompress, opts.force,
          opts.keep, &instat, &ispec);
      if (-1 != ret) {
        sigset_t unblocked;
        struct filespec ospec;

        sigs_mod(1, &unblocked);
        if (-1 != output_init(operands, opts.outmode, opts.decompress,
            opts.force, &instat, &ospec, &opathn)) {
          process(&opts, num_slot, &ispec, &ospec, &unblocked);

          if (OM_REGF == opts.outmode) {
            output_regf_uninit(ospec.fd, &instat, &opathn);
            if (!opts.keep) {
              input_oprnd_rm(operands);
            }
          }

          /*
            Display data compression ratio and space savings, but only
            if the user desires so.
          */
          if (opts.verbose && 0u < ispec.total && 0u < ospec.total) {
            uintmax_t plain_size,
                compr_size;
            double ratio,
                savings,
                ratio_magnitude;

            /*
              Do the math. Note that converting from uintmax_t to double
              *may* result in precision loss, but that shouldn't matter.
            */
            plain_size = !opts.decompress ? ispec.total : ospec.total;
            compr_size = ispec.total ^ ospec.total ^ plain_size;
            ratio = (double)compr_size / plain_size;
            savings = 1 - ratio;
            ratio_magnitude = ratio < 1 ? 1 / ratio : ratio;

            infof(&ispec, "compression ratio is %s%.3f%s, space savings is "
                "%.2f%%\n", ratio < 1 ? "1:" : "", ratio_magnitude,
                ratio < 1 ? "" : ":1", 100 * savings);
          }
        } /* output available or discarding */
        sigs_mod(0, &unblocked);

        input_uninit(&ispec);
      } /* input available */
    }

    /* Move to next operand. */
    if (0 != operands) {
      struct arg *next;

      next = operands->next;
      free(operands);
      operands = next;
    }
  } while (0 != operands);

  assert(0 == opathn);
  if (OM_STDOUT == opts.outmode && -1 == close(STDOUT_FILENO)) {
    failx(errno, "close(stdout)");
  }

  _exit(warned ? EX_WARN : EX_OK);
}

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

#ifndef MAIN_H
#  define MAIN_H

#  include <limits.h>    /* CHAR_BIT */
#  include <stddef.h>    /* size_t */
#  include <pthread.h>   /* pthread_mutex_t */
#  include <inttypes.h>  /* uint64_t */
#  include <sys/types.h> /* off_t */

#  if 8 != CHAR_BIT
#    error "Environments where 8 != CHAR_BIT are not supported."
#  endif


/* Utilities that can be called/accessed from multiple threads. */

/*
  (I) The treatment for fatal errors.

  bailout() doesn't need to be extern.

  If called from the main thread, remove any current output file and bail out.
  Primarily by unblocking any pending SIGPIPE/SIGXFSZ signals; both those
  forwarded by any sub-thread to the process level, and those generated for the
  main thread specifically, in response to an EPIPE/EFBIG write() condition. If
  no such signal is pending, or SIG_IGN was inherited through exec() as their
  actions, then bail out with the failure exit status.

  If called (indirectly) from any other thread, resignal the process with any
  pending SIGPIPE/SIGXFSZ. This will promote any such signal to the process
  level, if it was originally generated for the calling thread, accompanying an
  EPIPE/EFBIG write() condition. If the pending signal was already pending on
  the whole process, this will result in an idempotent kill(). Thereafter, send
  SIGUSR1 to the process, in order to signal the fatal error in the sub-thread.
  Finally, terminate the thread.
*/


/* (II) Logging utilities. */

/*
  Name of the executable, for logging purposes. Set up by the main thread
  before starting any threads.
*/
extern const char *pname;

/*
  Return a static string corresponding to the error number. strerror_r() is not
  SUSv2.
*/
const char *
err2str(int err);

/*
  Format a message to standard error. If the underlying vfprintf() fails, call
  bailout().
*/
void
log_info(const char *fmt, ...)
#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
;

/* Same as log_info(), but always call bailout(). */
void
log_fatal(const char *fmt, ...)
#ifdef __GNUC__
__attribute__((format(printf, 1, 2), noreturn))
#endif
;


/* (III) Memory allocation. */

/* Allocate memory. If unsuccessful, call log_fatal(). */
void *
xalloc(size_t size);

/*
  Memory deallocation function pointer, set up by the main thread before
  starting any threads. It may call log_info().
*/
extern void (*freef)(void *ptr);


/*
  (IV) Threading utilities. If they fail, they call log_fatal().
*/

struct cond
{
  pthread_mutex_t lock; /* Lock this to protect shared resource. */
  pthread_cond_t cond;  /* Trigger this if predicate becomes true. */
  long unsigned ccount, /* Increment this when checking predicate. */
      wcount;           /* Increment this when waiting is necessary. */
};

void
xinit(struct cond *cond);

void
xdestroy(struct cond *cond);

void
xlock(struct cond *cond);

void
xlock_pred(struct cond *cond);

void
xunlock(struct cond *cond);

void
xwait(struct cond *cond);

void
xsignal(struct cond *cond);

void
xbroadcast(struct cond *cond);

void
xcreate(pthread_t *thread, void *(*routine)(void *), void *arg);

void
xjoin(pthread_t thread);

void
xraise(int sig);


/*
  (V) File I/O utilities. If they fail, they call log_fatal().
*/

/*
  The file specifier.

  The pointers "sep" and "fmt" point to character arrays that either don't
  need to be released, or need to be released through different aliases.
  These are prepared solely for logging. This is why the pointed to chars
  are qualified as const.
*/
struct filespec
{
  int fd;           /* the file descriptor; may be -1 if discarding output */
  const char *sep,  /* name separator; either "" or "\"" */
      *fmt;         /* either file name or a special name, like stdin */
  uint64_t total;   /* total number of bytes transfered from/to this file */
  off_t size;       /* file size or 0 if unknown */
};

void
xread(struct filespec *ispec, char unsigned *buffer, size_t *vacant);

void
xwrite(struct filespec *ospec, const char unsigned *buffer, size_t size);


#endif

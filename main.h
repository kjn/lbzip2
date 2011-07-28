/* main.h,v 1.13 2010/03/03 00:57:58 lacos Exp */

#ifndef MAIN_H
#  define MAIN_H

#  include <limits.h>  /* CHAR_BIT */
#  include <stddef.h>  /* size_t */
#  include <pthread.h> /* pthread_mutex_t */

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
__attribute__((format(printf, 1, 2)))
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

#endif

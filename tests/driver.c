/*-
  driver.c -- test harness driver

  Copyright (C) 2014, 2015 Mikolaj Izdebski

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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define MAX_CASES 1000000

struct test_case {
  struct test_case *next;
  int id;
  const char *name;
  const char *suite_name;
  enum { RUN, OK, FAIL } status;
  char *message;
};


/* Like GNU's vasprintf(), but _exits on failure. */
static char *
xvasprintf(const char *fmt, va_list ap)
{
  char *str;
  size_t n;
  va_list aq;

  for (n = 100;; n += n) {
    va_copy(aq, ap);
    str = malloc(n);
    if (str == NULL) {
      perror("malloc");
      _exit(2);
    }
    str[n-1] = 'x';
    if (vsnprintf(str, n, fmt, aq) == -1) {
      perror("vsnprintf");
      _exit(2);
    }
    if (str[n-1] != '\0') {
      return str;
    }
    free(str);
    va_end(aq);
  }
}

/* Like printf(), but _exits on failure. */
static void
xprintf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);

  if (vprintf(fmt, ap) == -1) {
    perror("vprintf");
    _exit(2);
  }

  va_end(ap);
}

/* Bail out current test suite with given message. */
static void
t_error(const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = xvasprintf(fmt, ap);
  va_end(ap);

  xprintf("Bail out! %s\n", msg);
  free(msg);
  _exit(1);
}

/* Fail current test case with given message. */
static void
t_fail(struct test_case *tc, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  tc->message = xvasprintf(fmt, ap);
  va_end(ap);

  tc->status = FAIL;
}

/* Succeed current test case. */
static void
t_succeed(struct test_case *tc)
{
  tc->status = OK;
  tc->message = NULL;
}


/* Close file descriptor, bail out on failure. */
static void
xclose(int fd)
{
  if (close(fd) != 0) {
    t_error("failed to close file descriptor");
  }
}

/* Duplicate file descriptor, bail out on failure. */
static void
xdup2(int fd1, int fd2)
{
  if (dup2(fd1, fd2) < 0) {
    t_error("failed to dup2 file descriptor");
  }
}

/* Return file size as returned by fstat, bail out on failure. */
static off_t
xfstat_size(int fd)
{
  struct stat buf;

  if (fstat(fd, &buf) != 0) {
    t_error("failed to fstat file descriptor");
  }

  return buf.st_size;
}

/* Mmap file into memory, bail out on failure. */
static void *
xmmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
  void *ptr;

  ptr = mmap(addr, len, prot, flags, fd, off);
  if (ptr == MAP_FAILED) {
    t_error("failed to map file into memory");
  }

  return ptr;
}

/* Unmap memory region, bail out on failure. */
static void
xmunmap(void *ptr, size_t len)
{
  if (munmap(ptr, len) != 0) {
    t_error("failed to unmap memory region");
  }
}

/* Rename file, bail out on failure. */
static void
xrename(const char *old, const char *new)
{
  if (rename(old, new) != 0) {
    t_error("Unable to rename file %s to %s", old, new);
  }
}

/* Return file's base name. */
static char *
xbasename(const char *path)
{
  char *bn;

  bn = strrchr(path, '/');
  if (bn == NULL) {
    return strchr(path, *path);
  }

  return bn + 1;
}

/* Open file for reading, bail out on failure. */
static int
open_rd(const char *fn)
{
  int fd;

  fd = open(fn, O_RDONLY);
  if (fd == -1) {
    t_error("unable to open file for reading: %s", fn);
  }

  return fd;
}

/* Open file for writing, bail out on failure.  Creates file if not
   exist.  File is truncated. */
static int
open_wr(const char *fn)
{
  int fd;

  fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd == -1) {
    t_error("unable to open file for writing: %s", fn);
  }

  return fd;
}

/* Return 1 if given regular file exists. */
static int
file_exists(const char *fn)
{
  int fd;

  fd = open(fn, O_RDONLY);
  if (fd != -1) {
    xclose(fd);
    return 1;
  }
  if (errno != ENOENT) {
    t_error("unable to open file for reading: %s", fn);
  }
  return 0;
}

static const char *
signal_name(int sig)
{
  switch (sig) {
  case SIGABRT: return "SIGABRT";
  case SIGALRM: return "SIGALRM";
  case SIGBUS: return "SIGBUS";
  case SIGCHLD: return "SIGCHLD";
  case SIGCONT: return "SIGCONT";
  case SIGFPE: return "SIGFPE";
  case SIGHUP: return "SIGHUP";
  case SIGILL: return "SIGILL";
  case SIGINT: return "SIGINT";
  case SIGKILL: return "SIGKILL";
  case SIGPIPE: return "SIGPIPE";
  case SIGPOLL: return "SIGPOLL";
  case SIGPROF: return "SIGPROF";
  case SIGQUIT: return "SIGQUIT";
  case SIGSEGV: return "SIGSEGV";
  case SIGSTOP: return "SIGSTOP";
  case SIGSYS: return "SIGSYS";
  case SIGTERM: return "SIGTERM";
  case SIGTRAP: return "SIGTRAP";
  case SIGTSTP: return "SIGTSTP";
  case SIGTTIN: return "SIGTTIN";
  case SIGTTOU: return "SIGTTOU";
  case SIGURG: return "SIGURG";
  case SIGUSR1: return "SIGUSR1";
  case SIGUSR2: return "SIGUSR2";
  case SIGVTALRM: return "SIGVTALRM";
  case SIGXCPU: return "SIGXCPU";
  case SIGXFSZ: return "SIGXFSZ";
  default: return "unknown";
  }
}


/* Concatenate all string arguments into newly allocated string, until
   NULL argument.  Caller is responsible for releasing memory. */
static char *
t_concat(const char *msg, ...)
{
  va_list ap;
  va_list aq;
  const char *p;
  char *q;
  size_t len;

  va_start(ap, msg);
  va_copy(aq, ap);

  p = msg;
  len = 1;
  do {
    len += strlen(p);
    p = va_arg(ap, const char *);
  } while (p != NULL);

  q = malloc(len);
  if (q == NULL) {
    t_error("out of memory");
  }
  p = msg;
  len = 0;
  do {
    strcpy(q + len, p);
    len += strlen(p);
    p = va_arg(aq, const char *);
  } while (p != NULL);

  va_end(ap);
  va_end(aq);

  return q;
}


/* Execute subprocess and wait for its termination.  Standard streams
   are connected to files.  Return process status as obtained from
   wait. */
static int
t_exec(const char *path, char *argv[],
       const char *in, const char *out, const char *err)
{
  int in_fd;
  int out_fd;
  int err_fd;
  pid_t pid;
  int status;
  char *const envp[1] = {NULL};

  in_fd = open_rd(in);
  out_fd = open_wr(out);
  err_fd = open_wr(err);

  pid = fork();

  if (pid == 0) {
    xdup2(in_fd, STDIN_FILENO);
    xdup2(out_fd, STDOUT_FILENO);
    xdup2(err_fd, STDERR_FILENO);
  }

  xclose(in_fd);
  xclose(out_fd);
  xclose(err_fd);

  if (pid == 0) {
    argv[0] = xbasename(path);
    (void)execve(path, argv, envp);
    _exit(66);
  }

  for (;;) {
    /* TODO: add timeout */
    if (waitpid(pid, &status, 0) == pid) {
      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 66) {
          t_error("failed to execve %s", path);
        }
        return status;
      }
    }
  }
}


/* Compare two given files and fail test case if they differ. */
static int
t_compare(struct test_case *tc, const char *exp, const char *act)
{
  int exp_fd;
  int act_fd;
  off_t size;
  void *exp_ptr;
  void *act_ptr;
  int ret = 1;

  exp_fd = open_rd(exp);
  act_fd = open_rd(act);

  size = xfstat_size(exp_fd);
  if (xfstat_size(act_fd) != size) {
    t_fail(tc, "files differ in size; expected: %s, actual: %s", exp, act);
    ret = 1;
  }
  else {
    exp_ptr = xmmap(0, size, PROT_READ, MAP_SHARED, exp_fd, 0);
    act_ptr = xmmap(0, size, PROT_READ, MAP_SHARED, act_fd, 0);

    if (memcmp(exp_ptr, act_ptr, size) != 0) {
      t_fail(tc, "files differ; expected: %s, actual: %s", exp, act);
      ret = 1;
    }
    else {
      ret = 0;
    }

    xmunmap(exp_ptr, size);
    xmunmap(act_ptr, size);
  }

  xclose(exp_fd);
  xclose(act_fd);
}


/* Run compression test case. */
static void
t_compress(struct test_case *tc)
{
  char *args[2] = {NULL, NULL};

  int fd;

  char *in;
  char *zin;
  char *out;
  char *zout;
  char *zexp;
  char *err;

  int status;

  in = t_concat(tc->suite_name, "/", tc->name, ".raw", NULL);
  zin = t_concat(tc->suite_name, "/", tc->name, ".bz2", NULL);
  out = t_concat(tc->suite_name, "/", tc->name, ".out", NULL);
  zout = t_concat(tc->suite_name, "/", tc->name, ".zout", NULL);
  zexp = t_concat(tc->suite_name, "/", tc->name, ".zexp", NULL);
  err = t_concat(tc->suite_name, "/", tc->name, ".err", NULL);

  do {
    if (!file_exists(in)) {
      status = t_exec("./minbzcat", args, zin, out, err);
      if (WIFSIGNALED(status)) {
        t_error("minbzcat was killed by signal %d (%s)",
                WTERMSIG(status), signal_name(WTERMSIG(status)));
      }
      if (WEXITSTATUS(status) != 0) {
        t_error("minbzcat failed with exit code %d", WEXITSTATUS(status));
      }
      xrename(out, in);
    }
    status = t_exec("../src/lbzip2", args, in, zout, err);
    if (WIFSIGNALED(status)) {
      t_fail(tc, "lbzip2 was killed by signal %d (%s)",
             WTERMSIG(status), signal_name(WTERMSIG(status)));
      break;
    }
    if (WEXITSTATUS(status) != 0) {
      t_fail(tc, "lbzip2 failed with exit code %d", WEXITSTATUS(status));
      break;
    }
    fd = open_rd(err);
    if (xfstat_size(fd) != 0) {
      t_fail(tc, "lbzip2 printed message on standard error");
      break;
    }
    xclose(fd);
    if (file_exists(zexp)) {
      if (t_compare(tc, zexp, zout)) {
        break;
      }
    }
    else {
      status = t_exec("./minbzcat", args, zout, out, err);
      if (WIFSIGNALED(status)) {
        t_error("minbzcat was killed by signal %d (%s)",
                WTERMSIG(status), signal_name(WTERMSIG(status)));
      }
      if (WEXITSTATUS(status) != 0) {
        t_error("minbzcat failed with exit code %d", WEXITSTATUS(status));
      }
      if (t_compare(tc, in, out)) {
        break;
      }
      xrename(zout, zexp);
    }

    t_succeed(tc);
  }
  while (0);

  free(in);
  free(zin);
  free(out);
  free(zout);
  free(zexp);
  free(err);
}


/* Compare strings using strcmp.  Used in qsort. */
static int
string_cmp(const void *va, const void *vb)
{
  const char *const *pa = va;
  const char *const *pb = vb;
  return strcmp(*pa, *pb);
}

/* Read entire test suite into memory. */
static void
read_suite(char **t_suite, const char *suite_name)
{
  DIR *dp;
  struct dirent *ep;
  int i;
  int j;

  dp = opendir(suite_name);
  if (dp == NULL) {
    t_error("Failed to open directory: %s", suite_name);
  }
  j = 0;
  while ((ep = readdir(dp)) != NULL) {
    if (j >= MAX_CASES) {
      t_error("Test suites lagrer than %u test cases are not supported", MAX_CASES);
    }
    for (i = 0; i < 40; i++) {
      if (!isxdigit(ep->d_name[i]))
        break;
    }
    if (i < 40 || strcmp(ep->d_name + 40, ".bz2")) {
      continue;
    }
    t_suite[j] = t_concat(ep->d_name, NULL);
    t_suite[j][40] = '\0';
    j++;
  }
  if (j == 0) {
    t_error("No test cases found in specified directory: %s", suite_name);
  }
  t_suite[j] = NULL;
  if (closedir(dp) != 0) {
    t_error("Failed to close directory: %s", suite_name);
  }
  xprintf("1..%d\n", j);
  qsort(t_suite, j, sizeof(char *), string_cmp);
}


/* Run specified test suite. */
int
main(int argc, char **argv)
{
  char **case_name;
  const char *suite_name = argv[1];
  int last_case_id = 0;
  static char *suite[MAX_CASES+1];

  (void)setlocale(LC_CTYPE, "C");  /* for isxdigit() */
  (void)setvbuf(stdout, NULL, _IONBF, 0);  /* for real-time test progress */

  if (argc != 2) {
    t_error("This program accepts exactly one argument: path to test suite");
  }

  read_suite(suite, suite_name);

  for (case_name = suite; *case_name != NULL; case_name++) {
    struct test_case test_case;
    struct test_case *tc = &test_case;
    tc->status = RUN;
    tc->id = ++last_case_id;
    tc->name = *case_name;
    tc->suite_name = suite_name;
    t_compress(tc);
    if (tc->status == OK) {
      xprintf("ok %d %s\n", tc->id, tc->name);
    }
    else if (tc->status == FAIL) {
      xprintf("not ok %d %s %s\n", tc->id, tc->name, tc->message);
    }
    else {
      abort();
    }
  }
}

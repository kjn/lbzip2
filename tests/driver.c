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


static const char *base_dir;
static const char *suite_name;
static const char *case_name;


/* Like fprintf(stderr, ...), but _exits on failure. */
static void
xprintf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);

  if (vfprintf(stderr, fmt, ap) == -1) {
    perror("vfprintf");
    _exit(2);
  }

  va_end(ap);
}


/* Bail out current test suite with given message. */
static void
t_error(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  _exit(2);
}

/* Fail current test case with given message. */
static void
t_fail(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");

  _exit(1);
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

/* Remove file, bail out on failure. */
static void
xunlink(const char *path)
{
  if (unlink(path) != 0) {
    t_error("Unable to remove file %s", path);
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

static void
xmkdir(const char *path) {
  if (mkdir(path, 0755) != 0 && errno != EEXIST) {
    t_error("Unable to create directory %s", path);
  }
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
static void
t_compare(const char *exp, const char *act)
{
  int exp_fd;
  int act_fd;
  off_t size;
  void *exp_ptr;
  void *act_ptr;

  exp_fd = open_rd(exp);
  act_fd = open_rd(act);

  size = xfstat_size(exp_fd);
  if (xfstat_size(act_fd) != size) {
    t_fail("files differ in size; expected: %s, actual: %s", exp, act);
  }
  exp_ptr = xmmap(0, size, PROT_READ, MAP_SHARED, exp_fd, 0);
  act_ptr = xmmap(0, size, PROT_READ, MAP_SHARED, act_fd, 0);

  if (memcmp(exp_ptr, act_ptr, size) != 0) {
    t_fail("files differ; expected: %s, actual: %s", exp, act);
  }

  xmunmap(exp_ptr, size);
  xmunmap(act_ptr, size);

  xclose(exp_fd);
  xclose(act_fd);
}


/* Run compression test case. */
static void
test_compress(void)
{
  char *args[2] = {NULL, NULL};

  int fd;

  char *dir;
  char *in;
  char *zin;
  char *out;
  char *zout;
  char *zexp;
  char *err;

  int status;

  dir = t_concat("work-", suite_name, NULL);
  xmkdir(dir);

  in = t_concat(dir, "/", case_name, ".raw", NULL);
  zin = t_concat(base_dir, "/tests/suite/", suite_name, "/", case_name, ".bz2", NULL);
  out = t_concat(dir, "/", case_name, ".out", NULL);
  zout = t_concat(dir, "/", case_name, ".zout", NULL);
  zexp = t_concat(dir, "/", case_name, ".zexp", NULL);
  err = t_concat(dir, "/", case_name, ".err", NULL);

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
  status = t_exec("./lbzip2", args, in, zout, err);
  if (WIFSIGNALED(status)) {
    t_fail("lbzip2 was killed by signal %d (%s)", WTERMSIG(status),
           signal_name(WTERMSIG(status)));
  }
  if (WEXITSTATUS(status) != 0) {
    t_fail("lbzip2 failed with exit code %d", WEXITSTATUS(status));
  }
  fd = open_rd(err);
  if (xfstat_size(fd) != 0) {
    t_fail("lbzip2 printed message on standard error");
  }
  xclose(fd);
  if (file_exists(zexp)) {
    t_compare(zexp, zout);
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
    t_compare(in, out);
    xrename(zout, zexp);
  }

  free(dir);
  free(in);
  free(zin);
  free(out);
  free(zout);
  free(zexp);
  free(err);
}


/* Run decompression test case. */
static void
test_expand(void)
{
  char *args[3] = {NULL, "-d", NULL};

  int fd;
  int is_bad;
  off_t err_size;

  char *dir;
  char *bad;
  char *zin;
  char *out;
  char *exp;
  char *err;

  int status;

  dir = t_concat("work-", suite_name, NULL);
  xmkdir(dir);

  bad = t_concat(dir, "/", case_name, ".bad", NULL);
  zin = t_concat(base_dir, "/tests/suite/", suite_name, "/", case_name, ".bz2", NULL);
  out = t_concat(dir, "/", case_name, ".out", NULL);
  exp = t_concat(dir, "/", case_name, ".exp", NULL);
  err = t_concat(dir, "/", case_name, ".err", NULL);

  is_bad = file_exists(bad);
  if (is_bad == file_exists(exp)) {
    if (is_bad) {
      xunlink(bad);
      xunlink(exp);
    }
    status = t_exec("./minbzcat", args, zin, out, err);
    if (WIFSIGNALED(status)) {
      t_error("minbzcat was killed by signal %d (%s)",
              WTERMSIG(status), signal_name(WTERMSIG(status)));
    }
    if (WEXITSTATUS(status) == 0) {
      is_bad = 0;
      xrename(out, exp);
    }
    else {
      if (WEXITSTATUS(status) != 1) {
        t_error("minbzcat failed with exit code %d", WEXITSTATUS(status));
      }
      is_bad = 1;
      xclose(open_wr(bad));
    }
  }
  status = t_exec("./lbzip2", args, zin, out, err);
  if (WIFSIGNALED(status)) {
    t_fail("lbzip2 was killed by signal %d (%s)", WTERMSIG(status),
           signal_name(WTERMSIG(status)));
  }
  fd = open_rd(err);
  err_size = xfstat_size(fd);
  xclose(fd);
  if (!is_bad) {
    if (WEXITSTATUS(status) != 0) {
      t_fail("lbzip2 failed with exit code %d, but expected success", WEXITSTATUS(status));
    }
    if (err_size != 0) {
      t_fail("lbzip2 succeeded, but printed message on standard error");
    }
  }
  else {
    if (WEXITSTATUS(status) == 0) {
      t_fail("lbzip2 succeeded, but expected failure", WEXITSTATUS(status));
    }
    if (WEXITSTATUS(status) != 1) {
      t_fail("lbzip2 failed with exit code %d, but expected 1", WEXITSTATUS(status));
    }
    if (err_size == 0) {
      t_fail("lbzip2 failed, but did not print message on standard error");
    }
  }

  free(dir);
  free(bad);
  free(zin);
  free(out);
  free(exp);
  free(err);
}


/* Run specified test suite. */
int
main(int argc, char **argv)
{
  const char *mode;
  void (*test_handler)(void);

  (void)setlocale(LC_CTYPE, "C");  /* for isxdigit() */
  (void)setvbuf(stdout, NULL, _IONBF, 0);  /* for real-time test progress */

  if (argc != 5) {
    t_error("Exactly two arguments are expected: mode and path to test suite");
  }

  mode = argv[1];
  if (strcmp(mode, "compress") == 0) {
    test_handler = test_compress;
  }
  else if (strcmp(mode, "expand") == 0) {
    test_handler = test_expand;
  }
  else {
    t_error("unknown test mode: %s", mode);
  }

  base_dir = argv[2];
  suite_name = argv[3];
  case_name = argv[4];

  test_handler();
  xprintf("test passed\n");
  return 0;
}

/* lbunzip2.h,v 1.6 2009/11/27 03:38:44 lacos Exp */

#ifndef LBUNZIP2_H
#  define LBUNZIP2_H

struct lbunzip2_arg
{
  unsigned num_worker,
      num_slot;
  int print_cctrs,
      infd;
  const char *isep,
      *ifmt;
  int outfd;
  const char *osep,
      *ofmt;
};

void *
lbunzip2_wrap(void *v_lbunzip2_arg);

#endif

/* lbunzip2_single.h,v 1.5 2009/11/27 03:38:44 lacos Exp */

#ifndef LBUNZIP2_SINGLE_H
#  define LBUNZIP2_SINGLE_H

struct lbunzip2_single_arg
{
  unsigned num_slot;
  int print_cctrs,
      infd;
  const char *isep,
      *ifmt;
  int outfd;
  const char *osep,
      *ofmt;
};

void *
lbunzip2_single_wrap(void *v_lbunzip2_single_arg);

#endif

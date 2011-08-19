/* lbzip2.h,v 1.7 2009/11/27 03:38:44 lacos Exp */

#ifndef LBZIP2_H
#  define LBZIP2_H

struct lbzip2_arg
{
  unsigned num_worker,
      num_slot;
  int print_cctrs;
  struct filespec *ispec,
      *ospec;
  int bs100k,
      verbose,
      exponential;
};

void *
lbzip2_wrap(void *v_lbzip2_arg);

#endif

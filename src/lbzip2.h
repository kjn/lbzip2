/*-
  lbzip2.h -- high-level compression routines header

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
      verbose;
};

void *
lbzip2_wrap(void *v_lbzip2_arg);

#endif

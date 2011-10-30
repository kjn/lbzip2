/*-
  lbunzip2.h -- high-level decompression routines header

  Copyright (C) 2011 Mikolaj Izdebski
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

#ifndef LBUNZIP2_H
#  define LBUNZIP2_H

struct lbunzip2_arg
{
  unsigned num_worker,
      num_slot;
  int print_cctrs;
  struct filespec *ispec,
      *ospec;
  int verbose;
};

void *
lbunzip2_wrap(void *v_lbunzip2_arg);

#endif

/*-
  timespec.h -- time-related utilities

  Copyright (C) 2012 Mikolaj Izdebski

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

#include <time.h>
#include <stdbool.h>            /* bool */

struct timespec ts_now(void);
bool ts_before(struct timespec a, struct timespec b);
struct timespec ts_add_nano(struct timespec a, long nano);
double ts_diff(struct timespec a, struct timespec b);

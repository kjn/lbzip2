/*-
  timespec.c -- time-related utilities

  Copyright (C) 2025 Mikolaj Izdebski

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

#include "timespec.h"

struct timespec
ts_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts;
}

bool
ts_before(struct timespec a, struct timespec b)
{
  return a.tv_sec < b.tv_sec ||
    (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
}

struct timespec
ts_add_nano(struct timespec a, long nano)
{
  struct timespec ts = a;
  ts.tv_nsec += nano;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000L;
  }
  return ts;
}

double
ts_diff(struct timespec a, struct timespec b)
{
  struct timespec ts = a;
  ts.tv_sec -= b.tv_sec;
  ts.tv_nsec -= b.tv_nsec;
  if (ts.tv_nsec < 0) {
    ts.tv_sec -= 1;
    ts.tv_nsec += 1000000000L;
  }
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

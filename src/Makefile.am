#-
# Copyright (C) 2011 Mikolaj Izdebski
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

AM_CPPFLAGS = -I$(top_srcdir)/yambi -I$(top_srcdir)/lib
AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS)

bin_PROGRAMS = bzimp2
bzimp2_SOURCES = main.c lbzip2.c lbunzip2.c pqueue.c
bzimp2_LDADD = $(top_srcdir)/lib/libgnu.a $(top_srcdir)/yambi/libyambi.a \
    $(LIB_CLOCK_GETTIME) $(LIB_PTHREAD)

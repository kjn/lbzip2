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

check_PROGRAMS = minbzcat

minbzcat_SOURCES = minbzcat.c
minbzcat_LDADD = $(top_builddir)/lib/libgnu.a

TESTS_ENVIRONMENT = $(SHELL) $(srcdir)/Tester
TESTS = 32767.bz2 concat.bz2 crc1.bz2 crc2.bz2 cve.bz2 empty.bz2 fib.bz2 \
    gap.bz2 overrun.bz2 rand.bz2 repet.bz2 trash.bz2 void.bz2

EXTRA_DIST = $(TESTS) 32767.diff bzip2-0.1pl2.c ch255.c crc2.diff cve.c fib.c \
    Tester

#-
# Copyright (C) 2011, 2012, 2015 Mikolaj Izdebski
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

check_PROGRAMS = minbzcat driver

minbzcat_SOURCES = minbzcat.c
minbzcat_LDADD = $(top_builddir)/lib/libgnu.a

driver_SOURCES = driver.c
driver_LDADD = $(top_builddir)/lib/libgnu.a

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
                  $(top_srcdir)/build-aux/tap-driver.sh

TESTS = manual-compress.test fuzz-collect.test fuzz-divbwt.test \
    manual-expand.test

EXTRA_DIST = $(TESTS) 32767.diff bzip2-0.1pl2.c ch255.c crc2.diff cve.c fib.c

if ENABLE_COVERAGE
clean-local:
	$(LCOV) --rc lcov_branch_coverage=1 --directory $(top_srcdir) --zerocounters
check-local:
	$(LCOV) --rc lcov_branch_coverage=1 --directory $(top_srcdir) --capture --output-file coverage.info
	rm -rf $(builddir)/coverage
	$(GENHTML) --rc lcov_branch_coverage=1 --output-directory $(builddir)/coverage coverage.info
endif

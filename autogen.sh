#!/bin/sh
set -e

gnulib-tool --add-import pthread utimens warnings timespec-add timespec-sub \
    dtotimespec stat-time

aclocal -Im4
autoconf
autoheader
automake --add-missing --copy

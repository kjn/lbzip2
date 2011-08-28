#!/bin/sh
set -e

modules="pthread utimens warnings timespec-add timespec-sub dtotimespec"

gnulib-tool --add-import $modules
aclocal -Im4
autoconf
autoheader
automake --add-missing --copy

#!/usr/bin/perl -w
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

# Hardcoded files to consider.
@ARGV=qw(
yambi/encode.h
yambi/yambi.h
yambi/private.h
yambi/decode.h
yambi/transmit.c
yambi/divsufsort.c
yambi/decode.c
yambi/encode.c
yambi/prefix.c
yambi/collect.c
yambi/retrieve.c
yambi/emit.c
src/lbzip2.h
src/lbunzip2.h
src/pqueue.h
src/main.h
src/pqueue.c
src/lbunzip2.c
src/main.c
src/lbzip2.c
tests/minbzcat.c
) if !@ARGV;  # The user knows better.

sub msg { print "$f: @_\n"; ++$cnt }

for $f (@ARGV) {
  open F, $f or msg "file doesn't exist" and next;
  undef $/; $_=<F>;
  ++$nf;

  # ASCII chars, whitespaces, line length.
  /([^\x20-\x7e\n])/ and msg "contains prohibited chars";
  /[^\n]\n$/ or msg "doesn't end with a single NL";
  / \n/ and msg "has trailing whitespace before NL";
  /\n{4}/ and msg "has more than 2 consec blank lines";
  /\n[^\n]{80}/ and msg "has line longer than 79 chars";

  # C specific stuff.
  m{^/\*-([^*]|\*[^/])*Copyright([^*]|\*[^/])*\*/}
      or msg "has missing copyright block";
  $f =~ /\.h$/ xor /\n *# *include *<config\.h>\n/
      or msg "missing or excessive #include <config.h>";
}

$nf='No' if !$nf;
$cnt='no' if !$cnt;
print "$nf file(s) checked, $cnt warning(s).\n";

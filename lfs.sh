# lfs.sh,v 1.8 2010/02/18 22:20:30 lacos Exp

# Shell script to determine c89 configuration strings for large file support.
# Specify one of CFLAGS, LDFLAGS, LIBS in $1.

# Try to restore a sane shell environment. See "command" in SUSv2.

\unset IFS
\unalias -a
unset -f command
export PATH="$(command -p getconf PATH):$PATH"

unset LANG CDPATH
export LC_ALL=POSIX

set -e -C

# First sort key: biggest off_t possible.
# Second sort key: frugal int, long, pointer.
#
# programming env.  | int   | long  | ptr   | off_t
# ------------------+-------+-------+-------+------
# XBS5_ILP32_OFFBIG |    32 |    32 |    32 | >= 64
# XBS5_LPBIG_OFFBIG | >= 32 | >= 64 | >= 64 | >= 64
# XBS5_LP64_OFF64   |    32 |    64 |    64 |    64

for SPEC in \
    XBS5_ILP32_OFFBIG \
    XBS5_LPBIG_OFFBIG \
    XBS5_LP64_OFF64
do
  SUPP="$(getconf _$SPEC)"
  if [ x"$SUPP" != x-1 ] && [ x"$SUPP" != xundefined ]
  then
    # http://sources.redhat.com/bugzilla/show_bug.cgi?id=7095
    # Fixed by Ulrich Drepper on 07-FEB-2009.
    if ! getconf --version 2>&1 | grep -E -q 'getconf \((GNU libc|EGLIBC)\)' \
        || ! getconf $SPEC"_$1" 2>/dev/null
    then
      getconf -v $SPEC $SPEC"_$1"
    fi
    exit 0
  fi
done

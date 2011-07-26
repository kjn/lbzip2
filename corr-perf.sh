# corr-perf.sh,v 1.6 2009/10/19 22:49:23 lacos Exp

set -e -C

# Location of test files and to-be-created work files.
TESTDIR=/big/lacos/scratch

# How your platform prints (void *)0 when passed to printf()'s %p.
NULLFMT="(nil)"

# Number of cores (each with a dedicated cache).
MAXTHR=8

# Pathname to access GNU time.
GTIME=gtime

# Backlog factor (see main.c).
BLF=4

# After feeding more data to the splitter than the double of the
# splitter-to-workers queue, stall input for this many seconds. (Drain
# splitter-to-workers queue after filling it.)
INPUT_SLEEP=5

# Stall output initially for this many seconds. (Block everything through the
# muxer.)
OUTPUT_SLEEP=8


#########
# Setup #
#########

BNAME="$(basename "$0")"
exec </dev/null >>~/"$BNAME".$$.log 2>&1
set -x
cd "$TESTDIR"
PERFRESDIR="$HOME/$BNAME".$$.results


#####################
# Correctness tests #
#####################

input()
{
  dd bs=$((1024*1024)) count=$((BLF * MAXTHR * 2)) 2>/dev/null
  date +"input stalled, %Y-%m-%d %H:%M:%S %Z" >&2
  sleep "$INPUT_SLEEP"
  date +"input resumed, %Y-%m-%d %H:%M:%S %Z" >&2
  cat
}


output()
{
  date +"output stalled, %Y-%m-%d %H:%M:%S %Z" >&2
  sleep "$OUTPUT_SLEEP"
  date +"output resumed, %Y-%m-%d %H:%M:%S %Z" >&2
  cat
}


rm -f orig.concat orig.concat.bz2

for FILE in                              \
    gcc-4.3.0.tar                        \
    gcc-4.3.1.tar                        \
    gcc-4.3.2.tar                        \
    kdebase-runtime-4.0.4.tar            \
    kdebase-runtime-4.0.5.tar            \
    koffice-1.6.1.tar                    \
    koffice-1.6.2.tar                    \
    koffice-1.6.3.tar                    \
    linux-2.6.23.tar                     \
    linux-2.6.24.2.tar                   \
    linux-2.6.24.tar                     \
    linux-2.6.25.2.tar                   \
    linux-2.6.25.tar                     \
    linux-2.6.26.4.tar                   \
    linux-2.6.26.tar                     \
    Linux-pdf-HOWTOs-20070322.tar        \
    Linux-pdf-HOWTOs-20070811.tar        \
    Linux-ps-HOWTOs-20070322.tar         \
    Linux-ps-HOWTOs-20070811.tar         \
    netbeans-5_5_1-ide_sources.tar       \
    netbeans-5_5-ide_sources.tar         \
    OOo_2.3.1_src_core.tar               \
    OOo_2.3.1_src_l10n.tar               \
    OOo_2.4.0_src_core.tar               \
    OOo_2.4.0_src_l10n.tar               \
    OOo_2.4.1_src_core.tar               \
    OOo_2.4.1_src_l10n.tar               \
    ooo300-m3-l10n.tar                   \
    pyflate-0.31+bzr20070122-45MB-00     \
    pyflate-0.31+bzr20070122-45MB-fb     \
    pyflate-0.31+bzr20070122-510B        \
    pyflate-0.31+bzr20070122-765B        \
    pyflate-0.31+bzr20070122-aaa         \
    pyflate-0.31+bzr20070122-empty       \
    pyflate-0.31+bzr20070122-hello-world \
    qt-x11-opensource-src-4.4.0.tar      \
    qt-x11-opensource-src-4.4.1.tar
do
  date +"==== Starting correctness test on $FILE, %Y-%m-%d %H:%M:%S %Z"

  rm -f -- "$FILE"
  bzip2-shared -d -c -- "$FILE".bz2 >"$FILE"

  rm -f -- "$FILE".trace
  input <"$FILE".bz2 \
  | LBZIP2_TRACE_ALLOC=1 lbzip2 -d 2>"$FILE".trace \
  | output \
  | cmp -- - "$FILE"
  perl -w malloc_trace.pl "$NULLFMT" <"$FILE".trace

  rm -f -- "$FILE".single.trace
  input <"$FILE".bz2 \
  | LBZIP2_TRACE_ALLOC=1 lbzip2 -d -n 1 2>"$FILE".single.trace \
  | output \
  | cmp -- - "$FILE"
  perl -w malloc_trace.pl "$NULLFMT" <"$FILE".single.trace

  rm -f -- "$FILE".re.bz2 "$FILE".re.trace
  input <"$FILE" \
  | LBZIP2_TRACE_ALLOC=1 lbzip2 2>"$FILE".re.trace \
  | output >"$FILE".re.bz2
  bzip2-shared -d -c -- "$FILE".re.bz2 \
  | cmp -- - "$FILE"
  perl -w malloc_trace.pl "$NULLFMT" <"$FILE".re.trace

  rm -f -- "$FILE".re.de.trace
  input <"$FILE".re.bz2 \
  | LBZIP2_TRACE_ALLOC=1 lbzip2 -d 2>"$FILE".re.de.trace \
  | output \
  | cmp -- - "$FILE"
  perl -w malloc_trace.pl "$NULLFMT" <"$FILE".re.de.trace

  rm -f -- "$FILE".re.de.single.trace
  input <"$FILE".re.bz2 \
  | LBZIP2_TRACE_ALLOC=1 lbzip2 -d -n 1 2>"$FILE".re.de.single.trace \
  | output \
  | cmp -- - "$FILE"
  perl -w malloc_trace.pl "$NULLFMT" <"$FILE".re.de.single.trace

  cat -- "$FILE".bz2 >>orig.concat.bz2
  cat -- "$FILE" >>orig.concat
done

date +"==== Starting concatenated correctness test, %Y-%m-%d %H:%M:%S %Z"
rm -f orig.trace
input <orig.concat.bz2 \
| LBZIP2_TRACE_ALLOC=1 lbzip2 -d 2>orig.trace \
| output \
| cmp - orig.concat
perl -w malloc_trace.pl "$NULLFMT" <orig.trace

rm -f orig.single.trace
input <orig.concat.bz2 \
| LBZIP2_TRACE_ALLOC=1 lbzip2 -d -n 1 2>orig.single.trace \
| output \
| cmp - orig.concat
perl -w malloc_trace.pl "$NULLFMT" <orig.single.trace

rm -f orig.concat.re.bz2 orig.re.trace
input <orig.concat \
| LBZIP2_TRACE_ALLOC=1 lbzip2 2>orig.re.trace \
| output >orig.concat.re.bz2
bzip2-shared -d -c orig.concat.re.bz2 \
| cmp - orig.concat
perl -w malloc_trace.pl "$NULLFMT" <orig.re.trace

rm -f orig.re.de.trace
input <orig.concat.re.bz2 \
| LBZIP2_TRACE_ALLOC=1 lbzip2 -d 2>orig.re.de.trace \
| output \
| cmp - orig.concat
perl -w malloc_trace.pl "$NULLFMT" <orig.re.de.trace

rm -f orig.re.de.single.trace
input <orig.concat.re.bz2 \
| LBZIP2_TRACE_ALLOC=1 lbzip2 -d -n 1 2>orig.re.de.single.trace \
| output \
| cmp - orig.concat
perl -w malloc_trace.pl "$NULLFMT" <orig.re.de.single.trace


#####################
# Performance tests #
#####################

doit()
{
  local DECOMPRESS_OPTION="$1"
  local INPUT_SUFFIX="$2"

  local INPUT="orig.concat${INPUT_SUFFIX}"

  for ((WTHR=1; WTHR <= MAXTHR; ++WTHR)); do
    "$GTIME" -v lbzip2 -S $DECOMPRESS_OPTION -n "$WTHR" <"$INPUT" >/dev/null \
        2>"$PERFRESDIR"/lbzip2-S"$DECOMPRESS_OPTION"-n"$WTHR"
  done

  "$GTIME" -v bzip2-shared $DECOMPRESS_OPTION <"$INPUT" >/dev/null \
      2>"$PERFRESDIR"/bzip2-shared"$DECOMPRESS_OPTION"
}

date +"==== Starting performance tests, %Y-%m-%d %H:%M:%S %Z"
mkdir -- "$PERFRESDIR"

doit "-d" ".bz2"
doit "" ""

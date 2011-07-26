# test.sh,v 1.30 2009/11/27 03:38:44 lacos Exp

# Shell script to test lbzip2. For help, invoke without arguments. Must be run
# after the lbzip2 binary has been installed.

# Try to restore a sane shell environment. See "command" in SUSv2.

\unset IFS
\unalias -a
unset -f command
export PATH="$(command -p getconf PATH):$PATH"

unset LANG CDPATH
export LC_ALL=POSIX

# Unset environment variables that might have an influence on the compressors.
unset BZIP BZIP2 LBZIP2 LBZIP2_TRACE_ALLOC

set -e -C


C_BNAME="$(basename "$0")"
readonly C_BNAME

if ! tty -s; then
  cat <<EOT >&2
$C_BNAME: Please run $C_BNAME only from a (pseudo/virtual) terminal, eg. using
$C_BNAME: "screen". This script does not install signal handlers, and so relies
$C_BNAME: on the terminal driver to send SIGINT to the foreground process group
$C_BNAME: (ie. all descendants of this script) if you press the INTR character
$C_BNAME: (usually ^C).
EOT
  exit 1
fi

if [ ! -f "$1" ] || [ ! -r "$1" ]; then
  cat <<EOT >&2
$C_BNAME: "$1", passed in as \$1, is not a readable regular file.
$C_BNAME: Usage: $C_BNAME "DIR/UNCOMPRESSED-INPUT".
$C_BNAME: Working files will be placed under "DIR/scratch/".
$C_BNAME: Results will be placed under "DIR/results/".
$C_BNAME: If any of the last two directories pre-exist, please delete them.
EOT
  exit 1
fi

for T_CPROG in bzip2 lbzip2; do
  if ! command -v "$T_CPROG" >/dev/null; then
    printf '%s: command not found: %s\n' "$C_BNAME" "$T_CPROG" >&2
    exit 1
  fi
done

if command -v perl >/dev/null; then
  if command -v c89 >/dev/null; then
    C_CC1=c89
    C_CC2=''
  elif command -v gcc >/dev/null; then
    C_CC1=gcc
    C_CC2='-ansi -pedantic'
  else
    printf '%s: neither command found: %s %s\n' "$C_BNAME" c89 gcc >&2
    exit 1
  fi
  readonly C_CC1 C_CC2

  C_TRACE='1'
else
  printf '%s: perl not found, will not check allocation traces\n' "$C_BNAME" \
      >&2
  C_TRACE=''
fi
readonly C_TRACE

C_DNAME="$(dirname "$1")"
readonly C_DNAME

cd "$C_DNAME"

C_FNAME="$(basename "$1")"
readonly C_FNAME

if ! mkdir scratch results; then
  printf '%s: please run %s without arguments for help\n' "$C_BNAME" \
      "$C_BNAME" >&2
  exit 1
fi

if [ -z "$C_TRACE" ]; then
  C_NULLFMT=''
else
  cat >scratch/nullfmt.c <<'EOT'
#include <stdio.h>  /* fprintf() */
#include <stdlib.h> /* EXIT_FAILURE */

int main(void)
{
  return 0 > fprintf(stdout, "%p\n", (void *)0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
EOT

  T_CFLAGS="$(set CFLAGS; set +e; . "$OLDPWD"/lfs.sh)"
  T_LDFLAGS="$(set LDFLAGS; set +e; . "$OLDPWD"/lfs.sh)"
  T_LIBS="$(set LIBS; set +e; . "$OLDPWD"/lfs.sh)"

  $C_CC1 $T_CFLAGS -D _XOPEN_SOURCE=500 $C_CC2 -o scratch/nullfmt -s \
      $T_LDFLAGS scratch/nullfmt.c $T_LIBS
  C_NULLFMT="$(scratch/nullfmt)"
fi
readonly C_NULLFMT

C_EPROGS=''
for T_CPROG in pbzip2 7za; do
  if command -v "$T_CPROG" >/dev/null; then
    C_EPROGS="$C_EPROGS $T_CPROG"
  fi
done
readonly C_EPROGS


f_log()
{
  T_STAMP="$(date '+%Y-%m-%d %H:%M:%S %Z')"

  printf '%s: [%s] %s\n' "$C_BNAME" "$T_STAMP" "$*" >&2
}

f_corr_decompr_test()
{
  T_CASE="$1"-lbzip2-decompr"$3"
  f_log correctness "$T_CASE"

  LBZIP2_TRACE_ALLOC="$C_TRACE" lbzip2 -d $2 <scratch/"$1" \
      2>scratch/"$T_CASE".trace \
  | cmp - "$C_FNAME"
  if [ ! -z "$C_TRACE" ]; then
    perl -w "$OLDPWD"/malloc_trace.pl "$C_NULLFMT" <scratch/"$T_CASE".trace \
        >/dev/null
  fi
}

f_log correctness setup bzip2-compr
bzip2 <"$C_FNAME" >scratch/bzip2-compr

f_log correctness lbzip2-compr
LBZIP2_TRACE_ALLOC="$C_TRACE" lbzip2 <"$C_FNAME" >scratch/lbzip2-compr \
    2>scratch/lbzip2-compr.trace
if [ ! -z "$C_TRACE" ]; then
  perl -w "$OLDPWD"/malloc_trace.pl "$C_NULLFMT" <scratch/lbzip2-compr.trace \
      >/dev/null
fi

f_log correctness lbzip2-compr-bzip2-decompr
bzip2 -d <scratch/lbzip2-compr | cmp - "$C_FNAME"

f_corr_decompr_test bzip2-compr  ""     ""
f_corr_decompr_test bzip2-compr  "-n 1" -single
f_corr_decompr_test lbzip2-compr ""     ""
f_corr_decompr_test lbzip2-compr "-n 1" -single


# Under SUSv2, the word "time" is not a reserved word in the shell. (Or this
# may not be prohibited, but then the reserved word "time" has to support
# option "-p", and it is strange for a reserved word to take an option (think
# "if", "for")). I'm sure SUSv2 doesn't allow a reserved word to shadow a
# standard utility in an incompatible way. Well, some systems certified UNIX 98
# don't seem to care.
#
# ----------------------------------------------------------------------
# Standards, Environments, and Macros                  standards(5)
#
#          SUSv2        superset of SUS extended to  sup-    Solaris 7
#                       port   POSIX.1b-1993,   POSIX.1c-
#                       1996, and ISO/IEC 9899  (C  Stan-
#                       dard) Amendment 1
#
# http://www.opengroup.org/openbrand/register/xx.htm
# http://www.opengroup.org/openbrand/register/xw.htm
#
# $ export PATH=/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:/usr/local/bin
# $ sh
# $ \unset IFS
# $ \unalias -a
# $ unset -f command
# $ export PATH="$(command -p getconf PATH):$PATH"
# $ unset LANG CDPATH
# $ export LC_ALL=POSIX
# $ uname -s -r -v -m
# SunOS 5.10 Generic_120011-14 sun4u
# $ time -p true
# sh: -p:  not found
# [...]
# $ command -V time
# time is a reserved shell keyword
# $ 'time' -p true
# real 0.00
# user 0.00
# sys 0.00
# $ command time -p true
# Segmentation Fault (core dumped)
# ----------------------------------------------------------------------
#
# So I have to suppress reserved word recognition by quoting at least one
# character of the string "time". 28-JAN-2009 lacos

f_bzip2_compr_pipe()   { cat -- "$P_WFNAME" | ( 'time' -p bzip2    ) ; }
f_bzip2_decompr_pipe() { cat -- "$P_WFNAME" | ( 'time' -p bzip2 -d ) ; }
f_bzip2_compr_regf()   { 'time' -p bzip2    -c -- "$P_WFNAME" ; }
f_bzip2_decompr_regf() { 'time' -p bzip2 -d -c -- "$P_WFNAME" ; }
f_bzip2_ver()
{
  # I originally passed the "-V" option, but upstream bzip2-1.0.5, instead of
  # exiting after printing the version, still starts compressing. See
  # http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=220374
  # 28-JAN-2009 lacos
  bzip2 -h 2>&1 | sed -n '1s/^bzip2[^0-9]*\([0-9][^,]*\),.*$/\1/p'
}

f_lbzip2_compr_pipe()   { cat -- "$P_WFNAME" | ( 'time' -p lbzip2    -S ) ; }
f_lbzip2_decompr_pipe() { cat -- "$P_WFNAME" | ( 'time' -p lbzip2 -d -S ) ; }
f_lbzip2_compr_regf()   { 'time' -p lbzip2    -S <"$P_WFNAME" ; }
f_lbzip2_decompr_regf() { 'time' -p lbzip2 -d -S <"$P_WFNAME" ; }
f_lbzip2_ver()
{
  lbzip2 -h 2>&1 | sed -n 's/^Version \(.*\)\.$/\1/p'
}

f_pbzip2_compr_pipe()   { cat -- "$P_WFNAME" | ( 'time' -p pbzip2    -c - ) ; }
f_pbzip2_decompr_pipe() { cat -- "$P_WFNAME" | ( 'time' -p pbzip2 -d -c - ) ; }
f_pbzip2_compr_regf()   { 'time' -p pbzip2    -c -- "$P_WFNAME" ; }
f_pbzip2_setup()        {         pbzip2    -c -- "$P_WFNAME" ; }
f_pbzip2_decompr_regf() { 'time' -p pbzip2 -d -c -- "$P_WFNAME" ; }
f_pbzip2_ver()
{
  pbzip2 -V 2>&1 | sed -n '1s/^Parallel BZIP2 v\([^ ]*\) .*$/\1/p'
}

f_7za_compr_pipe()
{
  cat -- "$P_WFNAME" | ( 'time' -p 7za a _ -tbzip2 -bd -l -si -so )
}
f_7za_decompr_pipe()
{
  cat -- "$P_WFNAME" | ( 'time' -p 7za e _ -tbzip2 -bd -si -so )
}
f_7za_compr_regf()   { 'time' -p 7za a _ -tbzip2 -bd -l -so "$P_WFNAME" ; }
f_7za_setup()        {         7za a _ -tbzip2 -bd -l -so "$P_WFNAME" ; }
f_7za_decompr_regf() { 'time' -p 7za e "$P_WFNAME" -tbzip2 -bd -so ; }
f_7za_ver()
{
  7za 2>&1 | sed -n '3s/^p7zip  *Version  *\([.0-9]*\).*$/\1/p'
}


P_WFNAME="$C_FNAME"
for T_CPROG in $C_EPROGS; do
  f_log performance setup "$T_CPROG"-compr
  # If this fails for any CPROG, we bail out.
  f_"$T_CPROG"_setup >scratch/"$T_CPROG"-compr
done

f_log performance compression warmup
cat -- "$C_FNAME" >/dev/null

for T_INP in regf pipe; do
  for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
    T_FUN="$T_CPROG"_compr_"$T_INP"
    f_log performance "$T_FUN"

    # If the test fails, create an empty file.
    if T_OUT="$(f_"$T_FUN" 2>&1 >/dev/null)"; then
      printf '%s\n' "$T_OUT"
    else
      printf '%s\n' "$T_OUT" >&2
    fi >results/log."$T_CPROG"-compr-"$T_INP"
  done
done

for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
  P_WFNAME=scratch/"$T_CPROG"-compr

  f_log performance decompression warmup "$P_WFNAME"
  cat -- "$P_WFNAME" >/dev/null

  for T_INP in regf pipe; do
    for T_DPROG in bzip2 lbzip2 $C_EPROGS; do
      T_FUN="$T_DPROG"_decompr_"$T_INP"
      f_log performance "$T_FUN" on "$P_WFNAME"

      # If the test fails, create an empty file.
      if T_OUT="$(f_"$T_FUN" 2>&1 >/dev/null)"; then
        printf '%s\n' "$T_OUT"
      else
        printf '%s\n' "$T_OUT" >&2
      fi >results/log."$T_CPROG"-compr-"$T_DPROG"-decompr-"$T_INP"
    done
  done
done


f_print_size() { wc -c <"$1" >results/size."$2" ; }

f_log counting bytes
f_print_size "$C_FNAME" orig
for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
  f_print_size scratch/"$T_CPROG"-compr "$T_CPROG"-compr
done


f_divp()
{
  if [ ! -z "$1" ] && [ ! -z "$2" ]; then
    printf '%s\n' "if (0 != ($2)) { scale = 2; (100 * ($1)) / ($2) }" | bc
  fi
}

f_log calculating ratios

T_V_BZIP2="$(cat -- results/size.bzip2-compr)"
for T_CPROG in lbzip2 $C_EPROGS; do
  T_V_PROG="$(cat -- results/size."$T_CPROG"-compr)"
  f_divp "$T_V_PROG" "$T_V_BZIP2" >results/percent.compr-size-"$T_CPROG"-bzip2
done

f_get_real()
{
  sed -n 's/^real *\([^ ]*\)$/\1/p' results/log."$1" | head -n 1
}

for T_INP in regf pipe; do
  T_V_BZIP2="$(f_get_real bzip2-compr-"$T_INP")"
  for T_CPROG in lbzip2 $C_EPROGS; do
    T_V_PROG="$(f_get_real "$T_CPROG"-compr-"$T_INP")"
    f_divp "$T_V_BZIP2" "$T_V_PROG" \
        >results/percent.compr-speed-"$T_INP"-"$T_CPROG"-bzip2
  done
done

for T_INP in regf pipe; do
  for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
    T_V_BZIP2="$(f_get_real "$T_CPROG"-compr-bzip2-decompr-"$T_INP")"
    for T_DPROG in lbzip2 $C_EPROGS; do
      T_V_PROG="$(f_get_real "$T_CPROG"-compr-"$T_DPROG"-decompr-"$T_INP")"
      f_divp "$T_V_BZIP2" "$T_V_PROG" \
          >results/percent.decompr-speed-"$T_CPROG"-"$T_INP"-"$T_DPROG"-bzip2
    done
  done
done

f_get_tries()
{
  sed -n '2s/^lbzip2:\( any\)\{0,1\} worker tried[^:]*: *\([0-9]*\)$/\2/p' \
      results/log."$1"
}

f_get_stalls()
{
  sed -n '3s/^lbzip2:\( any\)\{0,1\} worker stalled[^:]*: *\([0-9]*\)$/\2/p' \
      results/log."$1"
}

f_print_stalls()
{
  T_TRIES="$(f_get_tries "$1")"
  T_STALLS="$(f_get_stalls "$1")"
  f_divp "$T_STALLS" "$T_TRIES" >results/percent.stall-"$1"
}

for T_INP in regf pipe; do
  for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
    f_print_stalls "$T_CPROG"-compr-lbzip2-decompr-"$T_INP"
  done
  f_print_stalls lbzip2-compr-"$T_INP"
done


f_log collecting system information
for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
  f_"$T_CPROG"_ver >results/ver."$T_CPROG"
done
uname -s -r -v -m >results/uname


f_log creating template to fill in

cat <<EOT >results/edit
:testdate
$(date '+%Y-%m-%d')

:submitter
# name, e-mail address


:distro
# OS distribution (packaging) name and version


:cc
# C compiler name and version


:libc
# C library name and version


:physmem_MiB
# size of RAM in blocks of 2^20 bytes


:processor_type
# eg. AMD Athlon(tm) 64 X2 Dual Core Processor 6000+


:num_all_procs
# number of all processors


:num_all_cores
# number of *all* cores, together


:num_all_hwts
# number of *all* hardware threads, together


:machinfo
# other info; product name, chipset; free multiline text

EOT

for T_TYPE_ID in 1 2 3 4 5 6; do
  cat <<EOT >>results/edit

:cache_type_${T_TYPE_ID}_typename
# L1, L2, TLB, whatever


:cache_type_${T_TYPE_ID}_size_KiB
# cache size in blocks of 1024 bytes


:cache_type_${T_TYPE_ID}_assoc
# cache associativity; free multiline text, be specific if you can


:cache_type_${T_TYPE_ID}_linesize_B
# cache line size in bytes


:cache_type_${T_TYPE_ID}_incl_excl
# inclusive/exclusive wrt. "higher" caches, free multiline text


:cache_type_${T_TYPE_ID}_contents
# data, instr, data+instr; free text


:cache_type_${T_TYPE_ID}_sharedby_hwts
# number of hardware threads sharing one such cache


:cache_type_${T_TYPE_ID}_sharedby_cores
# number of cores sharing one such cache


:cache_type_${T_TYPE_ID}_sharedby_procs
# number of processors sharing one such cache

EOT
done


C_TS0=1
C_TS1=20
C_TS2=33
C_TS3=46
C_TSS='|                  |            |            |'
C_W=72
C_L='-----------------------------------------------------------------------'
readonly C_TS0 C_TS1 C_TS2 C_TS3 C_TSS C_W C_L

f_tab() { printf "%.$1s%s%$(($C_W - $1 - ${#2}))s\\n" "$C_TSS" "$2" "$3" ; }

f_lpad()
{
  T_VAL="$(cat results/"$3")"
  f_tab "$1" "$2" "$T_VAL"
}

f_lpadr()
{
  T_VAL="$(f_get_real "$3")"
  f_tab "$1" "$2" "$T_VAL"
}

f_sep()
{
  printf "%.$(($1 - 1))s+%.$(($C_W - $1))s\\n" "$C_TSS" "$C_L"
  f_tab "$1" "$2" ""
}

f_log writing quick report

{
  f_sep "$C_TS0" 'Version'
  for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
    f_lpad "$C_TS1" "$T_CPROG" ver."$T_CPROG"
  done

  f_sep "$C_TS0" 'File size [B]'
  f_lpad "$C_TS1" original size.orig
  for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
    f_lpad "$C_TS1" "$T_CPROG" size."$T_CPROG"-compr
  done

  f_sep "$C_TS0" 'Compr. size [%]'
  for T_CPROG in lbzip2 $C_EPROGS; do
    f_lpad "$C_TS1" "$T_CPROG":bzip2 percent.compr-size-"$T_CPROG"-bzip2
  done

  f_sep "$C_TS0" 'Compr. time [s]'
  for T_INP in regf pipe; do
    f_sep "$C_TS1" "from $T_INP"
    for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
      f_lpadr "$C_TS2" "$T_CPROG" "$T_CPROG"-compr-"$T_INP"
    done
  done

  f_sep "$C_TS0" 'Compr. speed [%]'
  for T_INP in regf pipe; do
    f_sep "$C_TS1" "from $T_INP"
    for T_CPROG in lbzip2 $C_EPROGS; do
      f_lpad "$C_TS2" "$T_CPROG":bzip2 \
          percent.compr-speed-"$T_INP"-"$T_CPROG"-bzip2
    done
  done

  f_sep "$C_TS0" '"lbzip2" ws [%]'
  f_lpad "$C_TS1" "from regf" percent.stall-lbzip2-compr-regf
  f_lpad "$C_TS1" "from pipe" percent.stall-lbzip2-compr-pipe

  f_sep "$C_TS0" 'Decompr. time [s]'
  for T_INP in regf pipe; do
    f_sep "$C_TS1" "from $T_INP"
    for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
      f_sep "$C_TS2" "from $T_CPROG"
      for T_DPROG in bzip2 lbzip2 $C_EPROGS; do
        f_lpadr "$C_TS3" "by $T_DPROG" \
            "$T_CPROG"-compr-"$T_DPROG"-decompr-"$T_INP"
      done
    done
  done

  f_sep "$C_TS0" 'Decompr. speed [%]'
  for T_INP in regf pipe; do
    f_sep "$C_TS1" "from $T_INP"
    for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
      f_sep "$C_TS2" "from $T_CPROG"
      for T_DPROG in lbzip2 $C_EPROGS; do
        f_lpad "$C_TS3" "$T_DPROG":bzip2 \
            percent.decompr-speed-"$T_CPROG"-"$T_INP"-"$T_DPROG"-bzip2
      done
    done
  done

  f_sep "$C_TS0" '"lbzip2 -d" ws [%]'
  for T_INP in regf pipe; do
    f_sep "$C_TS1" "from $T_INP"
    for T_CPROG in bzip2 lbzip2 $C_EPROGS; do
      f_lpad "$C_TS2" "from $T_CPROG" \
          percent.stall-"$T_CPROG"-compr-lbzip2-decompr-"$T_INP"
    done
  done
} >scratch/rep

sed 's/  *$//' scratch/rep >results/report


C_REV='test.sh,v 1.30'
readonly C_REV

cat <<EOT >&2

$C_BNAME: Tests finished. A quick report has been saved as
$C_BNAME: "$C_DNAME/results/report".
$C_BNAME: Please edit
$C_BNAME: "$C_DNAME/results/edit".
$C_BNAME: After possibly reviewing the files under
$C_BNAME: "$C_DNAME/results/",
$C_BNAME: please send them to <lacos@caesar.elte.hu> as an attached pax (tar)
$C_BNAME: file, eg. "results.tar.bz2", with the subject
$C_BNAME: "automated lbzip2 test results [$C_REV]"
$C_BNAME: Thanks!
EOT

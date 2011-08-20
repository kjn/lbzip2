AM_CPPFLAGS = -I$(top_srcdir)/yambi -I$(top_srcdir)/lib
AM_CFLAGS = $(WARN_CFLAGS) $(WERROR_CFLAGS)

bin_PROGRAMS = lbzip2
lbzip2_SOURCES = main.c lbzip2.c lbunzip2.c lacos_rbtree.c
lbzip2_LDADD = $(top_srcdir)/lib/libgnu.a $(top_srcdir)/yambi/libyambi.a \
    $(LIB_CLOCK_GETTIME) $(LIB_PTHREAD)

# Makefile,v 1.9 2009/04/03 22:33:49 lacos Exp
.POSIX:

CC=gcc
CFLAGS=$$($(SHELL) lfs.sh CFLAGS) -D _XOPEN_SOURCE=500 -pipe -ansi -pedantic \
    -O2




LDFLAGS=-s $$($(SHELL) lfs.sh LDFLAGS)
LIBS=-l pthread -l bz2 $$($(SHELL) lfs.sh LIBS)

lbzip2: main.o lbzip2.o lbunzip2.o lbunzip2_single.o lacos_rbtree.o
	$(CC) -o lbzip2 $(LDFLAGS) main.o lbzip2.o lbunzip2.o \
  lbunzip2_single.o lacos_rbtree.o $(LIBS)

main.o: main.c main.h lbunzip2_single.h lbunzip2.h lbzip2.h
	$(CC) $(CFLAGS) -c main.c

lbzip2.o: lbzip2.c main.h lbzip2.h lacos_rbtree.h
	$(CC) $(CFLAGS) -c lbzip2.c

lbunzip2.o: lbunzip2.c main.h lbunzip2.h lacos_rbtree.h
	$(CC) $(CFLAGS) -c lbunzip2.c

lbunzip2_single.o: lbunzip2_single.c main.h lbunzip2_single.h
	$(CC) $(CFLAGS) -c lbunzip2_single.c

lacos_rbtree.o: lacos_rbtree.c lacos_rbtree.h
	$(CC) $(CFLAGS) -c lacos_rbtree.c

clean:
	rm -f lbzip2 main.o lbzip2.o lbunzip2.o lbunzip2_single.o \
  lacos_rbtree.o

check:
	$(SHELL) test.sh $(TESTFILE)

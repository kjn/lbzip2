Description: This patch is described in detail in the 0.13-1 changelog entry.
 In short:
 - move macro definitions to debian/rules,
 - add "install" target,
 - add "debian-sanity" target to quickly check the binary before installation.
 Upstream strived to justify his refusal to incorporate these changes.
Author: Laszlo Ersek <lacos@caesar.elte.hu>
Forwarded: http://lists.debian.org/debian-mentors/2009/10/msg00334.html
Reviewed-by: Paul Wise <pabs@debian.org>
Last-Update: 2009-10-28
Index: lbzip2/Makefile
===================================================================
--- lbzip2.orig/Makefile	2009-10-28 21:07:16.000000000 +0100
+++ lbzip2/Makefile	2009-10-28 21:09:25.000000000 +0100
@@ -1,16 +1,6 @@
 # Makefile,v 1.9 2009/04/03 22:33:49 lacos Exp
 .POSIX:
 
-CC=gcc
-CFLAGS=$$($(SHELL) lfs.sh CFLAGS) -D _XOPEN_SOURCE=500 -pipe -ansi -pedantic \
-    -O2
-
-
-
-
-LDFLAGS=-s $$($(SHELL) lfs.sh LDFLAGS)
-LIBS=-l pthread -l bz2 $$($(SHELL) lfs.sh LIBS)
-
 lbzip2: main.o lbzip2.o lbunzip2.o lbunzip2_single.o lacos_rbtree.o
 	$(CC) -o lbzip2 $(LDFLAGS) main.o lbzip2.o lbunzip2.o \
   lbunzip2_single.o lacos_rbtree.o $(LIBS)
@@ -32,7 +22,34 @@
 
 clean:
 	rm -f lbzip2 main.o lbzip2.o lbunzip2.o lbunzip2_single.o \
-  lacos_rbtree.o
+  lacos_rbtree.o rnd rnd.bz2 rnd.lbz2
+
+debian-sanity: lbzip2
+	rm -f rnd rnd.bz2 rnd.lbz2
+
+	# Create bzip2-compressed, save original.
+	head -c 10000000 </dev/urandom | uuencode x | tee rnd | bzip2 >rnd.bz2
+
+	# Create lbzip2-compressed.
+	LBZIP2= BZIP2= BZIP= ./lbzip2 <rnd >rnd.lbz2
+
+	# Decompress lbzip2-compressed with bzip2.
+	bzip2 -d <rnd.lbz2 | cmp - rnd
+
+	# Decompress lbzip2-compressed with lbzip2, use MWD on multi-core.
+	LBZIP2= BZIP2= BZIP= ./lbzip2 -d <rnd.lbz2 | cmp - rnd
+
+	# Decompress bzip2-compressed with lbzip2, use MWD on multi-core.
+	LBZIP2= BZIP2= BZIP= ./lbzip2 -d <rnd.bz2 | cmp - rnd
+
+	# Decompress lbzip2-compressed with lbzip2, use SWD.
+	LBZIP2= BZIP2= BZIP= ./lbzip2 -d -n 1 <rnd.lbz2 | cmp - rnd
+
+	# Decompress bzip2-compressed with lbzip2, use SWD.
+	LBZIP2= BZIP2= BZIP= ./lbzip2 -d -n 1 <rnd.bz2 | cmp - rnd
 
-check:
-	$(SHELL) test.sh $(TESTFILE)
+install: $(INSTDEP)
+	install -d $(DESTDIR)/usr/bin $(DESTDIR)/usr/share/lbzip2
+	install -t $(DESTDIR)/usr/bin -p lbzip2
+	install -t $(DESTDIR)/usr/share/lbzip2 -m 644 -p corr-perf.sh \
+  malloc_trace.pl test.sh lfs.sh

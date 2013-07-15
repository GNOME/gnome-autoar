# vim: set sw=8 ts=8 sts=8 noet:

NULL =
noinst_PROGRAMS =
EXTRA_DIST =

NOINST_H_FILES = \
	autoar-extract.h	\
	$(NULL)
INST_H_FILES = \
	$(NULL)

noinst_LTLIBRARIES = libautoarchive.la
libautoarchive_la_SOURCES = \
	autoar-extract.c	\
	$(INST_H_FILES)		\
	$(NOINST_H_FILES)	\
	$(NULL)
libautoarchive_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(NULL)
libautoarchive_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)	\
	$(AM_CFLAGS)		\
	$(NULL)

noinst_PROGRAMS += test-extract
EXTRA_DIST += test-extract.c
test_extract_SOURCES = \
	test-extract.c		\
	$(NULL)
test_extract_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)	\
	$(AM_CFLAGS)		\
	$(NULL)
test_extract_LDADD = \
	$(DEPENDENCIES_LIBS)	\
	libautoarchive.la	\
	$(NULL)

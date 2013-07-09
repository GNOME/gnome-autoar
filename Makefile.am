NULL =

noinst_LTLIBRARIES = libautoarchive.la

NOINST_H_FILES = \
	autoar-extract.h	\
	$(NULL)

INST_H_FILES = \
	$(NULL)

libautoarchive_la_SOURCES = \
	autoar-extract.c	\
	$(INST_H_FILES)		\
	$(NOINST_H_FILES)	\
	$(NULL)

libautoarchive_la_CPPFLAGS = \
	$(AM_CPPFLAGS)

libautoarchive_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)	\
	$(AM_CFLAGS)

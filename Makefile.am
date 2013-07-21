# vim: set sw=8 ts=8 sts=8 noet:

NULL =

noinst_PROGRAMS =

EXTRA_DIST = \
	autoar-enum-types.h.template \
	autoar-enum-types.c.template \
	$(NULL)

noinst_LTLIBRARIES = libautoarchive.la

libautoarchive_h_sources = \
	autoar-extract.h	\
	autoar-pref.h		\
	$(NULL)
libautoarchive_c_sources = \
	autoar-extract.c	\
	autoar-pref.c		\
	$(NULL)
libautoarchive_generated_sources = \
	autoar-enum-types.h	\
	autoar-enum-types.c	\
	$(NULL)

libautoarchive_la_SOURCES = \
	$(libautoarchive_generated_sources)	\
	$(libautoarchive_h_sources)		\
	$(libautoarchive_c_sources)		\
	$(NULL)
libautoarchive_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(NULL)
libautoarchive_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)	\
	$(AM_CFLAGS)		\
	$(NULL)

autoar-enum-types.h: autoar-enum-types.h.template $(libautoarchive_h_sources) $(GLIB_MKENUMS)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template autoar-enum-types.h.template $(libautoarchive_h_sources)) > $@
autoar-enum-types.c: autoar-enum-types.c.template $(libautoarchive_h_sources) $(GLIB_MKENUMS)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template autoar-enum-types.c.template $(libautoarchive_h_sources)) > $@

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

BUILT_SOURCES = \
	$(libautoarchive_generated_sources)	\
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

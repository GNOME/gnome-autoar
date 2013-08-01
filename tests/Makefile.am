# vim: set sw=8 ts=8 sts=8 noet:

NULL =

noinst_PROGRAMS = test-extract test-pref test-create

test_cflags = \
	$(DEPENDENCIES_CFLAGS)	\
	$(AM_CFLAGS)		\
	-I$(top_srcdir)		\
	$(NULL)

test_libs = \
	$(DEPENDENCIES_LIBS)	\
	$(top_builddir)/gnome-autoar/libgnome-autoar.la	\
	$(NULL)


test_extract_SOURCES = test-extract.c
test_extract_CFLAGS = $(test_cflags)
test_extract_LDADD = $(test_libs)

test_pref_SOURCES = test-pref.c
test_pref_CFLAGS = $(test_cflags)
test_pref_LDADD = $(test_libs)

test_create_SOURCES = test-create.c
test_create_CFLAGS = $(test_cflags)
test_create_LDADD = $(test_libs)

# vim: set sw=8 ts=8 sts=8 noet:

NULL =

EXTRA_DIST = \
	autoar-enum-types.h.template		\
	autoar-enum-types.c.template		\
	gnome-autoar.pc.in			\
	$(NULL)

lib_LTLIBRARIES = libgnome-autoar.la

libgnome_autoar_h_sources = \
	autoar-common.h		\
	autoar-create.h		\
	autoar-extract.h	\
	autoar-format-filter.h	\
	autoar-gtk.h		\
	autoar-pref.h		\
	$(NULL)
libgnome_autoar_c_sources = \
	autoar-common.c		\
	autoar-create.c		\
	autoar-extract.c	\
	autoar-format-filter.c	\
	autoar-gtk.c		\
	autoar-pref.c		\
	$(NULL)
libgnome_autoar_generated_sources = \
	autoar-enum-types.h	\
	autoar-enum-types.c	\
	$(NULL)

headerincludedir = $(includedir)/gnome-autoar
headerinclude_HEADERS = \
	autoar.h	\
	$(libgnome_autoar_h_sources)	\
	$(NULL)

libgnome_autoar_la_SOURCES = \
	$(libgnome_autoar_generated_sources)	\
	$(libgnome_autoar_h_sources)		\
	$(libgnome_autoar_c_sources)		\
	$(NULL)
libgnome_autoar_la_CPPFLAGS = \
	$(AM_CPPFLAGS)	\
	$(NULL)
libgnome_autoar_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)	\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

autoar-enum-types.h: autoar-enum-types.h.template $(libgnome_autoar_h_sources) $(GLIB_MKENUMS)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template autoar-enum-types.h.template $(libgnome_autoar_h_sources)) > $@
autoar-enum-types.c: autoar-enum-types.c.template $(libgnome_autoar_h_sources) $(GLIB_MKENUMS)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template autoar-enum-types.c.template $(libgnome_autoar_h_sources)) > $@

BUILT_SOURCES = \
	$(libgnome_autoar_generated_sources)	\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-autoar.pc

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

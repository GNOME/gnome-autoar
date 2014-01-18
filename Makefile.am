# vim: set sw=8 ts=8 sts=8 noet:

NULL =

# Global options
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =
BUILT_SOURCES =
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc


# Top-level directory
EXTRA_DIST += autogen.sh


# Directory: gnome-autoar
EXTRA_DIST += \
	gnome-autoar/autoar-enum-types.h.template	\
	gnome-autoar/autoar-enum-types.c.template	\
	gnome-autoar/gnome-autoar.pc.in			\
	$(NULL)

lib_LTLIBRARIES = gnome-autoar/libgnome-autoar.la

libgnome_autoar_la_headers = \
	gnome-autoar/autoar-common.h		\
	gnome-autoar/autoar-create.h		\
	gnome-autoar/autoar-extract.h		\
	gnome-autoar/autoar-format-filter.h	\
	gnome-autoar/autoar-gtk.h		\
	gnome-autoar/autoar-pref.h		\
	$(NULL)
libgnome_autoar_la_sources = \
	gnome-autoar/autoar-common.c		\
	gnome-autoar/autoar-create.c		\
	gnome-autoar/autoar-extract.c		\
	gnome-autoar/autoar-format-filter.c	\
	gnome-autoar/autoar-gtk.c		\
	gnome-autoar/autoar-pref.c		\
	$(NULL)
libgnome_autoar_la_generated_files = \
	gnome-autoar/autoar-enum-types.h	\
	gnome-autoar/autoar-enum-types.c	\
	$(NULL)

BUILT_SOURCES += \
	$(libgnome_autoar_la_generated_files)	\
	$(NULL)

autoarincludedir = $(includedir)/gnome-autoar
autoarinclude_HEADERS = \
	gnome-autoar/autoar.h			\
	$(libgnome_autoar_la_headers)		\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-autoar/gnome-autoar.pc

gnome_autoar_libgnome_autoar_la_SOURCES = \
	$(libgnome_autoar_la_generated_files)	\
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)
gnome_autoar_libgnome_autoar_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_la_CFLAGS = \
	$(DEPENDENCIES_CFLAGS)			\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)

gnome-autoar/autoar-enum-types.h: gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers) ) > $@
gnome-autoar/autoar-enum-types.c: gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers) ) > $@


# Directory: data
gsettings_ENUM_NAMESPACE = org.gnome.desktop.archives
gsettings_ENUM_FILES = $(top_srcdir)/gnome-autoar/autoar-format-filter.h

gsettings_SCHEMAS = data/org.gnome.desktop.archives.gschema.xml
EXTRA_DIST += $(gsettings_SCHEMAS)

@GSETTINGS_RULES@


# Directory: tests
noinst_PROGRAMS = \
	tests/test-extract	\
	tests/test-pref		\
	tests/test-create	\
	tests/test-ui		\
	$(NULL)

test_cflags = \
	$(DEPENDENCIES_CFLAGS)	\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

test_libs = \
	$(top_builddir)/gnome-autoar/libgnome-autoar.la	\
	$(DEPENDENCIES_LIBS)	\
	$(NULL)

tests_test_extract_SOURCES = tests/test-extract.c
tests_test_extract_CFLAGS = $(test_cflags)
tests_test_extract_LDADD = $(test_libs)

tests_test_pref_SOURCES = tests/test-pref.c
tests_test_pref_CFLAGS = $(test_cflags)
tests_test_pref_LDADD = $(test_libs)

tests_test_create_SOURCES = tests/test-create.c
tests_test_create_CFLAGS = $(test_cflags)
tests_test_create_LDADD = $(test_libs)

tests_test_ui_SOURCES = tests/test-ui.c
tests_test_ui_CFLAGS = $(test_cflags)
tests_test_ui_LDADD = $(test_libs)

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

SUBDIRS = . docs

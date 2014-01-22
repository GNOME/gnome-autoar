# vim: set sw=8 ts=8 sts=8 noet:

NULL =

# Global options
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =
BUILT_SOURCES =
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --enable-introspection


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
	$(GTK_CFLAGS)				\
	$(GIO_CFLAGS)				\
	$(LIBARCHIVE_CFLAGS)			\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_la_LIBADD = \
	$(GTK_LIBS)				\
	$(GIO_LIBS)				\
	$(LIBARCHIVE_LIBS)			\
	$(DEPENDENCIES_LIBS)			\
	$(NULL)

gnome-autoar/autoar-enum-types.h: gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers) ) > $@
gnome-autoar/autoar-enum-types.c: gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers) ) > $@

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = gnome-autoar/GnomeAutoar-0.1.gir
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

gnome-autoar/GnomeAutoar-0.1.gir: gnome-autoar/libgnome-autoar.la
gnome_autoar_GnomeAutoar_0_1_gir_SCANNERFLAGS = --identifier-prefix=Autoar
gnome_autoar_GnomeAutoar_0_1_gir_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
gnome_autoar_GnomeAutoar_0_1_gir_INCLUDES = GLib-2.0 GObject-2.0 Gio-2.0 Gtk-3.0
gnome_autoar_GnomeAutoar_0_1_gir_LIBS = gnome-autoar/libgnome-autoar.la
gnome_autoar_GnomeAutoar_0_1_gir_FILES = \
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)

CLEANFILES += $(gir_DATA) $(typelib_DATA)

endif


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
	$(GTK_CFLAGS)		\
	$(GIO_CFLAGS)		\
	$(LIBARCHIVE_CFLAGS)	\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

test_libs = \
	$(top_builddir)/gnome-autoar/libgnome-autoar.la	\
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

SUBDIRS = . docs

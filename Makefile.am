# vim: set sw=8 ts=8 sts=8 noet:

NULL =

# Global options
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =
BUILT_SOURCES =
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk --enable-gtk-doc --enable-introspection


# Top-level directory
EXTRA_DIST += autogen.sh


# Directory: gnome-autoar
EXTRA_DIST += \
	gnome-autoar/autoar-enum-types.h.template	\
	gnome-autoar/autoar-enum-types.c.template	\
	gnome-autoar/gnome-autoar.pc.in			\
	$(NULL)

libgnome_autoar_la_headers = \
	gnome-autoar/autoar-create.h		\
	gnome-autoar/autoar-extract.h		\
	gnome-autoar/autoar-format-filter.h	\
	gnome-autoar/autoar-misc.h		\
	gnome-autoar/autoar-pref.h		\
	$(NULL)
libgnome_autoar_la_sources = \
	gnome-autoar/autoar-create.c		\
	gnome-autoar/autoar-extract.c		\
	gnome-autoar/autoar-format-filter.c	\
	gnome-autoar/autoar-misc.c		\
	gnome-autoar/autoar-pref.c		\
	$(NULL)
libgnome_autoar_la_private_files = \
	gnome-autoar/autoar-private.h		\
	gnome-autoar/autoar-private.c		\
	$(NULL)
libgnome_autoar_la_generated_files = \
	gnome-autoar/autoar-enum-types.h	\
	gnome-autoar/autoar-enum-types.c	\
	$(NULL)
libgnome_autoar_gtk_la_headers = \
	gnome-autoar/autoar-gtk-chooser.h	\
	$(NULL)
libgnome_autoar_gtk_la_sources = \
	gnome-autoar/autoar-gtk-chooser.c	\
	$(NULL)

BUILT_SOURCES += \
	$(libgnome_autoar_la_generated_files)	\
	$(NULL)

lib_LTLIBRARIES = gnome-autoar/libgnome-autoar.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnome-autoar/gnome-autoar.pc

autoarincludedir = $(includedir)/gnome-autoar
autoarinclude_HEADERS = \
	gnome-autoar/autoar.h			\
	$(libgnome_autoar_la_headers)		\
	$(NULL)

gnome_autoar_libgnome_autoar_la_SOURCES = \
	$(libgnome_autoar_la_generated_files)	\
	$(libgnome_autoar_la_private_files)	\
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)
gnome_autoar_libgnome_autoar_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_la_CFLAGS = \
	$(GIO_CFLAGS)				\
	$(LIBARCHIVE_CFLAGS)			\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_la_LIBADD = \
	$(GIO_LIBS)				\
	$(LIBARCHIVE_LIBS)			\
	$(NULL)

if ENABLE_GTK

lib_LTLIBRARIES += gnome-autoar/libgnome-autoar-gtk.la

pkgconfig_DATA += gnome-autoar/gnome-autoar-gtk.pc

autoarinclude_HEADERS += \
	gnome-autoar/autoar-gtk.h		\
	$(libgnome_autoar_gtk_la_headers)	\
	$(NULL)

gnome_autoar_libgnome_autoar_gtk_la_SOURCES = \
	$(libgnome_autoar_gtk_la_headers)	\
	$(libgnome_autoar_gtk_la_sources)	\
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_la_CFLAGS = \
	$(GTK_CFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_la_LIBADD = \
	$(GTK_LIBS)				\
	$(top_builddir)/gnome-autoar/libgnome-autoar.la	\
	$(NULL)

endif

gnome-autoar/autoar-enum-types.h: gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers) ) > $@
gnome-autoar/autoar-enum-types.c: gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers) ) > $@

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES)
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_builddir)/gnome-autoar
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_builddir)/gnome-autoar

if HAVE_INTROSPECTION

INTROSPECTION_GIRS += gnome-autoar/GnomeAutoar-0.1.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)

gnome-autoar/GnomeAutoar-0.1.gir: gnome-autoar/libgnome-autoar.la
gnome_autoar_GnomeAutoar_0_1_gir_SCANNERFLAGS = --identifier-prefix=Autoar
gnome_autoar_GnomeAutoar_0_1_gir_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
gnome_autoar_GnomeAutoar_0_1_gir_INCLUDES = GLib-2.0 GObject-2.0 Gio-2.0
gnome_autoar_GnomeAutoar_0_1_gir_LIBS = gnome-autoar/libgnome-autoar.la
gnome_autoar_GnomeAutoar_0_1_gir_FILES = \
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)

if ENABLE_GTK

INTROSPECTION_GIRS += gnome-autoar/GnomeAutoarGtk-0.1.gir

gnome-autoar/GnomeAutoarGtk-0.1.gir: gnome-autoar/libgnome-autoar-gtk.la
gnome_autoar_GnomeAutoarGtk_0_1_gir_SCANNERFLAGS = --identifier-prefix=AutoarGtk
gnome_autoar_GnomeAutoarGtk_0_1_gir_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
gnome_autoar_GnomeAutoarGtk_0_1_gir_INCLUDES = GnomeAutoar-0.1 Gtk-3.0
gnome_autoar_GnomeAutoarGtk_0_1_gir_LIBS = gnome-autoar/libgnome-autoar-gtk.la
gnome_autoar_GnomeAutoarGtk_0_1_gir_FILES = \
	$(libgnome_autoar_gtk_la_headers)		\
	$(libgnome_autoar_gtk_la_sources)		\
	$(NULL)

endif

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
	$(NULL)

test_cflags = \
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

if ENABLE_GTK

noinst_PROGRAMS += \
	tests/test-ui		\
	$(NULL)

test_gtk_cflags = \
	$(GTK_CFLAGS)		\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

test_gtk_libs = \
	$(top_builddir)/gnome-autoar/libgnome-autoar-gtk.la	\
	$(top_builddir)/gnome-autoar/libgnome-autoar.la	\
	$(NULL)

tests_test_ui_SOURCES = tests/test-ui.c
tests_test_ui_CFLAGS = $(test_gtk_cflags)
tests_test_ui_LDADD = $(test_gtk_libs)

endif

SUBDIRS = . docs

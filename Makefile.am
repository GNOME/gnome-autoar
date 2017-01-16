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
	$(NULL)

libgnome_autoar_la_headers = \
	gnome-autoar/autoar-compressor.h	\
	gnome-autoar/autoar-extractor.h		\
	gnome-autoar/autoar-format-filter.h	\
	gnome-autoar/autoar-misc.h		\
	gnome-autoar/autoar-mime-types.h	\
	$(NULL)
libgnome_autoar_la_sources = \
	gnome-autoar/autoar-compressor.c	\
	gnome-autoar/autoar-extractor.c		\
	gnome-autoar/autoar-format-filter.c	\
	gnome-autoar/autoar-misc.c		\
	gnome-autoar/autoar-mime-types.c	\
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

lib_LTLIBRARIES = gnome-autoar/libgnome-autoar-@API_VERSION@.la

pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = gnome-autoar/gnome-autoar-@API_VERSION@.pc

autoarincludedir = $(includedir)/gnome-autoar-@API_VERSION@/gnome-autoar
autoarinclude_HEADERS = \
	gnome-autoar/gnome-autoar.h		\
	gnome-autoar/autoar-enum-types.h	\
	$(libgnome_autoar_la_headers)		\
	$(NULL)

gnome_autoar_libgnome_autoar_@API_VERSION@_la_SOURCES = \
	$(libgnome_autoar_la_generated_files)	\
	$(libgnome_autoar_la_private_files)	\
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)
gnome_autoar_libgnome_autoar_@API_VERSION@_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_@API_VERSION@_la_CFLAGS = \
	$(GIO_CFLAGS)				\
	$(LIBARCHIVE_CFLAGS)			\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_@API_VERSION@_la_LDFLAGS = \
	-version-info $(LT_VERSION_INFO)	\
	$(NULL)
gnome_autoar_libgnome_autoar_@API_VERSION@_la_LIBADD = \
	$(GIO_LIBS)				\
	$(LIBARCHIVE_LIBS)			\
	$(NULL)

if ENABLE_GTK

lib_LTLIBRARIES += gnome-autoar/libgnome-autoar-gtk-@API_VERSION@.la
nodist_pkgconfig_DATA += gnome-autoar/gnome-autoar-gtk-@API_VERSION@.pc

autoargtkincludedir = $(includedir)/gnome-autoar-@API_VERSION@/gnome-autoar
autoargtkinclude_HEADERS = \
	gnome-autoar/autoar-gtk.h		\
	$(libgnome_autoar_gtk_la_headers)	\
	$(NULL)

gnome_autoar_libgnome_autoar_gtk_@API_VERSION@_la_SOURCES = \
	$(libgnome_autoar_gtk_la_headers)	\
	$(libgnome_autoar_gtk_la_sources)	\
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_@API_VERSION@_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_@API_VERSION@_la_CFLAGS = \
	$(GTK_CFLAGS)				\
	-I$(top_srcdir)				\
	-I$(top_builddir)/gnome-autoar		\
	$(AM_CFLAGS)				\
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_@API_VERSION@_la_LDFLAGS = \
	-version-info $(LT_VERSION_INFO)	\
	$(NULL)
gnome_autoar_libgnome_autoar_gtk_@API_VERSION@_la_LIBADD = \
	$(GTK_LIBS)				\
	$(top_builddir)/gnome-autoar/libgnome-autoar-@API_VERSION@.la	\
	$(NULL)

endif

gnome-autoar/autoar-enum-types.h: gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.h.template $(libgnome_autoar_la_headers) ) > $@
gnome-autoar/autoar-enum-types.c: gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template gnome-autoar/autoar-enum-types.c.template $(libgnome_autoar_la_headers) ) > $@

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(BUILT_SOURCES) \
                 gnome-autoar/gnome-autoar-gtk-@API_VERSION@.pc \
                 gnome-autoar/gnome-autoar-@API_VERSION@.pc
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(top_builddir)/gnome-autoar
INTROSPECTION_COMPILER_ARGS = --includedir=$(top_builddir)/gnome-autoar

if HAVE_INTROSPECTION

INTROSPECTION_GIRS += gnome-autoar/GnomeAutoar-@API_VERSION@.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)

gnome-autoar/GnomeAutoar-@API_VERSION@.gir: gnome-autoar/libgnome-autoar-@API_VERSION@.la
gnome_autoar_GnomeAutoar_@API_VERSION_U@_gir_SCANNERFLAGS = --identifier-prefix=Autoar --c-include="gnome-autoar/gnome-autoar.h"
gnome_autoar_GnomeAutoar_@API_VERSION_U@_gir_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
gnome_autoar_GnomeAutoar_@API_VERSION_U@_gir_INCLUDES = GLib-2.0 GObject-2.0 Gio-2.0
gnome_autoar_GnomeAutoar_@API_VERSION_U@_gir_LIBS = gnome-autoar/libgnome-autoar-@API_VERSION@.la
gnome_autoar_GnomeAutoar_@API_VERSION_U@_gir_FILES = \
	$(libgnome_autoar_la_headers)		\
	$(libgnome_autoar_la_sources)		\
	$(NULL)

if ENABLE_GTK

INTROSPECTION_GIRS += gnome-autoar/GnomeAutoarGtk-@API_VERSION@.gir

gnome-autoar/GnomeAutoarGtk-@API_VERSION@.gir: gnome-autoar/libgnome-autoar-gtk-@API_VERSION@.la gnome-autoar/GnomeAutoar-@API_VERSION@.gir
gnome_autoar_GnomeAutoarGtk_@API_VERSION_U@_gir_SCANNERFLAGS = --identifier-prefix=AutoarGtk --c-include="gnome-autoar/autoar-gtk.h"
gnome_autoar_GnomeAutoarGtk_@API_VERSION_U@_gir_CFLAGS = -I$(top_srcdir) $(AM_CFLAGS)
gnome_autoar_GnomeAutoarGtk_@API_VERSION_U@_gir_INCLUDES = GnomeAutoar-@API_VERSION@ Gtk-3.0
gnome_autoar_GnomeAutoarGtk_@API_VERSION_U@_gir_LIBS = gnome-autoar/libgnome-autoar-gtk-@API_VERSION@.la
gnome_autoar_GnomeAutoarGtk_@API_VERSION_U@_gir_FILES = \
	$(libgnome_autoar_gtk_la_headers)		\
	$(libgnome_autoar_gtk_la_sources)		\
	$(NULL)

endif

if ENABLE_VAPIGEN
include $(VAPIGEN_MAKEFILE)

gnome-autoar/gnome-autoar-@API_VERSION@.vapi: gnome-autoar/GnomeAutoar-@API_VERSION@.gir

VAPIGEN_VAPIS = gnome-autoar/gnome-autoar-@API_VERSION@.vapi

gnome_autoar_gnome_autoar_@API_VERSION_U@_vapi_DEPS = gio-2.0
gnome_autoar_gnome_autoar_@API_VERSION_U@_vapi_METADATADIRS = $(srcdir)/gnome-autoar
gnome_autoar_gnome_autoar_@API_VERSION_U@_vapi_FILES = gnome-autoar/GnomeAutoar-@API_VERSION@.gir

if ENABLE_GTK
gnome-autoar/gnome-autoar-gtk-@API_VERSION@.vapi: gnome-autoar/GnomeAutoarGtk-@API_VERSION@.gir gnome-autoar/gnome-autoar-@API_VERSION@.vapi

VAPIGEN_VAPIS += gnome-autoar/gnome-autoar-gtk-@API_VERSION@.vapi

gnome_autoar_gnome_autoar_gtk_@API_VERSION_U@_vapi_DEPS = gnome-autoar-@API_VERSION@ gtk+-3.0
gnome_autoar_gnome_autoar_gtk_@API_VERSION_U@_vapi_VAPIDIRS = $(builddir)/gnome-autoar
gnome_autoar_gnome_autoar_gtk_@API_VERSION_U@_vapi_METADATADIRS = $(srcdir)/gnome-autoar
gnome_autoar_gnome_autoar_gtk_@API_VERSION_U@_vapi_FILES = gnome-autoar/GnomeAutoarGtk-@API_VERSION@.gir
gnome_autoar_gnome_autoar_gtk_@API_VERSION_U@_vapi_GIRDIRS = $(builddir)/gnome-autoar
endif

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPIGEN_VAPIS)

CLEANFILES += $(vapi_DATA)
endif

endif


# Directory: data
gsettings_ENUM_NAMESPACE = org.gnome.desktop.archives
gsettings_ENUM_FILES = $(top_srcdir)/gnome-autoar/autoar-format-filter.h

gsettings_SCHEMAS = data/org.gnome.desktop.archives.gschema.xml
EXTRA_DIST += $(gsettings_SCHEMAS)


# Directory: tests
test_cflags = \
	$(GIO_CFLAGS)		\
	$(LIBARCHIVE_CFLAGS)	\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

test_libs = \
	$(GIO_LIBS)		\
	$(top_builddir)/gnome-autoar/libgnome-autoar-@API_VERSION@.la	\
	$(NULL)

test_gtk_cflags = \
	$(GTK_CFLAGS)		\
	-I$(top_srcdir)		\
	$(AM_CFLAGS)		\
	$(NULL)

test_gtk_libs = \
	$(GTK_LIBS)		\
	$(top_builddir)/gnome-autoar/libgnome-autoar-gtk-@API_VERSION@.la	\
	$(top_builddir)/gnome-autoar/libgnome-autoar-@API_VERSION@.la	\
	$(NULL)

noinst_PROGRAMS = \
	tests/test-extract-unit \
	$(NULL)

tests_test_extract_unit_SOURCES = tests/test-extract-unit.c
tests_test_extract_unit_CFLAGS = $(test_cflags)
tests_test_extract_unit_LDADD = $(test_libs)


# Directory: tools
noinst_PROGRAMS += \
	tools/autoar-extract	\
	tools/autoar-create	\
	$(NULL)

tools_autoar_extract_SOURCES = tools/autoar-extract.c
tools_autoar_extract_CFLAGS = $(test_cflags)
tools_autoar_extract_LDADD = $(test_libs)

tools_autoar_create_SOURCES = tools/autoar-create.c
tools_autoar_create_CFLAGS = $(test_cflags)
tools_autoar_create_LDADD = $(test_libs)

if ENABLE_GTK

noinst_PROGRAMS += \
	tools/autoar-gtk	\
	$(NULL)

tools_autoar_gtk_SOURCES = tools/autoar-gtk.c
tools_autoar_gtk_CFLAGS = $(test_gtk_cflags)
tools_autoar_gtk_LDADD = $(test_gtk_libs)

endif

SUBDIRS = . docs

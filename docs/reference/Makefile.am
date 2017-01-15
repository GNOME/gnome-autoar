# vim: set sw=8 ts=8 sts=8 noet:

NULL =

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

DOC_MODULE=gnome-autoar
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml
MKDB_OPTIONS=--xml-mode --output-format=xml

DOC_SOURCE_DIR=$(top_srcdir)/gnome-autoar
HFILE_GLOB=$(top_srcdir)/gnome-autoar/*.h
CFILE_GLOB=$(top_srcdir)/gnome-autoar/*.c

IGNORE_HFILES = \
	gnome-autoar.h \
	autoar-gtk.h \
	autoar-enum-types.h \
	autoar-private.h \
	$(NULL)

IGNORE_CFILES = \
	autoar-enum-types.c \
	autoar-private.c \
	$(NULL)

GTKDOC_CFLAGS = \
	-I$(top_srcdir) \
	$(NULL)

GTKDOC_LIBS = \
	$(top_builddir)/gnome-autoar/libgnome-autoar-@API_VERSION@.la \
	$(NULL)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

if ENABLE_GTK_DOC
TESTS_ENVIRONMENT = cd $(srcdir) && \
  DOC_MODULE=$(DOC_MODULE) DOC_MAIN_SGML_FILE=$(DOC_MAIN_SGML_FILE) \
  SRCDIR=$(abs_srcdir) BUILDDIR=$(abs_builddir)
#TESTS = $(GTKDOC_CHECK)
endif

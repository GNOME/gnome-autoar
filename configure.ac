# vim: set sw=8 ts=8 sts=8 et:
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])

m4_define([major_version], [0])
m4_define([minor_version], [2])
m4_define([micro_version], [0])

AC_INIT([gnome-autoar],
        [major_version.minor_version.micro_version],
        [https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-autoar],
        [gnome-autoar])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([gnome-autoar/gnome-autoar.h])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign dist-xz no-dist-gzip tar-ustar subdir-objects])
AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# reset to 0 at the beginning of a development cycle
# increment by 1 for every stable micro release
m4_define([interface_age], [0])
m4_define([binary_age], [m4_eval(100 * minor_version + micro_version)])
m4_define([lt_current], [m4_eval(binary_age - interface_age)])
m4_define([lt_revision], [interface_age])
m4_define([lt_age], [m4_eval(binary_age - interface_age)])

AC_SUBST([LT_VERSION_INFO], [lt_current:lt_revision:lt_age])
AC_SUBST([API_VERSION], [0.2])
AC_SUBST([API_VERSION_U], [AS_TR_SH([$API_VERSION])])

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
LT_INIT
PKG_PROG_PKG_CONFIG
GLIB_GSETTINGS

GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(GLIB_MKENUMS, glib-mkenums)
AC_PATH_PROG(GLIB_COMPILE_RESOURCES, glib-compile-resources)

# check for gtk-doc
GTK_DOC_CHECK([1.25],[--flavour no-tmpl])

# check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([1.30.0])

VAPIGEN_CHECK([0.22])

# Checks for libraries.
GLIB_REQUIRED=2.35.6
GTK_REQUIRED=3.2
LIBARCHIVE_REQUIRED=3.2.0

PKG_CHECK_MODULES([LIBARCHIVE], [libarchive >= $LIBARCHIVE_REQUIRED],
                  [GNOME_AUTOAR_LIBARCHIVE_REQUIRES="libarchive"
                   AC_SUBST([GNOME_AUTOAR_LIBARCHIVE_REQUIRES])],
                  [AC_SEARCH_LIBS([archive_entry_is_encrypted], [archive],
                  [GNOME_AUTOAR_LIBARCHIVE_LIBS="-larchive"
                   AC_SUBST([GNOME_AUTOAR_LIBARCHIVE_LIBS])],
                  [AC_MSG_ERROR([libarchive not found])])])

PKG_CHECK_MODULES([GIO], [
        glib-2.0    >= $GLIB_REQUIRED
        gobject-2.0 >= $GLIB_REQUIRED
        gio-2.0     >= $GLIB_REQUIRED
        ])

AC_ARG_ENABLE([gtk],
        [AS_HELP_STRING([--enable-gtk], [Build some common GTK+ widgets])],
        [enable_gtk="$enableval"], [enable_gtk=yes])
if test x"$enable_gtk" = xyes; then
        PKG_CHECK_MODULES(
                [GTK], [gtk+-3.0 >= $GTK_REQUIRED], [],
                [AC_MSG_ERROR([

        gtk+-3.0 not found (or version < $GTK_REQUIRED)

        If you want to disable support for common GTK+ widgets,
        please append --disable-gtk to configure.

        ])])
fi
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)
AM_CONDITIONAL(ENABLE_GTK, [test x"$enable_gtk" = xyes])

if test x"$enable_gtk_doc" = xyes && test x"$enable_gtk" '!=' xyes; then
        AC_MSG_ERROR([

        GTK+ widgets support must be enabled to build API documentation.
        Please append --enable-gtk to configure.

        ])
fi

AM_CPPFLAGS="$AM_CPPFLAGS $DEPRECATION_FLAGS"
AM_CFLAGS="$AM_CFLAGS $WARN_CFLAGS $MORE_WARN_FLAGS"
AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_C_INLINE

# Checks for library functions.
AC_CHECK_FUNCS([getgrnam getpwnam link mkfifo mknod stat])

AC_CONFIG_FILES([Makefile
                 docs/Makefile
                 docs/reference/Makefile
                 gnome-autoar/gnome-autoar-$API_VERSION.pc:gnome-autoar/gnome-autoar.pc.in
                 gnome-autoar-gtk/gnome-autoar-gtk-$API_VERSION.pc:gnome-autoar-gtk/gnome-autoar-gtk.pc.in])
AS_MKDIR_P([data])
AC_OUTPUT

echo "
Configuration:

        Source code location    : ${srcdir}
        Prefix                  : ${prefix}
        Compiler                : ${CC}
        CFLAGS                  : ${CFLAGS}

Optional features:

        Build API documentation : ${enable_gtk_doc}
        GTK+ widgets            : ${enable_gtk}
"

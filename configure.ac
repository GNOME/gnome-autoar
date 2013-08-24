# vim: set sw=8 ts=8 sts=8 et:
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([libgnome-autoar], [0.1], [BUG-REPORT-ADDRESS], [libgnome-autoar])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([gnome-autoar/autoar.h])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11 foreign dist-xz no-dist-gzip tar-ustar])
AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
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

# Checks for libraries.
GLIB_REQUIRED=2.35.6
GTK_REQUIRED=3.2
LIBARCHIVE_REQUIRED=3.0.0
PKG_CHECK_MODULES([DEPENDENCIES], [
        glib-2.0    >= $GLIB_REQUIRED
        gobject-2.0 >= $GLIB_REQUIRED
        gio-2.0     >= $GLIB_REQUIRED
        gtk+-3.0    >= $GTK_REQUIRED
        libarchive  >= $LIBARCHIVE_REQUIRED
        ])

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
                 gnome-autoar/Makefile
                 gnome-autoar/gnome-autoar.pc
                 data/Makefile
                 tests/Makefile])
AC_OUTPUT

echo "
Configuration:

        Source code location    : ${srcdir}
        Prefix                  : ${prefix}
        Compiler                : ${CC}
        CFLAGS                  : ${CFLAGS}
"

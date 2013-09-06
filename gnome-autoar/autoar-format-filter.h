/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-format-filter.h
 * Functions related to archive formats and filters
 *
 * Copyright (C) 2013  Ting-Wei Lan
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

#ifndef AUTOAR_FORMAT_H
#define AUTOAR_FORMAT_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  AUTOAR_FORMAT_0, /*< skip >*/
  AUTOAR_FORMAT_ZIP = 1,   /* .zip */
  AUTOAR_FORMAT_TAR,       /* .tar, pax_restricted */
  AUTOAR_FORMAT_CPIO,      /* .cpio, odc */
  AUTOAR_FORMAT_7ZIP,      /* .7z */
  AUTOAR_FORMAT_AR_BSD,    /* .a */
  AUTOAR_FORMAT_AR_SVR4,   /* .a */
  AUTOAR_FORMAT_CPIO_NEWC, /* .cpio, newc */
  AUTOAR_FORMAT_GNUTAR,    /* .tar, gnutar */
  AUTOAR_FORMAT_ISO9660,   /* .iso */
  AUTOAR_FORMAT_PAX,       /* .tar, pax */
  AUTOAR_FORMAT_USTAR,     /* .tar, ustar */
  AUTOAR_FORMAT_XAR,       /* .xar, xar */
  AUTOAR_FORMAT_LAST /*< skip >*/
} AutoarFormat;

typedef enum {
  AUTOAR_FILTER_0, /*< skip >*/
  AUTOAR_FILTER_NONE = 1,
  AUTOAR_FILTER_COMPRESS,  /* .Z */
  AUTOAR_FILTER_GZIP,      /* .gz */
  AUTOAR_FILTER_BZIP2,     /* .bz2 */
  AUTOAR_FILTER_XZ,        /* .xz */
  AUTOAR_FILTER_LZMA,      /* .lzma */
  AUTOAR_FILTER_LZIP,      /* .lz */
  AUTOAR_FILTER_LZOP,      /* .lzo */
  AUTOAR_FILTER_GRZIP,     /* .grz */
  AUTOAR_FILTER_LRZIP,     /* .lrz */
  AUTOAR_FILTER_LAST /*< skip >*/
} AutoarFilter;

AutoarFormat  autoar_format_last                        (void);
gboolean      autoar_format_is_valid                    (AutoarFormat format);
const char   *autoar_format_get_mime_type               (AutoarFormat format);
const char   *autoar_format_get_extension               (AutoarFormat format);
const char   *autoar_format_get_description             (AutoarFormat format);
int           autoar_format_get_format_libarchive       (AutoarFormat format);
gchar        *autoar_format_get_description_libarchive  (AutoarFormat format);

AutoarFilter  autoar_filter_last                        (void);
gboolean      autoar_filter_is_valid                    (AutoarFilter filter);
const char   *autoar_filter_get_mime_type               (AutoarFilter filter);
const char   *autoar_filter_get_extension               (AutoarFilter filter);
const char   *autoar_filter_get_description             (AutoarFilter filter);
int           autoar_filter_get_filter_libarchive       (AutoarFilter filter);
gchar        *autoar_filter_get_description_libarchive  (AutoarFilter filter);

gchar        *autoar_format_filter_get_mime_type        (AutoarFormat format,
                                                         AutoarFilter filter);
gchar        *autoar_format_filter_get_extension        (AutoarFormat format,
                                                         AutoarFilter filter);
gchar        *autoar_format_filter_get_description      (AutoarFormat format,
                                                         AutoarFilter filter);

G_END_DECLS

#endif /* AUTOAR_PREF_H */

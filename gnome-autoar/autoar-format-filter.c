/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-format-filter.c
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

#include "config.h"

#include "autoar-format-filter.h"

#include <archive.h>
#include <gio/gio.h>
#include <glib.h>

typedef struct _AutoarFormatDescription AutoarFormatDescription;
typedef struct _AutoarFilterDescription AutoarFilterDescription;

struct _AutoarFormatDescription
{
  AutoarFormat format;
  int libarchive_format;
  char *extension;
  char *keyword;
  char *mime_type;
  char *description;
};

struct _AutoarFilterDescription
{
  AutoarFilter filter;
  int libarchive_filter;
  char *extension;
  char *keyword;
  char *mime_type;
  char *description;
};

static AutoarFormatDescription autoar_format_description[] = {
  { AUTOAR_FORMAT_ZIP,       ARCHIVE_FORMAT_ZIP,                 "zip",  "zip",
    "application/zip",       "Zip archive" },
  { AUTOAR_FORMAT_TAR,       ARCHIVE_FORMAT_TAR_PAX_RESTRICTED,  "tar",  "tar",
    "application/x-tar",     "Tar archive (restricted pax)" },
  { AUTOAR_FORMAT_CPIO,      ARCHIVE_FORMAT_CPIO_POSIX,          "cpio", "cpio",
    "application/x-cpio",    "CPIO archive" },
  { AUTOAR_FORMAT_7ZIP,      ARCHIVE_FORMAT_7ZIP,                "7z",   "7z-compressed",
    "application/x-7z-compressed", "7-zip archive" },
  { AUTOAR_FORMAT_AR_BSD,    ARCHIVE_FORMAT_AR_BSD,              "a",    "ar",
    "application/x-ar",      "AR archive (BSD)" },
  { AUTOAR_FORMAT_AR_SVR4,   ARCHIVE_FORMAT_AR_GNU,              "a",    "ar",
    "application/x-ar",      "AR archive (SVR4)" },
  { AUTOAR_FORMAT_CPIO_NEWC, ARCHIVE_FORMAT_CPIO_SVR4_NOCRC,     "cpio", "sv4cpio",
    "application/x-sv4cpio", "SV4 CPIO archive" },
  { AUTOAR_FORMAT_GNUTAR,    ARCHIVE_FORMAT_TAR_GNUTAR,          "tar",  "tar",
    "application/x-tar",     "Tar archive (GNU tar)" },
  { AUTOAR_FORMAT_ISO9660,   ARCHIVE_FORMAT_ISO9660,             "iso",  "cd-image",
    "application/x-cd-image", "Raw CD Image" },
  { AUTOAR_FORMAT_PAX,       ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE, "tar",  "tar",
    "application/x-tar",     "Tar archive (pax)" },
  { AUTOAR_FORMAT_USTAR,     ARCHIVE_FORMAT_TAR_USTAR,           "tar",  "tar",
    "application/x-tar",     "Tar archive (ustar)" },
  { AUTOAR_FORMAT_XAR,       ARCHIVE_FORMAT_XAR,                 "xar",  "xar",
    "application/x-xar",     "Xar archive" }
};

static AutoarFilterDescription autoar_filter_description[] = {
  { AUTOAR_FILTER_NONE,      ARCHIVE_FILTER_NONE,                "",     "",
    "",                      "" },
  { AUTOAR_FILTER_COMPRESS,  ARCHIVE_FILTER_COMPRESS,            "Z",    "compress",
    "application/x-compress", "UNIX-compressed" },
  { AUTOAR_FILTER_GZIP,      ARCHIVE_FILTER_GZIP,                "gz",   "gzip",
    "application/gzip",      "Gzip" },
  { AUTOAR_FILTER_BZIP2,     ARCHIVE_FILTER_BZIP2,               "bz2",  "bzip",
    "application/x-bzip",    "Bzip2" },
  { AUTOAR_FILTER_XZ,        ARCHIVE_FILTER_XZ,                  "xz",   "xz",
    "application/x-xz",      "XZ" },
  { AUTOAR_FILTER_LZMA,      ARCHIVE_FILTER_LZMA,                "lzma", "lzma",
    "application/x-lzma",    "LZMA" },
  { AUTOAR_FILTER_LZIP,      ARCHIVE_FILTER_LZIP,                "lz",   "lzip",
    "application/x-lzip",    "Lzip" },
  { AUTOAR_FILTER_LZOP,      ARCHIVE_FILTER_LZOP,                "lzo",  "lzop",
    "application/x-lzop",    "LZO" },
  { AUTOAR_FILTER_GRZIP,     ARCHIVE_FILTER_GRZIP,               "grz",  "grzip",
    "application/x-grzip",   "GRZip" },
  { AUTOAR_FILTER_LRZIP,     ARCHIVE_FILTER_LRZIP,               "lrz",  "lrzip",
    "application/x-lrzip",   "Long Range ZIP (lrzip)" }
};

const char*
autoar_format_get_mime_type (AutoarFormat format)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  return autoar_format_description[format - 1].mime_type;
}

const char*
autoar_format_get_extension (AutoarFormat format)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  return autoar_format_description[format - 1].extension;
}

const char*
autoar_format_get_description (AutoarFormat format)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  return autoar_format_description[format - 1].description;
}

int
autoar_format_get_format_libarchive (AutoarFormat format)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, -1);
  return autoar_format_description[format - 1].libarchive_format;
}

gchar*
autoar_format_get_description_libarchive (AutoarFormat format)
{
  struct archive* a;
  gchar *str;

  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);

  a = archive_write_new ();
  archive_write_set_format (a, autoar_format_description[format - 1].libarchive_format);
  str = g_strdup (archive_format_name (a));
  archive_write_free (a);

  return str;
}

const char*
autoar_filter_get_mime_type (AutoarFilter filter)
{
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);
  return autoar_filter_description[filter - 1].mime_type;
}

const char*
autoar_filter_get_extension (AutoarFilter filter)
{
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);
  return autoar_filter_description[filter - 1].extension;
}

const char*
autoar_filter_get_description (AutoarFilter filter)
{
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);
  return autoar_filter_description[filter - 1].description;
}

int
autoar_filter_get_filter_libarchive (AutoarFilter filter)
{
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, -1);
  return autoar_filter_description[filter - 1].libarchive_filter;
}

gchar*
autoar_filter_get_description_libarchive (AutoarFilter filter)
{
  struct archive *a;
  gchar *str;

  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);

  a = archive_write_new ();
  archive_write_add_filter (a, autoar_filter_description[filter - 1].libarchive_filter);
  str = g_strdup (archive_filter_name (a, 0));
  archive_write_free (a);

  return str;
}

gchar*
autoar_format_filter_get_mime_type (AutoarFormat format,
                                    AutoarFilter filter)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);

  switch (filter) {
    case AUTOAR_FILTER_NONE:
      return g_strdup (autoar_format_description[format - 1].mime_type);
    case AUTOAR_FILTER_COMPRESS:
      return g_strconcat ("application/x-",
                          autoar_format_description[format - 1].keyword,
                          "z", NULL);
    case AUTOAR_FILTER_GZIP:
      return g_strconcat ("application/x-compressed-",
                          autoar_format_description[format - 1].keyword,
                          NULL);
    default:
      return g_strconcat ("application/x-",
                          autoar_filter_description[filter - 1].keyword,
                          "-compressed-",
                          autoar_format_description[format - 1].keyword,
                          NULL);
  }
}

gchar*
autoar_format_filter_get_extension (AutoarFormat format,
                                    AutoarFilter filter)
{
  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);

  return g_strconcat (".",
                      autoar_format_description[format - 1].extension,
                      autoar_filter_description[filter - 1].extension[0] ? "." : "",
                      autoar_filter_description[filter - 1].extension,
                      NULL);
}

gchar*
autoar_format_filter_get_description (AutoarFormat format,
                                      AutoarFilter filter)
{
  gchar *mime_type;
  gchar *description;

  g_return_val_if_fail (format > 0 && format < AUTOAR_FORMAT_LAST, NULL);
  g_return_val_if_fail (filter > 0 && filter < AUTOAR_FILTER_LAST, NULL);

  mime_type = autoar_format_filter_get_mime_type (format, filter);
  description = g_content_type_get_description (mime_type);
  g_free (mime_type);

  return description;
}

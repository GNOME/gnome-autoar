/*
 * SPDX-FileCopyrightText: 2016  Razvan Chitu <razvan.ch95@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Functions for checking autoar support for various mime types
 */

#define G_LOG_DOMAIN "gnome-autoar"

#include "autoar-mime-types.h"

static gchar *supported_mime_types[] = {
  "application/x-7z-compressed",
  "application/x-7z-compressed-tar",
  "application/x-bzip",
  "application/x-bzip-compressed-tar",
  "application/x-compress",
  "application/x-compressed-tar",
  "application/x-cpio",
  "application/x-lha",
  "application/x-lzip",
  "application/x-lzip-compressed-tar",
  "application/x-lzma",
  "application/x-lzma-compressed-tar",
  "application/x-tar",
  "application/x-tarz",
  "application/x-xar",
  "application/x-xz",
  "application/x-xz-compressed-tar",
  "application/zip",
  "application/gzip",
  "application/bzip2",
  "application/x-bzip2-compressed-tar",
  "application/vnd.rar",
  "application/zstd",
  "application/x-zstd-compressed-tar",
  NULL
};

/**
 * autoar_check_mime_type_supported:
 * @mime_type: a string representing the mime type
 *
 * Checks whether a mime type is supported by autoar. This function does no
 * blocking IO.
 *
 * Returns: %TRUE if the mime type is supported
 **/
gboolean
autoar_check_mime_type_supported (const gchar *mime_type)
{
  gint i;
  for (i = 0; supported_mime_types[i] != NULL; ++i) {
    if (g_content_type_equals (supported_mime_types[i], mime_type))
      return TRUE;
  }

  return FALSE;
}

/**
 * autoar_query_mime_type_supported:
 * @file: a #GFile to check if its mime type is supported
 *
 * This function will query the file's mime type and then call
 * autoar_check_mime_type_supported(), so it does blocking IO.
 *
 * Returns: %TRUE if the mime type of the #GFile is supported
 **/
gboolean
autoar_query_mime_type_supported (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;

  g_return_val_if_fail (G_IS_FILE (file), FALSE);

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                            NULL, NULL);

  g_return_val_if_fail (G_IS_FILE_INFO (info), FALSE);

  return autoar_check_mime_type_supported (g_file_info_get_content_type (info));
}

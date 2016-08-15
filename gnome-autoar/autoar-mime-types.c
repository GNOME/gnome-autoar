/*
 * autoar-mime-types.h
 * Functions for checking autoar support for various mime types
 *
 * Copyright (C) 2016  Razvan Chitu
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

 #include "autoar-mime-types.h"

static gchar *supported_mime_types[] = {
  "application/x-7z-compressed",
  "application/x-7z-compressed-tar",
  "application/x-bzip",
  "application/x-bzip-compressed-tar",
  "application/x-compress",
  "application/x-compressed-tar",
  "application/x-cpio",
  "application/x-gzip",
  "application/x-lha",
  "application/x-lzip",
  "application/x-lzip-compressed-tar",
  "application/x-lzma",
  "application/x-lzma-compressed-tar",
  "application/x-rar",
  "application/x-tar",
  "application/x-tarz",
  "application/x-xar",
  "application/x-xz",
  "application/x-xz-compressed-tar",
  "application/zip",
  "application/gzip",
  "application/bzip2",
  NULL
};

static GHashTable *supported_mime_types_table = NULL;

static void
initialize_supported_mime_types_table (void)
{
  int i;

  supported_mime_types_table = g_hash_table_new (g_str_hash,
                                                 g_str_equal);

  for (i = 0; supported_mime_types[i] != NULL; ++i) {
    g_hash_table_add (supported_mime_types_table,
                      supported_mime_types[i]);
  }
}

/**
 * autoar_check_mime_type_supported:
 * @mime_type: a string representing the mime type
 *
 * Checks whether a mime type is supported by autoar. This function does no
 * blocking IO.
 *
 * Returns: an #AutoarFilter
 **/
gboolean
autoar_check_mime_type_supported (const gchar *mime_type)
{
  if (supported_mime_types_table == NULL) {
    initialize_supported_mime_types_table ();
  }

  return g_hash_table_contains (supported_mime_types_table, mime_type);
}

/**
 * autoar_query_mime_type_supported:
 * @file: a #GFile to check if its mime type is supported
 *
 * This function will query the file's mime type and then call
 * autoar_check_mime_type_supported(), so it does blocking IO.
 *
 * Returns: an #AutoarFilter
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

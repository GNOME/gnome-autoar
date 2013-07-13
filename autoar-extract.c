/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-extract.c
 * Automatically extract archives in some GNOME programs
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

#include "autoar-extract.h"

#include <archive.h>
#include <archive_entry.h>
#include <gio/gio.h>
#include <string.h>

G_DEFINE_TYPE (AutoarExtract, autoar_extract, G_TYPE_OBJECT)

#define AUTOAR_EXTRACT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_EXTRACT, AutoarExtractPrivate))

#define BUFFER_SIZE (64 * 1024)

struct _AutoarExtractPrivate
{
  char *source;
  char *output;

  guint64 size;
  guint64 completed_size;

  guint files;
  guint completed_files;

  GInputStream *istream;
  void         *buffer;
  gssize        buffer_size;
  GError       *error;
};

enum
{
  PROP_0,
  PROP_SOURCE,
  PROP_OUTPUT,
  PROP_SIZE,
  PROP_COMPLETED_SIZE,
  PROP_FILES,
  PROP_COMPLETED_FILES
};

static void
autoar_extract_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  arextract = AUTOAR_EXTRACT (object);
  priv = arextract->priv;

  switch (property_id) {
    case PROP_SOURCE:
      g_value_set_string (value, priv->source);
      break;
    case PROP_OUTPUT:
      g_value_set_string (value, priv->output);
      break;
    case PROP_SIZE:
      g_value_set_uint64 (value, priv->size);
      break;
    case PROP_COMPLETED_SIZE:
      g_value_set_uint64 (value, priv->completed_size);
      break;
    case PROP_FILES:
      g_value_set_uint (value, priv->files);
      break;
    case PROP_COMPLETED_FILES:
      g_value_set_uint (value, priv->completed_files);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
autoar_extract_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  arextract = AUTOAR_EXTRACT (object);
  priv = arextract->priv;

  switch (property_id) {
    case PROP_SIZE:
      autoar_extract_set_size (arextract, g_value_get_uint64 (value));
      break;
    case PROP_COMPLETED_SIZE:
      autoar_extract_set_completed_size (arextract, g_value_get_uint64 (value));
      break;
    case PROP_FILES:
      autoar_extract_set_files (arextract, g_value_get_uint (value));
      break;
    case PROP_COMPLETED_FILES:
      autoar_extract_set_completed_files (arextract, g_value_get_uint (value));
      break;
    case PROP_SOURCE:
      g_free (priv->source);
      priv->source = g_value_dup_string (value);
      break;
    case PROP_OUTPUT:
      g_free (priv->output);
      priv->output = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

char*
autoar_extract_get_source (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->source;
}

char*
autoar_extract_get_output (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->output;
}

guint64
autoar_extract_get_size (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->size;
}

guint64
autoar_extract_get_completed_size (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->completed_size;
}

guint
autoar_extract_get_files (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->files;
}

guint
autoar_extract_get_completed_files (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->completed_files;
}

void
autoar_extract_set_size (AutoarExtract *arextract,
                         guint64 size)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->priv->size = size;
}

void
autoar_extract_set_completed_size (AutoarExtract *arextract,
                                   guint64 completed_size)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  g_return_if_fail (completed_size <= arextract->priv->completed_size);
  arextract->priv->completed_size = completed_size;
}

void
autoar_extract_set_files (AutoarExtract *arextract,
                          guint files)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->priv->files = files;
}

void
autoar_extract_set_completed_files (AutoarExtract *arextract,
                                    guint completed_files)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  g_return_if_fail (completed_files <= arextract->priv->completed_files);
  arextract->priv->completed_files = completed_files;
}

static void
autoar_extract_finalize (GObject *object)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  arextract = AUTOAR_EXTRACT (object);
  priv = arextract->priv;

  g_free (priv->source);
  priv->source = NULL;

  g_free (priv->output);
  priv->output = NULL;

  if (priv->istream != NULL) {
    if (!g_input_stream_is_closed (priv->istream)) {
      g_input_stream_close (priv->istream, NULL, NULL);
    }
    g_object_unref (priv->istream);
  }

  g_free (priv->buffer);
  priv->buffer = NULL;

  if (priv->error != NULL) {
    g_error_free (priv->error);
    priv->error = NULL;
  }

  G_OBJECT_CLASS (autoar_extract_parent_class)->finalize (object);
}

static int
libarchive_read_open_cb (struct archive *ar_read,
                         void *client_data)
{
  AutoarExtract *arextract;
  GFile *file;
  GFileInfo *fileinfo;

  arextract = (AutoarExtract*)client_data;
  if (arextract->priv->error != NULL) {
    return ARCHIVE_FATAL;
  }

  file = g_file_new_for_commandline_arg (arextract->priv->source);

  fileinfo = g_file_query_info (file,
                                G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                NULL,
                                &(arextract->priv->error));
  g_return_val_if_fail (arextract->priv->error == NULL, ARCHIVE_FATAL);

  arextract->priv->size = g_file_info_get_size (fileinfo);
  g_object_unref (fileinfo);

  arextract->priv->istream = (GInputStream*)g_file_read (file,
                                                         NULL,
                                                         &(arextract->priv->error));
  g_return_val_if_fail (arextract->priv->error == NULL, ARCHIVE_FATAL);

  return ARCHIVE_OK;
}

static int
libarchive_read_close_cb (struct archive *ar_read,
                          void *client_data)
{
  AutoarExtract *arextract;

  arextract = (AutoarExtract*)client_data;
  if (arextract->priv->error != NULL) {
    return ARCHIVE_FATAL;
  }

  if (arextract->priv->istream != NULL) {
    g_input_stream_close (arextract->priv->istream, NULL, NULL);
    g_object_unref (arextract->priv->istream);
    arextract->priv->istream = NULL;
  }

  return ARCHIVE_OK;
}

static ssize_t
libarchive_read_read_cb (struct archive *ar_read,
                         void *client_data,
                         const void **buffer)
{
  AutoarExtract *arextract;
  gssize read_size;

  arextract = (AutoarExtract*)client_data;
  if (arextract->priv->error != NULL) {
    return -1;
  }

  *buffer = &(arextract->priv->buffer);
  read_size = g_input_stream_read (arextract->priv->istream,
                                   arextract->priv->buffer,
                                   arextract->priv->buffer_size,
                                   NULL,
                                   &(arextract->priv->error));
  g_return_val_if_fail (arextract->priv->error == NULL, -1);

  arextract->priv->completed_size += read_size;

  return read_size;
}

static char*
_g_filename_basename_remove_extension (const char *filename)
{
  char *dot_location;
  char *basename;

  if (filename == NULL) {
    return NULL;
  }

  /* filename must not be directory, so we do not get a bad basename. */
  basename = g_path_get_basename (filename);

  dot_location = strrchr (basename, '.');
  if (dot_location == NULL || dot_location == basename) {
    return basename;
  }

  if (dot_location - 4 > basename) {
    if (strcmp (dot_location - 4, ".tar") == 0) {
      dot_location -= 4;
    }
  }

  *dot_location = '\0';

  return basename;
}

static gboolean
archive_extract_do_pattern_check (const char *pathname)
{
  return TRUE;
}

static void
archive_extract_do_write_entry (AutoarExtract *arextract,
                                struct archive *a,
                                struct archive_entry *entry,
                                GFile *dest)
{
}

/* Additional marshaller generated by glib-genmarshal
 * Command: echo "VOID:DOUBLE,DOUBLE" | glib-genmarshal --header */

/* VOID:DOUBLE,DOUBLE (/dev/stdin:1) */
extern void g_cclosure_user_marshal_VOID__DOUBLE_DOUBLE (GClosure     *closure,
                                                         GValue       *return_value,
                                                         guint         n_param_values,
                                                         const GValue *param_values,
                                                         gpointer      invocation_hint,
                                                         gpointer      marshal_data);

/* Additional marshaller generated by glib-genmarshal
 * Command: echo "VOID:DOUBLE,DOUBLE" | glib-genmarshal --body */

#ifdef G_ENABLE_DEBUG
#define g_marshal_value_peek_boolean(v)  g_value_get_boolean (v)
#define g_marshal_value_peek_char(v)     g_value_get_schar (v)
#define g_marshal_value_peek_uchar(v)    g_value_get_uchar (v)
#define g_marshal_value_peek_int(v)      g_value_get_int (v)
#define g_marshal_value_peek_uint(v)     g_value_get_uint (v)
#define g_marshal_value_peek_long(v)     g_value_get_long (v)
#define g_marshal_value_peek_ulong(v)    g_value_get_ulong (v)
#define g_marshal_value_peek_int64(v)    g_value_get_int64 (v)
#define g_marshal_value_peek_uint64(v)   g_value_get_uint64 (v)
#define g_marshal_value_peek_enum(v)     g_value_get_enum (v)
#define g_marshal_value_peek_flags(v)    g_value_get_flags (v)
#define g_marshal_value_peek_float(v)    g_value_get_float (v)
#define g_marshal_value_peek_double(v)   g_value_get_double (v)
#define g_marshal_value_peek_string(v)   (char*) g_value_get_string (v)
#define g_marshal_value_peek_param(v)    g_value_get_param (v)
#define g_marshal_value_peek_boxed(v)    g_value_get_boxed (v)
#define g_marshal_value_peek_pointer(v)  g_value_get_pointer (v)
#define g_marshal_value_peek_object(v)   g_value_get_object (v)
#define g_marshal_value_peek_variant(v)  g_value_get_variant (v)
#else /* !G_ENABLE_DEBUG */
/* WARNING: This code accesses GValues directly, which is UNSUPPORTED API.
 *          Do not access GValues directly in your code. Instead, use the
 *          g_value_get_*() functions
 */
#define g_marshal_value_peek_boolean(v)  (v)->data[0].v_int
#define g_marshal_value_peek_char(v)     (v)->data[0].v_int
#define g_marshal_value_peek_uchar(v)    (v)->data[0].v_uint
#define g_marshal_value_peek_int(v)      (v)->data[0].v_int
#define g_marshal_value_peek_uint(v)     (v)->data[0].v_uint
#define g_marshal_value_peek_long(v)     (v)->data[0].v_long
#define g_marshal_value_peek_ulong(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_int64(v)    (v)->data[0].v_int64
#define g_marshal_value_peek_uint64(v)   (v)->data[0].v_uint64
#define g_marshal_value_peek_enum(v)     (v)->data[0].v_long
#define g_marshal_value_peek_flags(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_float(v)    (v)->data[0].v_float
#define g_marshal_value_peek_double(v)   (v)->data[0].v_double
#define g_marshal_value_peek_string(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_param(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_boxed(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_pointer(v)  (v)->data[0].v_pointer
#define g_marshal_value_peek_object(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_variant(v)  (v)->data[0].v_pointer
#endif /* !G_ENABLE_DEBUG */


/* VOID:DOUBLE,DOUBLE (/dev/stdin:1) */
void
g_cclosure_user_marshal_VOID__DOUBLE_DOUBLE (GClosure     *closure,
                                             GValue       *return_value G_GNUC_UNUSED,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      invocation_hint G_GNUC_UNUSED,
                                             gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__DOUBLE_DOUBLE) (gpointer     data1,
                                                    gdouble      arg_1,
                                                    gdouble      arg_2,
                                                    gpointer     data2);
  register GMarshalFunc_VOID__DOUBLE_DOUBLE callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_VOID__DOUBLE_DOUBLE) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            g_marshal_value_peek_double (param_values + 1),
            g_marshal_value_peek_double (param_values + 2),
            data2);
}


static void
autoar_extract_class_init (AutoarExtractClass *klass)
{
  GObjectClass *object_class;
  GType type;

  object_class = G_OBJECT_CLASS (klass);
  type = G_TYPE_FROM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (AutoarExtractPrivate));

  object_class->get_property = autoar_extract_get_property;
  object_class->set_property = autoar_extract_set_property;
  object_class->finalize = autoar_extract_finalize;

  g_object_class_install_property (object_class, PROP_SOURCE,
                                   g_param_spec_string ("source",
                                                        "Source archive",
                                                        "The archive file to be extracted",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output directory",
                                                        "Output directory of extracted archive",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_SIZE,
                                   g_param_spec_uint64 ("size",
                                                        "File size",
                                                        "Size of the archive file",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_COMPLETED_SIZE,
                                   g_param_spec_uint64 ("completed-size",
                                                        "Read file size",
                                                        "Bytes read from the archive",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_COMPLETED_FILES,
                                   g_param_spec_uint ("completed-files",
                                                      "Written files",
                                                      "Number of files has been written",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_signal_new ("scaned",
                type,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (AutoarExtractClass, scaned),
                NULL, NULL,
                g_cclosure_marshal_VOID__UINT,
                1,
                G_TYPE_UINT);

  g_signal_new ("progress",
                type,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (AutoarExtractClass, progress),
                NULL, NULL,
                g_cclosure_user_marshal_VOID__DOUBLE_DOUBLE,
                G_TYPE_NONE,
                2,
                G_TYPE_DOUBLE,
                G_TYPE_DOUBLE);

  g_signal_new ("completed",
                type,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (AutoarExtractClass, completed),
                NULL, NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE,
                0);

  g_signal_new ("error",
                type,
                G_SIGNAL_RUN_LAST,
                G_STRUCT_OFFSET (AutoarExtractClass, error),
                NULL, NULL,
                g_cclosure_marshal_VOID__POINTER,
                G_TYPE_NONE,
                1,
                G_TYPE_POINTER);
}

static void
autoar_extract_init (AutoarExtract *arextract)
{
  AutoarExtractPrivate *priv;

  priv = AUTOAR_EXTRACT_GET_PRIVATE (arextract);
  arextract->priv = priv;

  priv->source = NULL;
  priv->output = NULL;

  priv->size = 0;
  priv->completed_size = 0;

  priv->files = 0;
  priv->completed_files = 0;

  priv->istream = NULL;
  priv->buffer_size = BUFFER_SIZE;
  priv->buffer = g_new (char, priv->buffer_size);
  priv->error = NULL;
}

AutoarExtract*
autoar_extract_new (const char *source,
                    const char *output)
{
  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (output != NULL, NULL);

  return g_object_new (AUTOAR_TYPE_EXTRACT,
                       "source",
                       source,
                       "output",
                       output,
                       NULL);
}

void
autoar_extract_start (AutoarExtract* arextract)
{
  struct archive *a;
  struct archive_entry *entry;

  char *pathname_prefix;
  int pathname_prefix_len;

  gboolean has_top_level_dir;
  char *top_level_dir_basename;
  char *top_level_dir_basename_modified;
  GFile *top_level_parent_dir;
  GFile *top_level_dir;

  GFile *source;

  int i;

  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  g_return_if_fail (arextract->priv->source != NULL);
  g_return_if_fail (arextract->priv->output != NULL);

  a = archive_read_new ();
  archive_read_support_filter_all (a);
  archive_read_support_format_all (a);

  /* Reset all counter variables */
  arextract->priv->size = 0;
  arextract->priv->completed_size = 0;
  arextract->priv->files = 0;
  arextract->priv->completed_files = 0;

  /* Step 1: Scan all file names in the archive
   * We have to check whether the archive contains a top-level directory
   * before performing the extraction. We emit the "scanned" signal when
   * the checking is completed. */
  archive_read_open (a,
                     arextract,
                     libarchive_read_open_cb,
                     libarchive_read_read_cb,
                     libarchive_read_close_cb);
  pathname_prefix = NULL;
  has_top_level_dir = TRUE;
  while (archive_read_next_header (a, &entry) == ARCHIVE_OK) {
    const char *pathname, *dir_sep_location;
    size_t skip_len, prefix_len;

    pathname = archive_entry_pathname (entry);

    /* TODO: Add file name pattern check here */

    if (pathname_prefix == NULL) {
      skip_len = strspn (pathname, "./");
      dir_sep_location = strchr (pathname + skip_len, '/');
      if (dir_sep_location == NULL) {
        prefix_len = strlen (pathname);
      } else {
        prefix_len = dir_sep_location - pathname;
      }
      pathname_prefix = g_strndup (pathname, prefix_len);
      pathname_prefix_len = prefix_len;
    } else {
      if (!g_str_has_prefix (pathname, pathname_prefix)) {
        has_top_level_dir = FALSE;
      }
    }
    archive_read_data_skip (a);
    arextract->priv->files++;
  }
  g_free (pathname_prefix);
  archive_read_close (a);
  if (arextract->priv->error != NULL) {
    g_signal_emit_by_name (arextract, "error", arextract->priv->error);
    archive_read_free (a);
    return;
  }
  g_signal_emit_by_name (arextract, "scanned");

  /* Step 2: Create necessary directories */
  top_level_dir_basename = _g_filename_basename_remove_extension (arextract->priv->source);
  top_level_parent_dir = g_file_new_for_commandline_arg (arextract->priv->output);
  top_level_dir = g_file_get_child (top_level_parent_dir, top_level_dir_basename);

  top_level_dir_basename_modified = NULL;
  for (i=1; g_file_query_exists (top_level_dir, NULL); i++) {
    g_free (top_level_dir_basename_modified);
    g_object_unref (top_level_dir);
    top_level_dir_basename_modified = g_strdup_printf ("%s (%d)",
                                                       top_level_dir_basename,
                                                       i);
    top_level_dir = g_file_get_child (top_level_parent_dir,
                                      top_level_dir_basename_modified);
  }

  g_file_make_directory_with_parents (top_level_dir, NULL, &(arextract->priv->error));

  g_free (top_level_dir_basename);
  g_free (top_level_dir_basename_modified);
  g_object_unref (top_level_parent_dir);

  if (arextract->priv->error != NULL) {
    g_signal_emit_by_name (arextract, "error", arextract->priv->error);
    g_object_unref (top_level_dir);
    archive_read_free (a);
    return;
  }

  /* Step 3: Extract files
   * We have to re-open the archive to extract files */
  archive_read_open (a,
                     arextract,
                     libarchive_read_open_cb,
                     libarchive_read_read_cb,
                     libarchive_read_close_cb);
  while (archive_read_next_header (a, &entry) == ARCHIVE_OK) {
    const char *pathname;
    const char *pathname_skip_prefix;
    char **pathname_chunks;

    GFile *extracted_filename;
    GFileInfo *restored_fileinfo;

    pathname = archive_entry_pathname (entry);
    if (has_top_level_dir)
      pathname_skip_prefix = pathname + pathname_prefix_len;
    else
      pathname_skip_prefix = pathname + strspn (pathname, "./");

    extracted_filename = g_file_get_child (top_level_dir, pathname_skip_prefix);

    /* Extracted file should not be located outside the top level directory. */
    if (!g_file_has_prefix (extracted_filename, top_level_dir)) {
      pathname_chunks = g_strsplit (pathname_skip_prefix, "/", G_MAXINT);
      for (i = 0; pathname_chunks[i] != NULL; i++) {
        if (strcmp (pathname_chunks[i], "..") == 0) {
          char *pathname_sanitized;

          *pathname_chunks[i] = '\0';
          pathname_sanitized = g_strjoinv ("/", pathname_chunks);

          g_object_unref (extracted_filename);
          extracted_filename = g_file_get_child (top_level_dir, pathname_sanitized);

          g_free (pathname_sanitized);

          if (g_file_has_prefix (extracted_filename, top_level_dir))
            break;
        }
      }
      g_strfreev (pathname_chunks);
    }

    /* TODO: Add file name pattern check here */
    archive_extract_do_pattern_check (pathname_skip_prefix);

    /* TODO: Write entry to disk */
    archive_extract_do_write_entry (arextract, a, entry, extracted_filename);

    arextract->priv->completed_files++;
    g_signal_emit_by_name (arextract,
                           "progress",
                           ((double)(arextract->priv->size)) / ((double)(arextract->priv->completed_size)),
                           ((double)(arextract->priv->files)) / ((double)(arextract->priv->completed_files)));
    g_object_unref (extracted_filename);
  }

  g_object_unref (top_level_dir);
  archive_read_close (a);
  archive_read_free (a);
  if (arextract->priv->error != NULL) {
    g_signal_emit_by_name (arextract, "error", arextract->priv->error);
    return;
  }

  /* If the extraction is completed successfully, remove the source file.
   * Errors are not fatal because we have completed our work. */
  source = g_file_new_for_commandline_arg (arextract->priv->source);
  g_file_delete (source, NULL, NULL);
  g_object_unref (source);
  g_signal_emit_by_name (arextract, "completed");
}

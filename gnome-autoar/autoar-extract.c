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

#include "autoar-common.h"
#include "autoar-pref.h"

#include <archive.h>
#include <archive_entry.h>
#include <gio/gio.h>
#include <gobject/gvaluecollector.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined HAVE_MKFIFO || defined HAVE_MKNOD
# include <fcntl.h>
#endif

#ifdef HAVE_GETPWNAM
# include <pwd.h>
#endif

#ifdef HAVE_GETGRNAM
# include <grp.h>
#endif


G_DEFINE_TYPE (AutoarExtract, autoar_extract, G_TYPE_OBJECT)
G_DEFINE_QUARK (autoar-extract, autoar_extract)

#define AUTOAR_EXTRACT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_EXTRACT, AutoarExtractPrivate))

#define BUFFER_SIZE (64 * 1024)
#define NOT_AN_ARCHIVE_ERRNO 2013

typedef struct _GFileAndInfo GFileAndInfo;

struct _AutoarExtractPrivate
{
  /* Variables from user input */
  char *source;
  char *output;

  GFile *source_file;
  GFile *output_file;

  int source_is_mem  : 1;
  int output_is_dest : 1;

  AutoarPref *arpref;

  const void *source_buffer;
  gsize source_buffer_size;

  GCancellable *cancellable;

  /* Variables used to show progess */
  guint64 size;
  guint64 completed_size;

  guint files;
  guint completed_files;

  /* Internal variables */
  GInputStream *istream;
  void         *buffer;
  gssize        buffer_size;
  GError       *error;

  GHashTable *userhash;
  GHashTable *grouphash;
  GHashTable *bad_filename;
  GPtrArray  *pattern_compiled;
  GArray     *extracted_dir_list;
  GFile      *top_level_dir;

  int   pathname_prefix_len;
  char *pathname_basename;
  char *suggested_destname;

  int in_thread         : 1;
  int use_raw_format    : 1;
  int has_top_level_dir : 1;
  int has_only_one_file : 1;
};

struct _GFileAndInfo
{
  GFile *file;
  GFileInfo *info;
};

enum
{
  SCANNED,
  DECIDE_DEST,
  PROGRESS,
  CANCELLED,
  COMPLETED,
  ERROR,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SOURCE,           /* Only used to display messages */
  PROP_SOURCE_FILE,      /* It may be invalid if source-is-mem is TRUE */
  PROP_OUTPUT,           /* Only used to display messages */
  PROP_OUTPUT_FILE,
  PROP_SIZE,
  PROP_COMPLETED_SIZE,
  PROP_FILES,
  PROP_COMPLETED_FILES,
  PROP_SOURCE_IS_MEM,    /* Must be set when constructing object */
  PROP_OUTPUT_IS_DEST
};

static guint autoar_extract_signals[LAST_SIGNAL] = { 0 };

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
    case PROP_SOURCE_FILE:
      g_value_set_object (value, priv->source_file);
      break;
    case PROP_OUTPUT:
      g_value_set_string (value, priv->output);
      break;
    case PROP_OUTPUT_FILE:
      g_value_set_object (value, priv->output_file);
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
    case PROP_SOURCE_IS_MEM:
      g_value_set_boolean (value, priv->source_is_mem);
      break;
    case PROP_OUTPUT_IS_DEST:
      g_value_set_boolean (value, priv->output_is_dest);
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
    case PROP_SOURCE:
      g_free (priv->source);
      priv->source = g_value_dup_string (value);
      break;
    case PROP_SOURCE_FILE:
      g_clear_object (&(priv->source_file));
      priv->source_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_OUTPUT:
      g_free (priv->output);
      priv->output = g_value_dup_string (value);
      break;
    case PROP_OUTPUT_FILE:
      g_clear_object (&(priv->output_file));
      priv->output_file = g_object_ref (g_value_get_object (value));
      break;
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
    case PROP_SOURCE_IS_MEM:
      priv->source_is_mem = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_IS_DEST:
      autoar_extract_set_output_is_dest (arextract, g_value_get_boolean (value));
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

GFile*
autoar_extract_get_source_file (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->source_file;
}

char*
autoar_extract_get_output (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->output;
}

GFile*
autoar_extract_get_output_file (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->output_file;
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

gboolean
autoar_extract_get_source_is_mem (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), FALSE);
  return arextract->priv->source_is_mem;
}

gboolean
autoar_extract_get_output_is_dest (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), FALSE);
  return arextract->priv->source_is_mem;
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

void
autoar_extract_set_output_is_dest  (AutoarExtract *arextract,
                                    gboolean output_is_dest)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->priv->output_is_dest = output_is_dest;
}

static void
autoar_extract_dispose (GObject *object)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  arextract = AUTOAR_EXTRACT (object);
  priv = arextract->priv;

  g_debug ("AutoarExtract: dispose");

  if (priv->istream != NULL) {
    if (!g_input_stream_is_closed (priv->istream)) {
      g_input_stream_close (priv->istream, priv->cancellable, NULL);
    }
    g_object_unref (priv->istream);
    priv->istream = NULL;
  }

  g_clear_object (&(priv->source_file));
  g_clear_object (&(priv->output_file));
  g_clear_object (&(priv->arpref));
  g_clear_object (&(priv->top_level_dir));
  g_clear_object (&(priv->cancellable));

  if (priv->userhash != NULL) {
    g_hash_table_unref (priv->userhash);
    priv->userhash = NULL;
  }

  if (priv->grouphash != NULL) {
    g_hash_table_unref (priv->grouphash);
    priv->grouphash = NULL;
  }

  if (priv->bad_filename != NULL) {
    g_hash_table_unref (priv->bad_filename);
    priv->bad_filename = NULL;
  }

  if (priv->pattern_compiled != NULL) {
    g_ptr_array_unref (priv->pattern_compiled);
    priv->pattern_compiled = NULL;
  }

  if (priv->extracted_dir_list != NULL) {
    g_array_unref (priv->extracted_dir_list);
    priv->extracted_dir_list = NULL;
  }

  G_OBJECT_CLASS (autoar_extract_parent_class)->dispose (object);
}

static void
autoar_extract_finalize (GObject *object)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  arextract = AUTOAR_EXTRACT (object);
  priv = arextract->priv;

  g_debug ("AutoarExtract: finalize");

  g_free (priv->source);
  priv->source = NULL;

  g_free (priv->output);
  priv->output = NULL;

  g_free (priv->buffer);
  priv->buffer = NULL;

  if (priv->error != NULL) {
    g_error_free (priv->error);
    priv->error = NULL;
  }

  g_free (priv->pathname_basename);
  priv->pathname_basename = NULL;

  g_free (priv->suggested_destname);
  priv->suggested_destname = NULL;

  G_OBJECT_CLASS (autoar_extract_parent_class)->finalize (object);
}

static int
libarchive_read_open_cb (struct archive *ar_read,
                         void *client_data)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  g_debug ("libarchive_read_open_cb: called");

  arextract = (AutoarExtract*)client_data;
  priv = arextract->priv;

  if (priv->error != NULL)
    return ARCHIVE_FATAL;

  if (arextract->priv->source_is_mem) {
    priv->istream =
      g_memory_input_stream_new_from_data (priv->source_buffer,
                                           priv->source_buffer_size,
                                           NULL);
  } else {
    GFileInputStream *istream;
    istream = g_file_read (priv->source_file,
                           priv->cancellable,
                           &(arextract->priv->error));
    priv->istream = G_INPUT_STREAM (istream);
  }

  if (priv->error != NULL)
    return ARCHIVE_FATAL;

  g_debug ("libarchive_read_open_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static int
libarchive_read_close_cb (struct archive *ar_read,
                          void *client_data)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;

  g_debug ("libarchive_read_close_cb: called");

  arextract = (AutoarExtract*)client_data;
  priv = arextract->priv;

  if (priv->error != NULL)
    return ARCHIVE_FATAL;

  if (priv->istream != NULL) {
    g_input_stream_close (priv->istream, priv->cancellable, NULL);
    g_object_unref (priv->istream);
    priv->istream = NULL;
  }

  g_debug ("libarchive_read_close_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static ssize_t
libarchive_read_read_cb (struct archive *ar_read,
                         void *client_data,
                         const void **buffer)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;
  gssize read_size;

  g_debug ("libarchive_read_read_cb: called");

  arextract = (AutoarExtract*)client_data;
  priv = arextract->priv;

  if (priv->error != NULL || priv->istream == NULL)
    return -1;

  *buffer = priv->buffer;
  read_size = g_input_stream_read (priv->istream,
                                   priv->buffer,
                                   priv->buffer_size,
                                   priv->cancellable,
                                   &(priv->error));
  if (priv->error != NULL)
    return -1;

  g_debug ("libarchive_read_read_cb: %lu", read_size);
  return read_size;
}

static off_t
libarchive_read_seek_cb (struct archive *ar_read,
                         void *client_data,
                         off_t request,
                         int whence)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;
  GSeekable *seekable;
  GSeekType  seektype;
  off_t new_offset;

  g_debug ("libarchive_read_seek_cb: called");

  arextract = (AutoarExtract*)client_data;
  priv = arextract->priv;
  seekable = (GSeekable*)(priv->istream);
  if (priv->error != NULL || priv->istream == NULL)
    return -1;

  switch (whence) {
    case SEEK_SET:
      seektype = G_SEEK_SET;
      break;
    case SEEK_CUR:
      seektype = G_SEEK_CUR;
      break;
    case SEEK_END:
      seektype = G_SEEK_END;
      break;
    default:
      return -1;
  }

  g_seekable_seek (seekable,
                   request,
                   seektype,
                   priv->cancellable,
                   &(priv->error));
  new_offset = g_seekable_tell (seekable);
  if (priv->error != NULL)
    return -1;

  g_debug ("libarchive_read_seek_cb: %"G_GOFFSET_FORMAT, (goffset)new_offset);
  return new_offset;
}

static off_t
libarchive_read_skip_cb (struct archive *ar_read,
                         void *client_data,
                         off_t request)
{
  AutoarExtract *arextract;
  AutoarExtractPrivate *priv;
  GSeekable *seekable;
  off_t old_offset, new_offset;

  g_debug ("libarchive_read_skip_cb: called");

  arextract = (AutoarExtract*)client_data;
  priv = arextract->priv;
  seekable = (GSeekable*)(priv->istream);
  if (priv->error != NULL || priv->istream == NULL) {
    return -1;
  }

  old_offset = g_seekable_tell (seekable);
  new_offset = libarchive_read_seek_cb (ar_read, client_data, request, SEEK_CUR);
  if (new_offset > old_offset)
    return (new_offset - old_offset);

  return 0;
}

static int
libarchive_create_read_object (gboolean use_raw_format,
                               AutoarExtract *arextract,
                               struct archive **a)
{
  *a = archive_read_new ();
  archive_read_support_filter_all (*a);
  if (use_raw_format)
    archive_read_support_format_raw (*a);
  else
    archive_read_support_format_all (*a);
  archive_read_set_open_callback (*a, libarchive_read_open_cb);
  archive_read_set_read_callback (*a, libarchive_read_read_cb);
  archive_read_set_close_callback (*a, libarchive_read_close_cb);
  archive_read_set_seek_callback (*a, libarchive_read_seek_cb);
  archive_read_set_skip_callback (*a, libarchive_read_skip_cb);
  archive_read_set_callback_data (*a, arextract);

  return archive_read_open1 (*a);
}

static void
g_pattern_spec_free_safe (void *pattern_compiled)
{
  if (pattern_compiled != NULL)
    g_pattern_spec_free (pattern_compiled);
}

static void
g_file_and_info_free (void *g_file_and_info) {
  GFileAndInfo *fi = g_file_and_info;
  g_object_unref (fi->file);
  g_object_unref (fi->info);
}

static char*
g_file_get_name (GFile *file) {
  char *name;
  name = g_file_get_path (file);
  if (name == NULL)
    name = g_file_get_uri (file);
  return name;
}

static inline void
autoar_extract_signal_scanned (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[SCANNED], 0,
                               arextract->priv->files);
}

static inline void
autoar_extract_signal_decide_dest (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[DECIDE_DEST], 0,
                               arextract->priv->top_level_dir);
}

static inline void
autoar_extract_signal_progress (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[PROGRESS], 0,
                               ((double)(arextract->priv->completed_size)) /
                               ((double)(arextract->priv->size)),
                               ((double)(arextract->priv->completed_files)) /
                               ((double)(arextract->priv->files)));
}

static inline void
autoar_extract_signal_cancelled (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[CANCELLED], 0);

}

static inline void
autoar_extract_signal_completed (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[COMPLETED], 0);

}

static inline void
autoar_extract_signal_error (AutoarExtract *arextract)
{
  if (arextract->priv->error != NULL) {
    if (arextract->priv->error->domain == G_IO_ERROR &&
        arextract->priv->error->code == G_IO_ERROR_CANCELLED) {
      g_error_free (arextract->priv->error);
      arextract->priv->error = NULL;
      autoar_extract_signal_cancelled (arextract);
    } else {
      autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                                   autoar_extract_signals[ERROR], 0,
                                   arextract->priv->error);
    }
  }
}

static GFile*
autoar_extract_do_sanitize_pathname (const char *pathname,
                                     const char *skip_chars,
                                     GFile *top_level_dir) {
  const char *pathname_skip_prefix;
  char **pathname_chunks;
  GFile *extracted_filename;
  int i;

  if (skip_chars != NULL)
    pathname_skip_prefix = pathname + strspn (pathname, "./");
  else
    pathname_skip_prefix = pathname;

  for (; *pathname_skip_prefix == '/'; pathname_skip_prefix++);
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

  return extracted_filename;
}

static gboolean
autoar_extract_do_pattern_check (const char *path,
                                 GPtrArray *pattern)
{
  char **path_components;
  GArray *path_components_len;

  int i, j, len;

  path_components = g_strsplit (path, "/", G_MAXINT);
  path_components_len = g_array_new (FALSE, FALSE, sizeof(size_t));
  for (i = 0; path_components[i] != NULL; i++) {
    len = strlen (path_components[i]);
    g_array_append_val (path_components_len, len);
  }

  for (i = 0; g_ptr_array_index (pattern, i) != NULL; i++) {
    for (j = 0; path_components[j] != NULL; j++) {
      if (g_pattern_match (g_ptr_array_index (pattern, i),
                           g_array_index (path_components_len, size_t, j),
                           path_components[j],
                           NULL)) {
        g_debug ("autoar_extract_do_pattern_check: ### %s", path_components[j]);
        g_strfreev (path_components);
        g_array_unref (path_components_len);
        return FALSE;
      }
    }
  }

  g_strfreev (path_components);
  g_array_unref (path_components_len);

  return TRUE;
}

static void
autoar_extract_do_write_entry (AutoarExtract *arextract,
                               struct archive *a,
                               struct archive_entry *entry,
                               GFile *dest,
                               GFile *hardlink)
{
  AutoarExtractPrivate *priv;
  GFileInfo *info;
  mode_t filetype;
  int r;

  priv = arextract->priv;

  {
    GFile *parent;
    parent = g_file_get_parent (dest);
    if (!g_file_query_exists (parent, priv->cancellable))
      g_file_make_directory_with_parents (parent, priv->cancellable, NULL);
    g_object_unref (parent);
  }

  info = g_file_info_new ();

  /* time */
  g_debug ("autoar_extract_do_write_entry: time");
  if (archive_entry_atime_is_set (entry)) {
    g_file_info_set_attribute_uint64 (info,
                                      G_FILE_ATTRIBUTE_TIME_ACCESS,
                                      archive_entry_atime (entry));
    g_file_info_set_attribute_uint32 (info,
                                      G_FILE_ATTRIBUTE_TIME_ACCESS_USEC,
                                      archive_entry_atime_nsec (entry) / 1000);
  }
  if (archive_entry_birthtime_is_set (entry)) {
    g_file_info_set_attribute_uint64 (info,
                                      G_FILE_ATTRIBUTE_TIME_CREATED,
                                      archive_entry_birthtime (entry));
    g_file_info_set_attribute_uint32 (info,
                                      G_FILE_ATTRIBUTE_TIME_CREATED_USEC,
                                      archive_entry_birthtime_nsec (entry) / 1000);
  }
  if (archive_entry_ctime_is_set (entry)) {
    g_file_info_set_attribute_uint64 (info,
                                      G_FILE_ATTRIBUTE_TIME_CHANGED,
                                      archive_entry_ctime (entry));
    g_file_info_set_attribute_uint32 (info,
                                      G_FILE_ATTRIBUTE_TIME_CHANGED_USEC,
                                      archive_entry_ctime_nsec (entry) / 1000);
  }
  if (archive_entry_mtime_is_set (entry)) {
    g_file_info_set_attribute_uint64 (info,
                                      G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                      archive_entry_mtime (entry));
    g_file_info_set_attribute_uint32 (info,
                                      G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
                                      archive_entry_mtime_nsec (entry) / 1000);
  }

  /* user */
  {
    guint32 uid;
    const char *uname;

    g_debug ("autoar_extract_do_write_entry: user");
#ifdef HAVE_GETPWNAM
    if ((uname = archive_entry_uname (entry)) != NULL) {
      void *got_uid;
      if (g_hash_table_lookup_extended (priv->userhash, uname, NULL, &got_uid) == TRUE) {
        uid = GPOINTER_TO_UINT (got_uid);
      } else {
        struct passwd *pwd = getpwnam (uname);
        if (pwd == NULL) {
          uid = archive_entry_uid (entry);
        } else {
          uid = pwd->pw_uid;
          g_hash_table_insert (priv->userhash, g_strdup (uname), GUINT_TO_POINTER (uid));
        }
      }
      g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID, uid);
    } else
#endif

    if ((uid = archive_entry_uid (entry)) != 0) {
      g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID, uid);
    }
  }

  /* group */
  {
    guint32 gid;
    const char *gname;

    g_debug ("autoar_extract_do_write_entry: group");
#ifdef HAVE_GETGRNAM
    if ((gname = archive_entry_gname (entry)) != NULL) {
      void *got_gid;
      if (g_hash_table_lookup_extended (priv->grouphash, gname, NULL, &got_gid) == TRUE) {
        gid = GPOINTER_TO_UINT (got_gid);
      } else {
        struct group *grp = getgrnam (gname);
        if (grp == NULL) {
          gid = archive_entry_gid (entry);
        } else {
          gid = grp->gr_gid;
          g_hash_table_insert (priv->grouphash, g_strdup (gname), GUINT_TO_POINTER (gid));
        }
      }
      g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID, gid);
    } else
#endif

    if ((gid = archive_entry_gid (entry)) != 0) {
      g_file_info_set_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID, gid);
    }
  }

  /* permissions */
  g_debug ("autoar_extract_do_write_entry: permissions");
  g_file_info_set_attribute_uint32 (info,
                                    G_FILE_ATTRIBUTE_UNIX_MODE,
                                    archive_entry_perm (entry));

#ifdef HAVE_LINK
  if (hardlink != NULL) {
    char *hardlink_path, *dest_path;
    r = link (hardlink_path = g_file_get_path (hardlink),
              dest_path = g_file_get_path (dest));
    g_debug ("autoar_extract_do_write_entry: hard link, %s => %s, %d",
             dest_path, hardlink_path, r);
    g_free (hardlink_path);
    g_free (dest_path);
    if (r >= 0) {
      g_debug ("autoar_extract_do_write_entry: skip file creation");
      goto applyinfo;
    }
  }
#endif

  g_debug ("autoar_extract_do_write_entry: writing");
  r = 0;
  switch (filetype = archive_entry_filetype (entry)) {
    default:
    case AE_IFREG:
      {
        GOutputStream *ostream;
        const void *buffer;
        size_t size, written;
        off_t offset;

        g_debug ("autoar_extract_do_write_entry: case REG");
        ostream = (GOutputStream*)g_file_replace (dest,
                                                  NULL,
                                                  FALSE,
                                                  G_FILE_CREATE_NONE,
                                                  priv->cancellable,
                                                  &(priv->error));
        if (arextract->priv->error != NULL) {
          g_object_unref (info);
          return;
        }
        if (ostream != NULL) {
          /* Archive entry size may be zero if we use raw format. */
          if (archive_entry_size(entry) > 0 || priv->use_raw_format) {
            while (archive_read_data_block (a, &buffer, &size, &offset) == ARCHIVE_OK) {
              /* buffer == NULL occurs in some zip archives when an entry is
               * completely read. We just skip this situation to prevent GIO
               * warnings. */
              if (buffer == NULL)
                continue;
              g_output_stream_write_all (ostream,
                                         buffer,
                                         size,
                                         &written,
                                         priv->cancellable,
                                         &(priv->error));
              if (priv->error != NULL) {
                g_output_stream_close (ostream, priv->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              if (g_cancellable_is_cancelled (priv->cancellable)) {
                g_output_stream_close (ostream, priv->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              priv->completed_size += written;
              autoar_extract_signal_progress (arextract);
            }
          }
          g_output_stream_close (ostream, priv->cancellable, NULL);
          g_object_unref (ostream);
        }
      }
      break;
    case AE_IFDIR:
      {
        GFileAndInfo fileandinfo;

        g_debug ("autoar_extract_do_write_entry: case DIR");
        g_file_make_directory_with_parents (dest, priv->cancellable, &(priv->error));
        if (priv->error != NULL) {
          /* "File exists" is not a fatal error */
          if (priv->error->code == G_IO_ERROR_EXISTS) {
            g_error_free (priv->error);
            priv->error = NULL;
          } else {
            g_object_unref (info);
            return;
          }
        }
        fileandinfo.file = g_object_ref (dest);
        fileandinfo.info = g_object_ref (info);
        g_array_append_val (priv->extracted_dir_list, fileandinfo);
      }
      break;
    case AE_IFLNK:
      g_debug ("autoar_extract_do_write_entry: case LNK");
      g_file_make_symbolic_link (dest,
                                 archive_entry_symlink (entry),
                                 priv->cancellable,
                                 &(priv->error));
      break;
    /* FIFOs, sockets, block files, character files are not important
     * in the regular archives, so errors are not fatal. */
#if defined HAVE_MKFIFO || defined HAVE_MKNOD
    case AE_IFIFO:
      {
        char *path;
        g_debug ("autoar_extract_do_write_entry: case FIFO");
# ifdef HAVE_MKFIFO
        r = mkfifo (path = g_file_get_path (dest), archive_entry_perm (entry));
# else
        r = mknod (path = g_file_get_path (dest),
                   S_IFIFO | archive_entry_perm (entry),
                   0);
# endif
        g_free (path);
      }
      break;
#endif
#ifdef HAVE_MKNOD
    case AE_IFSOCK:
      {
        char *path;
        g_debug ("autoar_extract_do_write_entry: case SOCK");
        r = mknod (path = g_file_get_path (dest),
                   S_IFSOCK | archive_entry_perm (entry),
                   0);
        g_free (path);
      }
      break;
    case AE_IFBLK:
      {
        char *path;
        g_debug ("autoar_extract_do_write_entry: case BLK");
        r = mknod (path = g_file_get_path (dest),
                   S_IFBLK | archive_entry_perm (entry),
                   archive_entry_rdev (entry));
        g_free (path);
      }
      break;
    case AE_IFCHR:
      {
        char *path;
        g_debug ("autoar_extract_do_write_entry: case CHR");
        r = mknod (path = g_file_get_path (dest),
                   S_IFCHR | archive_entry_perm (entry),
                   archive_entry_rdev (entry));
        g_free (path);
      }
      break;
#endif
  }

#if defined HAVE_MKFIFO || defined HAVE_MKNOD
  /* Create a empty regular file if we cannot create the special file. */
  if (r < 0 && (filetype == AE_IFIFO ||
                filetype == AE_IFSOCK ||
                filetype == AE_IFBLK ||
                filetype == AE_IFCHR)) {
    GOutputStream *ostream;
    ostream = (GOutputStream*)g_file_append_to (dest, G_FILE_CREATE_NONE, priv->cancellable, NULL);
    if (ostream != NULL) {
      g_output_stream_close (ostream, priv->cancellable, NULL);
      g_object_unref (ostream);
    }
  }
#endif

applyinfo:
  g_debug ("autoar_extract_do_write_entry: applying info");
  g_file_set_attributes_from_info (dest,
                                   info,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   priv->cancellable,
                                   &(priv->error));

  if (priv->error != NULL) {
    g_debug ("autoar_extract_do_write_entry: %s\n", priv->error->message);
    g_error_free (priv->error);
    priv->error = NULL;
  }

  g_object_unref (info);
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
  object_class->dispose = autoar_extract_dispose;
  object_class->finalize = autoar_extract_finalize;

  g_object_class_install_property (object_class, PROP_SOURCE,
                                   g_param_spec_string ("source",
                                                        "Source archive",
                                                        "The archive file to be extracted",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SOURCE_FILE,
                                   g_param_spec_object ("source-file",
                                                        "Source archive GFile",
                                                        "The archive GFile to be extracted",
                                                        G_TYPE_FILE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output directory",
                                                        "Output directory of extracted archive",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OUTPUT_FILE,
                                   g_param_spec_object ("output-file",
                                                        "Output directory GFile",
                                                        "Output directory GFile of extracted archives",
                                                        G_TYPE_FILE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SIZE,
                                   g_param_spec_uint64 ("size",
                                                        "File size",
                                                        "Size of the extracted files",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_SIZE,
                                   g_param_spec_uint64 ("completed-size",
                                                        "Written file size",
                                                        "Bytes written to disk",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILES,
                                   g_param_spec_uint ("files",
                                                      "Files",
                                                      "Number of files in the archive",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_FILES,
                                   g_param_spec_uint ("completed-files",
                                                      "Written files",
                                                      "Number of files has been written",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SOURCE_IS_MEM,
                                   g_param_spec_boolean ("source-is-mem",
                                                         "Source is memory",
                                                         "Whether source file is in memory",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OUTPUT_IS_DEST,
                                   g_param_spec_boolean ("output-is-dest",
                                                         "Output is destination",
                                                         "Whether output direcotry is used as destination",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  autoar_extract_signals[SCANNED] =
    g_signal_new ("scanned",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarExtractClass, scanned),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);

  autoar_extract_signals[DECIDE_DEST] =
    g_signal_new ("decide-dest",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarExtractClass, decide_dest),
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_FILE);

  autoar_extract_signals[PROGRESS] =
    g_signal_new ("progress",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarExtractClass, progress),
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_DOUBLE,
                  G_TYPE_DOUBLE);

  autoar_extract_signals[CANCELLED] =
    g_signal_new ("cancelled",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarExtractClass, cancelled),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  autoar_extract_signals[COMPLETED] =
    g_signal_new ("completed",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarExtractClass, completed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  autoar_extract_signals[ERROR] =
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

  priv->source_file = NULL;
  priv->output_file = NULL;

  priv->arpref = NULL;

  priv->source_is_mem = FALSE;
  priv->output_is_dest = FALSE;

  priv->source_buffer = NULL;
  priv->source_buffer_size = 0;

  priv->cancellable = NULL;

  priv->size = 0;
  priv->completed_size = 0;

  priv->files = 0;
  priv->completed_files = 0;

  priv->istream = NULL;
  priv->buffer_size = BUFFER_SIZE;
  priv->buffer = g_new (char, priv->buffer_size);
  priv->error = NULL;

  priv->userhash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->grouphash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->bad_filename = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->pattern_compiled = g_ptr_array_new_with_free_func (g_pattern_spec_free_safe);
  priv->extracted_dir_list = g_array_new (FALSE, FALSE, sizeof (GFileAndInfo));
  g_array_set_clear_func (priv->extracted_dir_list, g_file_and_info_free);
  priv->top_level_dir = NULL;

  priv->pathname_prefix_len = 0;
  priv->pathname_basename = NULL;
  priv->suggested_destname = NULL;

  priv->in_thread = FALSE;
  priv->use_raw_format = FALSE;
  priv->has_top_level_dir = TRUE;
  priv->has_only_one_file = TRUE;
}

static AutoarExtract*
autoar_extract_new_full (const char *source,
                         GFile *source_file,
                         const char *output,
                         GFile *output_file,
                         gboolean source_is_mem,
                         gboolean output_is_dest,
                         AutoarPref *arpref,
                         const void *buffer,
                         gsize buffer_size,
                         const char *suggested_destname)
{
  AutoarExtract *arextract;
  char *gen_source, *gen_output;
  GFile *gen_source_file, *gen_output_file;

  gen_source      = NULL;
  gen_source_file = NULL;
  gen_output      = NULL;
  gen_output_file = NULL;

  if (source_is_mem) {
    gen_source = g_strdup_printf ("(memory %p, size %" G_GSIZE_FORMAT ")", buffer, buffer_size);
    gen_source_file = g_file_new_for_commandline_arg (gen_source);
  } else {
    if (source == NULL)
      gen_source = g_file_get_name (source_file);
    if (source_file == NULL)
      gen_source_file = g_file_new_for_commandline_arg (source);
  }

  if (output == NULL)
    gen_output = g_file_get_name (output_file);
  if (output_file == NULL)
    gen_output_file = g_file_new_for_commandline_arg (output);

  arextract =
    g_object_new (AUTOAR_TYPE_EXTRACT,
                  "source",         source      != NULL ? source      : gen_source,
                  "source-file",    source_file != NULL ? source_file : gen_source_file,
                  "output",         output      != NULL ? output      : gen_output,
                  "output-file",    output_file != NULL ? output_file : gen_output_file,
                  "source-is-mem",  source_is_mem,
                  "output-is-dest", output_is_dest, NULL);
  arextract->priv->arpref = g_object_ref (arpref);

  if (source_is_mem) {
    arextract->priv->source_buffer = buffer;
    arextract->priv->source_buffer_size = buffer_size;
    if (suggested_destname != NULL)
      arextract->priv->suggested_destname =
        autoar_common_get_basename_remove_extension (suggested_destname);
    else
      arextract->priv->suggested_destname =
        autoar_common_get_basename_remove_extension (gen_source);
  } else {
    char *source_basename = g_file_get_basename (arextract->priv->source_file);
    arextract->priv->suggested_destname =
      autoar_common_get_basename_remove_extension (source_basename);
  }

  g_free (gen_source);
  g_free (gen_output);

  if (gen_source_file != NULL)
    g_object_unref (gen_source_file);
  if (gen_output_file != NULL)
    g_object_unref (gen_output_file);

  return arextract;
}


AutoarExtract*
autoar_extract_new (const char *source,
                    const char *output,
                    AutoarPref *arpref)
{
  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (output != NULL, NULL);

  return autoar_extract_new_full (source, NULL, output, NULL,
                                  FALSE, FALSE, arpref,
                                  NULL, 0, NULL);
}

AutoarExtract*
autoar_extract_new_file (GFile *source_file,
                         GFile *output_file,
                         AutoarPref *arpref)
{
  g_return_val_if_fail (source_file != NULL, NULL);
  g_return_val_if_fail (output_file != NULL, NULL);

  return autoar_extract_new_full (NULL, source_file, NULL, output_file,
                                  FALSE, FALSE, arpref,
                                  NULL, 0, NULL);
}

AutoarExtract*
autoar_extract_new_memory (const void *buffer,
                           gsize buffer_size,
                           const char *source_name,
                           const char *output,
                           AutoarPref *arpref)
{

  g_return_val_if_fail (output != NULL, NULL);
  g_return_val_if_fail (buffer != NULL, NULL);

  return autoar_extract_new_full (NULL, NULL, output, NULL,
                                  TRUE, FALSE, arpref,
                                  buffer, buffer_size, source_name);
}

AutoarExtract*
autoar_extract_new_memory_file (const void *buffer,
                                gsize buffer_size,
                                const char *source_name,
                                GFile *output_file,
                                AutoarPref *arpref)
{
  g_return_val_if_fail (output_file != NULL, NULL);
  g_return_val_if_fail (buffer != NULL, NULL);

  return autoar_extract_new_full (NULL, NULL, NULL, output_file,
                                  TRUE, FALSE, arpref,
                                  buffer, buffer_size, source_name);
}

static void
autoar_extract_step_initialize_pattern (AutoarExtract *arextract) {
  /* Step 0: Compile the file name pattern. */

  AutoarExtractPrivate *priv = arextract->priv;
  const char **pattern = autoar_pref_get_pattern_to_ignore (priv->arpref);
  int i;

  g_debug ("autoar_extract_step_initialize_pattern: called");

  if (pattern != NULL) {
    for (i = 0; pattern[i] != NULL; i++)
      g_ptr_array_add (priv->pattern_compiled, g_pattern_spec_new (pattern[i]));
  }
  g_ptr_array_add (priv->pattern_compiled, NULL);
}

static void
autoar_extract_step_scan_toplevel (AutoarExtract *arextract) {
  /* Step 1: Scan all file names in the archive
   * We have to check whether the archive contains a top-level directory
   * before performing the extraction. We emit the "scanned" signal when
   * the checking is completed. */

  struct archive *a;
  struct archive_entry *entry;

  char *pathname_prefix;

  AutoarExtractPrivate *priv;
  int r;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_scan_toplevel: called");

  r = libarchive_create_read_object (FALSE, arextract, &a);
  if (r != ARCHIVE_OK) {
    archive_read_free (a);
    r = libarchive_create_read_object (TRUE, arextract, &a);
    if (priv->error == NULL) {
      if (r != ARCHIVE_OK) {
        priv->error = autoar_common_g_error_new_a (a, priv->source);
      } else if (archive_filter_count (a) <= 1){
        /* If we only use raw format and filter count is one, libarchive will
         * not do anything except for just copying the source file. We do not
         * want this thing to happen because it does unnecesssary copying. */
        priv->error = g_error_new (AUTOAR_EXTRACT_ERROR, NOT_AN_ARCHIVE_ERRNO,
                                   "\'%s\': %s", priv->source, "not an archive");
      }
    }
    archive_read_free (a);
    return;
    priv->use_raw_format = TRUE;
  }

  pathname_prefix = NULL;

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;

    if (g_cancellable_is_cancelled (priv->cancellable)) {
      g_free (pathname_prefix);
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    g_debug ("autoar_extract_step_scan_toplevel: %d: pathname = %s", priv->files, pathname);

    if (!priv->use_raw_format && !autoar_extract_do_pattern_check (pathname, priv->pattern_compiled)) {
      g_hash_table_insert (priv->bad_filename, g_strdup (pathname), GUINT_TO_POINTER (TRUE));
      continue;
    }

    g_debug ("autoar_extract_step_scan_toplevel: %d: pattern check passed", priv->files);

    if (pathname_prefix == NULL) {
      char *dir_sep_location;
      size_t skip_len, prefix_len;

      skip_len = strspn (pathname, "./");
      dir_sep_location = strchr (pathname + skip_len, '/');
      if (dir_sep_location == NULL) {
        prefix_len = strlen (pathname);
      } else {
        prefix_len = dir_sep_location - pathname;
      }
      pathname_prefix = g_strndup (pathname, prefix_len);
      g_debug ("autoar_extract_step_scan_toplevel: pathname_prefix = %s", pathname_prefix);

      priv->pathname_prefix_len = prefix_len;
      priv->pathname_basename = g_path_get_basename (pathname);
    } else {
      priv->has_only_one_file = FALSE;
      if (!g_str_has_prefix (pathname, pathname_prefix)) {
        priv->has_top_level_dir = FALSE;
      }
    }
    priv->files++;
    priv->size += archive_entry_size (entry);
    archive_read_data_skip (a);
  }

  if (r != ARCHIVE_EOF) {
    if (priv->error == NULL) {
      priv->error = autoar_common_g_error_new_a (a, priv->source);
    }
    g_free (pathname_prefix);
    archive_read_free (a);
    return;
  }

  g_free (pathname_prefix);
  archive_read_free (a);

  g_debug ("autoar_extract_step_scan_toplevel: has_top_level_dir = %s",
           priv->has_top_level_dir ? "TRUE" : "FALSE");
  g_debug ("autoar_extract_step_scan_toplevel: has_only_one_file = %s",
           priv->has_only_one_file ? "TRUE" : "FALSE");
  autoar_extract_signal_scanned (arextract);
}

static void
autoar_extract_step_decide_dest (AutoarExtract *arextract) {
  /* Step 2: Create necessary directories
   * If the archive contains only one file, we don't create the directory */

  const char *pathname_extension;

  AutoarExtractPrivate *priv;
  int i;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_decide_dest: called");

  {
    pathname_extension = autoar_common_get_filename_extension (priv->pathname_basename);
    if (priv->has_only_one_file && (pathname_extension != priv->pathname_basename)) {
      /* If we only have one file, we have to add the file extension.
       * Although we use the variable `top_level_dir', it may be a regular
       * file, so the extension is important. */
      char *new_filename = g_strconcat (priv->suggested_destname, pathname_extension, NULL);
      priv->top_level_dir = g_file_get_child (priv->output_file, new_filename);
      g_free (new_filename);
    } else {
      priv->top_level_dir = g_file_get_child (priv->output_file, priv->suggested_destname);
      pathname_extension = "";
    }
  }

  {
    char *top_level_dir_basename_modified = NULL;
    for (i = 1; g_file_query_exists (priv->top_level_dir, priv->cancellable); i++) {
      g_free (top_level_dir_basename_modified);
      g_object_unref (priv->top_level_dir);

      if (g_cancellable_is_cancelled (priv->cancellable))
        return;

      if (priv->has_only_one_file) {
        top_level_dir_basename_modified = g_strdup_printf ("%s(%d)%s",
                                                           priv->suggested_destname,
                                                           i,
                                                           pathname_extension);
      } else {
        top_level_dir_basename_modified = g_strdup_printf ("%s(%d)",
                                                           priv->suggested_destname,
                                                           i);
      }
      priv->top_level_dir = g_file_get_child (priv->output_file,
                                              top_level_dir_basename_modified);
    }
    g_free (top_level_dir_basename_modified);
  }

  if (!(priv->has_only_one_file))
    g_file_make_directory_with_parents (priv->top_level_dir, priv->cancellable, &(priv->error));

  if (priv->error != NULL)
    return;

  autoar_extract_signal_decide_dest (arextract);
}

static void
autoar_extract_step_decide_dest_already (AutoarExtract *arextract) {
  /* Alternative step 2: Output is destination */
  arextract->priv->top_level_dir = g_object_ref (arextract->priv->output_file);
  autoar_extract_signal_decide_dest (arextract);
}

static void
autoar_extract_step_extract (AutoarExtract *arextract) {
  /* Step 3: Extract files
   * We have to re-open the archive to extract files */

  struct archive *a;
  struct archive_entry *entry;

  AutoarExtractPrivate *priv;
  int r;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_extract: called");

  r = libarchive_create_read_object (priv->use_raw_format, arextract, &a);
  if (r != ARCHIVE_OK) {
    if (priv->error == NULL) {
      priv->error = autoar_common_g_error_new_a (a, priv->source);
    }
    archive_read_free (a);
    return;
  }

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;
    const char *hardlink;
    GFile *extracted_filename;
    GFile *hardlink_filename;

    if (g_cancellable_is_cancelled (priv->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    hardlink = archive_entry_hardlink (entry);
    hardlink_filename = NULL;
    if (GPOINTER_TO_UINT (g_hash_table_lookup (priv->bad_filename, pathname)))
      continue;

    if (!(priv->has_only_one_file)) {
      if (priv->has_top_level_dir) {
        extracted_filename =
          autoar_extract_do_sanitize_pathname (pathname + priv->pathname_prefix_len,
                                               NULL, priv->top_level_dir);
        if (hardlink != NULL)
          hardlink_filename =
            autoar_extract_do_sanitize_pathname (hardlink + priv->pathname_prefix_len,
                                                 NULL, priv->top_level_dir);
      } else {
        extracted_filename =
          autoar_extract_do_sanitize_pathname (pathname, "./", priv->top_level_dir);
        if (hardlink != NULL)
          hardlink_filename =
            autoar_extract_do_sanitize_pathname (pathname, "./", priv->top_level_dir);
      }
    } else {
      extracted_filename = g_object_ref (priv->top_level_dir);
    }

    autoar_extract_do_write_entry (arextract, a, entry,
                                   extracted_filename, hardlink_filename);

    g_object_unref (extracted_filename);
    if (hardlink_filename != NULL)
      g_object_unref (hardlink_filename);

    if (priv->error != NULL) {
      archive_read_free (a);
      return;
    }

    priv->completed_files++;
    autoar_extract_signal_progress (arextract);
  }

  if (r != ARCHIVE_EOF) {
    if (priv->error == NULL) {
      priv->error = autoar_common_g_error_new_a (a, priv->source);
    }
    archive_read_free (a);
    return;
  }

  archive_read_free (a);
}

static void
autoar_extract_step_apply_dir_fileinfo (AutoarExtract *arextract) {
  /* Step 4: Re-apply file info to all directories
   * It is required because modification times may be updated during the
   * writing of files in the directory. */

  AutoarExtractPrivate *priv;
  int i;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_apply_dir_fileinfo: called");

  for (i = 0; i < priv->extracted_dir_list->len; i++) {
    GFile *file = g_array_index (priv->extracted_dir_list, GFileAndInfo, i).file;
    GFileInfo *info = g_array_index (priv->extracted_dir_list, GFileAndInfo, i).info;
    g_file_set_attributes_from_info (file, info,
                                     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                     priv->cancellable, NULL);
    if (g_cancellable_is_cancelled (priv->cancellable)) {
      return;
    }
  }
}

static void
autoar_extract_step_cleanup (AutoarExtract *arextract) {
  /* Step 5: Force progress to be 100% and remove the source archive file
   * If the extraction is completed successfully, remove the source file.
   * Errors are not fatal because we have completed our work. */

  AutoarExtractPrivate *priv;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_cleanup: called");

  priv->completed_size = priv->size;
  priv->completed_files = priv->files;
  autoar_extract_signal_progress (arextract);
  g_debug ("autoar_extract_step_cleanup: Update progress");
  if (autoar_pref_get_delete_if_succeed (priv->arpref) && priv->source_file != NULL) {
    g_debug ("autoar_extract_step_cleanup: Delete");
    g_file_delete (priv->source_file, priv->cancellable, NULL);
  }
}

static void
autoar_extract_run (AutoarExtract *arextract)
{
  /* Numbers of steps.
   * The array size must be modified if more steps are added. */
  void (*steps[7])(AutoarExtract*);

  AutoarExtractPrivate *priv;
  int i;

  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  priv = arextract->priv;

  g_return_if_fail (priv->source_file != NULL || (priv->source_is_mem &&
                                                  priv->source_buffer != NULL));
  g_return_if_fail (priv->output_file != NULL);

  if (g_cancellable_is_cancelled (priv->cancellable)) {
    autoar_extract_signal_cancelled (arextract);
    return;
  }

  i = 0;
  steps[i++] = autoar_extract_step_initialize_pattern;
  steps[i++] = autoar_extract_step_scan_toplevel;
  steps[i++] = priv->output_is_dest ?
               autoar_extract_step_decide_dest_already :
               autoar_extract_step_decide_dest;
  steps[i++] = autoar_extract_step_extract;
  steps[i++] = autoar_extract_step_apply_dir_fileinfo;
  steps[i++] = autoar_extract_step_cleanup;
  steps[i++] = NULL;

  for (i = 0; steps[i] != NULL; i++) {
    g_debug ("autoar_extract_run: Step %d Begin", i);
    (*steps[i])(arextract);
    g_debug ("autoar_extract_run: Step %d End", i);
    if (priv->error != NULL) {
      autoar_extract_signal_error (arextract);
      return;
    }
    if (g_cancellable_is_cancelled (priv->cancellable)) {
      autoar_extract_signal_cancelled (arextract);
      return;
    }
  }

  autoar_extract_signal_completed (arextract);
}

void
autoar_extract_start (AutoarExtract *arextract,
                      GCancellable *cancellable)
{
  if (cancellable != NULL)
    g_object_ref (cancellable);
  arextract->priv->cancellable = cancellable;
  arextract->priv->in_thread = FALSE;
  autoar_extract_run (arextract);
}

static void
autoar_extract_start_async_thread (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
  AutoarExtract *arextract = source_object;
  autoar_extract_run (arextract);
  g_task_return_pointer (task, NULL, g_free);
  g_object_unref (arextract);
  g_object_unref (task);
}


void
autoar_extract_start_async (AutoarExtract *arextract,
                            GCancellable *cancellable)
{
  GTask *task;

  g_object_ref (arextract);
  if (cancellable != NULL)
    g_object_ref (cancellable);
  arextract->priv->cancellable = cancellable;
  arextract->priv->in_thread = TRUE;

  task = g_task_new (arextract, NULL, NULL, NULL);
  g_task_set_task_data (task, NULL, NULL);
  g_task_run_in_thread (task, autoar_extract_start_async_thread);
}

void
autoar_extract_free_source_buffer (AutoarExtract *arextract,
                                   GDestroyNotify free_func)
{
  if (arextract->priv->source_buffer != NULL)
    (*free_func)((void*)(arextract->priv->source_buffer));

  arextract->priv->source_buffer = NULL;
  arextract->priv->source_buffer_size = 0;
}

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

#include "autoar-misc.h"
#include "autoar-private.h"
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

/**
 * SECTION:autoar-extract
 * @Short_description: Automatically extract an archive
 * @Title: AutoarExtract
 * @Include: gnome-autoar/autoar.h
 *
 * The #AutoarExtract object is used to automatically extract files and
 * directories from an archive. By default, it will only create one file or
 * directory in the output directory. This is done to avoid clutter on the
 * user's output directory. If the archive contains only one file, the file
 * will be extracted to the output directory. If the archive has more than one
 * file, the files will be extracted in a directory having the same name as the
 * archive, except the extension. It is also possible to just extract all files
 * to the output directory (note that this will not perform any checks) by
 * using autoar_extract_set_output_is_dest().

 * #AutoarExtract will not attempt to solve any name conflicts. If the
 * destination directory already exists, it will proceed normally. If the
 * destionation directory cannot be created, it will fail with an error.
 * It is possible however to change the destination, when
 * #AutoarExtract::decide-destination is emitted. The signal provides the decided
 * destination and the list of files to be extracted. The signal also allows a
 * new output destination to be used instead of the one provided by
 * #AutoarExtract. This is convenient for solving name conflicts and
 * implementing specific logic based on the contents of the archive.
 *
 * When #AutoarExtract stops all work, it will emit one of the three signals:
 * #AutoarExtract::cancelled, #AutoarExtract::error, and
 * #AutoarExtract::completed. After one of these signals is received,
 * the #AutoarExtract object should be destroyed because it cannot be used to
 * start another archive operation. An #AutoarExtract object can only be used
 * once and extract one archive.
 **/

G_DEFINE_TYPE (AutoarExtract, autoar_extract, G_TYPE_OBJECT)

/**
 * autoar_extract_quark:
 *
 * Gets the #AutoarExtract Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (autoar-extract, autoar_extract)

#define AUTOAR_EXTRACT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_EXTRACT, AutoarExtractPrivate))

#define BUFFER_SIZE (64 * 1024)
#define NOT_AN_ARCHIVE_ERRNO 2013
#define EMPTY_ARCHIVE_ERRNO 2014

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

  gint64 notify_interval;

  /* Variables used to show progess */
  guint64 size;
  guint64 completed_size;

  guint files;
  guint completed_files;

  gint64 notify_last;

  /* Internal variables */
  GInputStream *istream;
  void         *buffer;
  gssize        buffer_size;
  GError       *error;

  GList *files_list;

  GHashTable *userhash;
  GHashTable *grouphash;
  GArray     *extracted_dir_list;
  GFile      *destination_dir;

  GFile *prefix;
  GFile *new_prefix;

  char *suggested_destname;

  int in_thread         : 1;
  int use_raw_format    : 1;
};

struct _GFileAndInfo
{
  GFile *file;
  GFileInfo *info;
};

enum
{
  SCANNED,
  DECIDE_DESTINATION,
  PROGRESS,
  CONFLICT,
  CANCELLED,
  COMPLETED,
  AR_ERROR,
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
  PROP_OUTPUT_IS_DEST,
  PROP_NOTIFY_INTERVAL
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
    case PROP_NOTIFY_INTERVAL:
      g_value_set_int64 (value, priv->notify_interval);
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
    case PROP_SOURCE_IS_MEM:
      priv->source_is_mem = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_IS_DEST:
      autoar_extract_set_output_is_dest (arextract, g_value_get_boolean (value));
      break;
    case PROP_NOTIFY_INTERVAL:
      autoar_extract_set_notify_interval (arextract, g_value_get_int64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 * autoar_extract_get_source:
 * @arextract: an #AutoarExtract
 *
 * If #AutoarExtract:source_is_mem is %TRUE, gets the descriptive string for
 * the source memory buffer. Otherwise, gets the source file will be extracted
 * for this object. It may be a filename or URI.
 *
 * Returns: (transfer none): a string
 **/
char*
autoar_extract_get_source (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->source;
}

/**
 * autoar_extract_get_source_file:
 * @arextract: an #AutoarExtract
 *
 * If #AutoarExtract:source_is_mem is %TRUE, gets the #GFile object generated
 * using the value of #AutoarExtract:source. The returned #GFile is not usable
 * at all. Otherwise, gets the #GFile object which represents the source
 * archive will be extracted for this object.
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_extract_get_source_file (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->source_file;
}

/**
 * autoar_extract_get_output:
 * @arextract: an #AutoarExtract
 *
 * If #AutoarExtract:output_is_dest is %FALSE, gets the directory which contains
 * the extracted file or directory. Otherwise, get the filename of the extracted
 * file or directory itself. See autoar_extract_set_output_is_dest().
 *
 * Returns: (transfer none): a filename
 **/
char*
autoar_extract_get_output (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->output;
}

/**
 * autoar_extract_get_output_file:
 * @arextract: an #AutoarExtract
 *
 * This function is similar to autoar_extract_get_output(), except for the
 * return value is a #GFile.
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_extract_get_output_file (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->priv->output_file;
}

/**
 * autoar_extract_get_size:
 * @arextract: an #AutoarExtract
 *
 * Gets the size in bytes will be written when the operation is completed.
 *
 * Returns: total size of extracted files in bytes
 **/
guint64
autoar_extract_get_size (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->size;
}

/**
 * autoar_extract_get_completed_size:
 * @arextract: an #AutoarExtract
 *
 * Gets the size in bytes has been written to disk.
 *
 * Returns: size in bytes has been written
 **/
guint64
autoar_extract_get_completed_size (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->completed_size;
}

/**
 * autoar_extract_get_files:
 * @arextract: an #AutoarExtract
 *
 * Gets the total number of files will be written when the operation is
 * completed.
 *
 * Returns: total number of extracted files
 **/
guint
autoar_extract_get_files (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->files;
}

/**
 * autoar_extract_get_completed_files:
 * @arextract: an #AutoarExtract
 *
 * Gets the number of files has been written to disk.
 *
 * Returns: number of files has been written to disk
 **/
guint
autoar_extract_get_completed_files (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->completed_files;
}

/**
 * autoar_extract_get_source_is_mem:
 * @arextract: an #AutoarExtract
 *
 * Gets whether the source archive is a memory buffer.
 *
 * Returns: %TRUE if the source archive is a memory buffer.
 **/
gboolean
autoar_extract_get_source_is_mem (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), FALSE);
  return arextract->priv->source_is_mem;
}

/**
 * autoar_extract_get_output_is_dest:
 * @arextract: an #AutoarExtract
 *
 * See autoar_extract_set_output_is_dest().
 *
 * Returns: %TRUE if #AutoarExtract:output is the location of extracted file
 * or directory
 **/
gboolean
autoar_extract_get_output_is_dest (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), FALSE);
  return arextract->priv->source_is_mem;
}

/**
 * autoar_extract_get_notify_interval:
 * @arextract: an #AutoarExtract
 *
 * See autoar_extract_set_notify_interval().
 *
 * Returns: the minimal interval in microseconds between the emission of the
 * #AutoarExtract::progress signal.
 **/
gint64
autoar_extract_get_notify_interval (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), 0);
  return arextract->priv->notify_interval;
}

/**
 * autoar_extract_set_output_is_dest:
 * @arextract: an #AutoarExtract
 * @output_is_dest: %TRUE if the location of the extracted directory or file
 * has been already decided
 *
 * By default #AutoarExtract:output-is-dest is set to %FALSE, which means
 * only one file or directory will be generated. The destination is internally
 * determined by analyzing the contents of the archive. If this is not wanted,
 * #AutoarExtract:output-is-dest can be set to %TRUE, which will make
 * #AutoarExtract:output the destination for extracted files. In any case, the
 * destination directory will be notified via #AutoarExtract::decide-destination,
 * when it is possible to set a new destination.
 *
 * #AutoarExtract will attempt to create the destination directory regardless
 * to whether its path was internally decided or not.

 * This function should only be called before calling autoar_extract_start() or
 * autoar_extract_start_async().
 **/
void
autoar_extract_set_output_is_dest  (AutoarExtract *arextract,
                                    gboolean output_is_dest)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->priv->output_is_dest = output_is_dest;
}

/**
 * autoar_extract_set_notify_interval:
 * @arextract: an #AutoarExtract
 * @notify_interval: the minimal interval in microseconds
 *
 * Sets the minimal interval between emission of #AutoarExtract::progress
 * signal. This prevent too frequent signal emission, which may cause
 * performance impact. If you do not want this feature, you can set the interval
 * to 0, so you will receive every progress update.
 **/
void
autoar_extract_set_notify_interval (AutoarExtract *arextract,
                                    gint64 notify_interval)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  g_return_if_fail (notify_interval >= 0);
  arextract->priv->notify_interval = notify_interval;
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
  g_clear_object (&(priv->destination_dir));
  g_clear_object (&(priv->cancellable));
  g_clear_object (&(priv->prefix));
  g_clear_object (&(priv->new_prefix));

  g_list_free_full (priv->files_list, g_object_unref);
  priv->files_list = NULL;

  if (priv->userhash != NULL) {
    g_hash_table_unref (priv->userhash);
    priv->userhash = NULL;
  }

  if (priv->grouphash != NULL) {
    g_hash_table_unref (priv->grouphash);
    priv->grouphash = NULL;
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

  g_debug ("libarchive_read_read_cb: %" G_GSSIZE_FORMAT, read_size);
  return read_size;
}

static gint64
libarchive_read_seek_cb (struct archive *ar_read,
                         void *client_data,
                         gint64 request,
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

static gint64
libarchive_read_skip_cb (struct archive *ar_read,
                         void *client_data,
                         gint64 request)
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
g_file_and_info_free (void *g_file_and_info)
{
  GFileAndInfo *fi = g_file_and_info;
  g_object_unref (fi->file);
  g_object_unref (fi->info);
}

static inline void
autoar_extract_signal_scanned (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[SCANNED], 0,
                               arextract->priv->files);
}

static inline void
autoar_extract_signal_decide_destination (AutoarExtract *arextract,
                                          GFile *destination,
                                          GList *files,
                                          GFile **new_destination)
{
  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[DECIDE_DESTINATION], 0,
                               destination,
                               files,
                               new_destination);
}

static inline void
autoar_extract_signal_progress (AutoarExtract *arextract)
{
  gint64 mtime;
  mtime = g_get_monotonic_time ();
  if (mtime - arextract->priv->notify_last >= arextract->priv->notify_interval) {
    autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                                 autoar_extract_signals[PROGRESS], 0,
                                 arextract->priv->completed_size,
                                 arextract->priv->completed_files);
    arextract->priv->notify_last = mtime;
  }
}

static AutoarConflictAction
autoar_extract_signal_conflict (AutoarExtract *arextract,
                                GFile *file,
                                GFile **new_file)
{
  AutoarConflictAction action = AUTOAR_CONFLICT_OVERWRITE;

  autoar_common_g_signal_emit (arextract, arextract->priv->in_thread,
                               autoar_extract_signals[CONFLICT], 0,
                               file,
                               new_file,
                               &action);

  if (*new_file) {
    g_autofree char *previous_path, *new_path;

    previous_path = g_file_get_path (file);
    new_path = g_file_get_path (*new_file);

    g_debug ("autoar_extract_signal_conflict: %s => %s", previous_path, new_path);
  }

  return action;
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
                                   autoar_extract_signals[AR_ERROR], 0,
                                   arextract->priv->error);
    }
  }
}

static GFile*
autoar_extract_get_common_prefix (GList *files,
                                  GFile *root)
{
  GFile *prefix;
  GFile *file;
  GList *l;

  prefix = g_object_ref (files->data);

  if (!g_file_has_prefix (prefix, root)) {
    g_object_unref (prefix);
    return NULL;
  }

  while (!g_file_has_parent (prefix, root)) {
    file = g_file_get_parent (prefix);
    g_object_unref (prefix);
    prefix = file;
  }

  for (l = files->next; l; l = l->next) {
    file = l->data;

    if (!g_file_has_prefix (file, prefix) && !g_file_equal (file, prefix)) {
      g_object_unref (prefix);
      return NULL;
    }
  }

  return prefix;
}

static GFile*
autoar_extract_do_sanitize_pathname (AutoarExtract *arextract,
                                     const char *pathname)
{
  AutoarExtractPrivate *priv = arextract->priv;
  GFile *extracted_filename;
  gboolean valid_filename;

  extracted_filename = g_file_get_child (priv->destination_dir, pathname);

  valid_filename = g_file_equal (extracted_filename, priv->destination_dir) ||
                   g_file_has_prefix (extracted_filename, priv->destination_dir);

  if (!valid_filename) {
    g_autofree char *basename;

    basename = g_file_get_basename (extracted_filename);

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (priv->destination_dir,
                                           basename);
  }

  if (priv->prefix != NULL && priv->new_prefix != NULL) {
    g_autofree char *relative_path;
    /* Replace the old prefix with the new one */
    relative_path = g_file_get_relative_path (priv->prefix,
                                              extracted_filename);

    relative_path = relative_path != NULL ? relative_path : g_strdup ("");

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (priv->new_prefix, relative_path);
  }

  {
    g_autofree char *sanitized_pathname;

    sanitized_pathname = g_file_get_path (extracted_filename);

    g_debug ("autoar_extract_do_sanitize_pathname: %s", sanitized_pathname);
  }

  return extracted_filename;
}

static gboolean
autoar_extract_check_file_conflict (GFile *file,
                                    mode_t extracted_filetype)
{
  GFileType file_type;
  gboolean conflict = FALSE;

  file_type = g_file_query_file_type (file,
                                      G_FILE_QUERY_INFO_NONE,
                                      NULL);
  /* If there is no file with the given name, there will be no conflict */
  if (file_type == G_FILE_TYPE_UNKNOWN) {
    return conflict;
  }

  switch (extracted_filetype) {
    case AE_IFDIR:
      break;
    case AE_IFREG:
    case AE_IFLNK:
#if defined HAVE_MKFIFO || defined HAVE_MKNOD
    case AE_IFIFO:
#endif
#ifdef HAVE_MKNOD
    case AE_IFSOCK:
    case AE_IFBLK:
    case AE_IFCHR:
#endif
      conflict = TRUE;
      break;
    default:
      break;
  }

  return conflict;
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
        gint64 offset;

        g_debug ("autoar_extract_do_write_entry: case REG");

        ostream = (GOutputStream*)g_file_replace (dest,
                                                  NULL,
                                                  FALSE,
                                                  G_FILE_CREATE_NONE,
                                                  priv->cancellable,
                                                  &(priv->error));
        if (priv->error != NULL) {
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
          /* "File exists" is not a fatal error, as long as the existing file
           * is a directory
           */
          GFileType file_type;

          file_type = g_file_query_file_type (dest,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL);

          if (g_error_matches (priv->error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
              file_type == G_FILE_TYPE_DIRECTORY) {
            g_clear_error (&priv->error);
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
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_SIZE,
                                   g_param_spec_uint64 ("completed-size",
                                                        "Written file size",
                                                        "Bytes written to disk",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILES,
                                   g_param_spec_uint ("files",
                                                      "Files",
                                                      "Number of files in the archive",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_FILES,
                                   g_param_spec_uint ("completed-files",
                                                      "Written files",
                                                      "Number of files has been written",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READABLE |
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
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_NOTIFY_INTERVAL,
                                   g_param_spec_int64 ("notify-interval",
                                                       "Notify interval",
                                                       "Minimal time interval between progress signal",
                                                       0, G_MAXINT64, 100000,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_STATIC_STRINGS));

/**
 * AutoarExtract::scanned:
 * @arextract: the #AutoarExtract
 * @files: the number of files will be extracted from the source archive
 *
 * This signal is emitted when #AutoarExtract finish scanning filename entries
 * in the source archive.
 **/
  autoar_extract_signals[SCANNED] =
    g_signal_new ("scanned",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);

/**
 * AutoarExtract::decide-destination:
 * @arextract: the #AutoarExtract
 * @destination: the location where files will be extracted
 * @files: the list of files to be extracted. All have @destination as their
           common prefix
 *
 * Returns: (transfer full): a new destination that will overwrite the previous
 *                           one, or %NULL if this is not wanted
 *
 * This signal is emitted when the path of the destination directory is
 * determined. It is useful for solving name conflicts or for setting a new
 * destination, based on the contents of the archive.
 **/
  autoar_extract_signals[DECIDE_DESTINATION] =
    g_signal_new ("decide-destination",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_OBJECT,
                  2,
                  G_TYPE_FILE,
                  G_TYPE_POINTER);

/**
 * AutoarExtract::progress:
 * @arextract: the #AutoarExtract
 * @completed_size: bytes has been written to disk
 * @completed_files: number of files have been written to disk
 *
 * This signal is used to report progress of creating archives.
 **/
  autoar_extract_signals[PROGRESS] =
    g_signal_new ("progress",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_UINT64,
                  G_TYPE_UINT);

/**
 * AutoarExtract::conflict:
 * @arextract: the #AutoarExtract
 * @file: the file that caused a conflict
 * @new_file: an address to store the new destination for a conflict file
 *
 * Returns: the action to be performed by #AutoarExtract
 *
 * This signal is used to report and offer the possibility to solve name
 * conflicts when extracting files.
 **/
  autoar_extract_signals[CONFLICT] =
    g_signal_new ("conflict",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_UINT,
                  2,
                  G_TYPE_FILE,
                  G_TYPE_POINTER);

/**
 * AutoarExtract::cancelled:
 * @arextract: the #AutoarExtract
 *
 * This signal is emitted after archive extracting job is cancelled by the
 * #GCancellable.
 **/
  autoar_extract_signals[CANCELLED] =
    g_signal_new ("cancelled",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarExtract::completed:
 * @arextract: the #AutoarExtract
 *
 * This signal is emitted after the archive extracting job is successfully
 * completed.
 **/
  autoar_extract_signals[COMPLETED] =
    g_signal_new ("completed",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarExtract::error:
 * @arextract: the #AutoarExtract
 * @error: the #GError
 *
 * This signal is emitted when error occurs and all jobs should be terminated.
 * Possible error domains are %AUTOAR_EXTRACT_ERROR, %G_IO_ERROR, and
 * %AUTOAR_LIBARCHIVE_ERROR, which represent error occurs in #AutoarExtract,
 * GIO, and libarchive, respectively. The #GError is owned by #AutoarExtract
 * and should not be freed.
 **/
  autoar_extract_signals[AR_ERROR] =
    g_signal_new ("error",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_ERROR);
}

static void
autoar_extract_init (AutoarExtract *arextract)
{
  AutoarExtractPrivate *priv;

  priv = AUTOAR_EXTRACT_GET_PRIVATE (arextract);
  arextract->priv = priv;

  priv->source_buffer = NULL;
  priv->source_buffer_size = 0;

  priv->cancellable = NULL;

  priv->size = 0;
  priv->completed_size = 0;

  priv->files_list = NULL;

  priv->files = 0;
  priv->completed_files = 0;

  priv->notify_last = 0;

  priv->istream = NULL;
  priv->buffer_size = BUFFER_SIZE;
  priv->buffer = g_new (char, priv->buffer_size);
  priv->error = NULL;

  priv->userhash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->grouphash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  priv->extracted_dir_list = g_array_new (FALSE, FALSE, sizeof (GFileAndInfo));
  g_array_set_clear_func (priv->extracted_dir_list, g_file_and_info_free);
  priv->destination_dir = NULL;
  priv->new_prefix = NULL;

  priv->suggested_destname = NULL;

  priv->in_thread = FALSE;
  priv->use_raw_format = FALSE;
}

static AutoarExtract*
autoar_extract_new_full (const char *source,
                         GFile *source_file,
                         const char *output,
                         GFile *output_file,
                         gboolean source_is_mem,
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
      gen_source = autoar_common_g_file_get_name (source_file);
    if (source_file == NULL)
      gen_source_file = g_file_new_for_commandline_arg (source);
  }

  if (output == NULL)
    gen_output = autoar_common_g_file_get_name (output_file);
  if (output_file == NULL)
    gen_output_file = g_file_new_for_commandline_arg (output);

  arextract =
    g_object_new (AUTOAR_TYPE_EXTRACT,
                  "source",         source      != NULL ? source      : gen_source,
                  "source-file",    source_file != NULL ? source_file : gen_source_file,
                  "output",         output      != NULL ? output      : gen_output,
                  "output-file",    output_file != NULL ? output_file : gen_output_file,
                  "source-is-mem",  source_is_mem, NULL);
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
    g_free (source_basename);
  }

  g_free (gen_source);
  g_free (gen_output);

  if (gen_source_file != NULL)
    g_object_unref (gen_source_file);
  if (gen_output_file != NULL)
    g_object_unref (gen_output_file);

  return arextract;
}


/**
 * autoar_extract_new:
 * @source: source archive
 * @output: output directory of extracted file or directory, or the file name
 * of the extracted file or directory itself if you set
 * #AutoarExtract:output-is-dest on the returned object
 * @arpref: an #AutoarPref object
 *
 * Extract a new #AutoarExtract object.
 *
 * Returns: (transfer full): a new #AutoarExtract object
 **/
AutoarExtract*
autoar_extract_new (const char *source,
                    const char *output,
                    AutoarPref *arpref)
{
  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (output != NULL, NULL);

  return autoar_extract_new_full (source, NULL, output, NULL,
                                  FALSE, arpref,
                                  NULL, 0, NULL);
}

/**
 * autoar_extract_new_file:
 * @source_file: source archive
 * @output_file: output directory of extracted file or directory, or the
 * file name of the extracted file or directory itself if you set
 * #AutoarExtract:output-is-dest on the returned object
 * @arpref: an #AutoarPref object
 *
 * Create a new #AutoarExtract object.
 *
 * Returns: (transfer full): a new #AutoarExtract object
 **/
AutoarExtract*
autoar_extract_new_file (GFile *source_file,
                         GFile *output_file,
                         AutoarPref *arpref)
{
  g_return_val_if_fail (source_file != NULL, NULL);
  g_return_val_if_fail (output_file != NULL, NULL);

  return autoar_extract_new_full (NULL, source_file, NULL, output_file,
                                  FALSE, arpref,
                                  NULL, 0, NULL);
}

/**
 * autoar_extract_new_memory:
 * @buffer: memory buffer holding the source archive
 * @buffer_size: the size of the source archive memory buffer
 * @source_name: the name of the source archive
 * @output: output directory of extracted file or directory, or the file name
 * of the extracted file or directory itself if you set
 * #AutoarExtract:output-is-dest on the returned object
 * @arpref: an #AutoarPref object
 *
 * Create a new #AutoarExtract object. @source_name does not need to be a full
 * path. The file which it represents does not need to exist, either. This
 * argument is only used to decide the name of the extracted file or directory,
 * and it is useless if you set #AutoarExtract:output-is-dest to %TRUE.
 *
 * Returns: (transfer full): a new #AutoarExtract object
 **/
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
                                  TRUE, arpref,
                                  buffer, buffer_size, source_name);
}

/**
 * autoar_extract_new_memory_file:
 * @buffer: memory buffer holding the source archive
 * @buffer_size: the size of the source archive memory buffer
 * @source_name: the name of the source archive
 * @output_file: output directory of extracted file or directory, or the file
 * name of the extracted file or directory itself if you set
 * #AutoarExtract:output-is-dest on the returned object
 * @arpref: an #AutoarPref object
 *
 * Create a new #AutoarExtract object. This function is similar to
 * autoar_extract_new_memory() except for the argument for the output
 * directory is #GFile.
 *
 * Returns: (transfer full): a new #AutoarExtract object
 **/
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
                                  TRUE, arpref,
                                  buffer, buffer_size, source_name);
}

static void
autoar_extract_step_scan_toplevel (AutoarExtract *arextract)
{
  /* Step 0: Scan all file names in the archive
   * We have to check whether the archive contains a top-level directory
   * before performing the extraction. We emit the "scanned" signal when
   * the checking is completed. */

  struct archive *a;
  struct archive_entry *entry;

  AutoarExtractPrivate *priv;
  int r;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_scan_toplevel: called");

  r = libarchive_create_read_object (FALSE, arextract, &a);
  if (r != ARCHIVE_OK) {
    archive_read_free (a);
    r = libarchive_create_read_object (TRUE, arextract, &a);
    if (r != ARCHIVE_OK) {
      if (priv->error == NULL)
        priv->error = autoar_common_g_error_new_a (a, priv->source);
      return;
    } else if (archive_filter_count (a) <= 1){
      /* If we only use raw format and filter count is one, libarchive will
       * not do anything except for just copying the source file. We do not
       * want this thing to happen because it does unnecesssary copying. */
      if (priv->error == NULL)
        priv->error = g_error_new (AUTOAR_EXTRACT_ERROR, NOT_AN_ARCHIVE_ERRNO,
                                   "\'%s\': %s", priv->source, "not an archive");
      return;
    }
    priv->use_raw_format = TRUE;
  }

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;

    if (g_cancellable_is_cancelled (priv->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    g_debug ("autoar_extract_step_scan_toplevel: %d: pathname = %s", priv->files, pathname);

    priv->files_list = g_list_prepend (priv->files_list,
                                       g_file_get_child (priv->output_file, pathname));

    priv->files++;
    priv->size += archive_entry_size (entry);
    archive_read_data_skip (a);
  }

  if (priv->files_list == NULL) {
    if (priv->error == NULL) {
      priv->error = g_error_new (AUTOAR_EXTRACT_ERROR, EMPTY_ARCHIVE_ERRNO,
                                 "\'%s\': %s", priv->source, "empty archive");
    }
    archive_read_free (a);
    return;
  }

  if (r != ARCHIVE_EOF) {
    if (priv->error == NULL) {
      priv->error = autoar_common_g_error_new_a (a, priv->source);
    }
    archive_read_free (a);
    return;
  }

  /* If we are unable to determine the total size, set it to a positive
   * number to prevent strange percentage. */
  if (priv->size <= 0)
    priv->size = G_MAXUINT64;

  archive_read_free (a);

  g_debug ("autoar_extract_step_scan_toplevel: files = %d", priv->files);

  priv->files_list = g_list_reverse (priv->files_list);

  priv->prefix = autoar_extract_get_common_prefix (priv->files_list, priv->output_file);

  if (priv->prefix != NULL) {
    g_autofree char *path_prefix;

    path_prefix = g_file_get_path (priv->prefix);
    g_debug ("autoar_extract_step_scan_toplevel: pathname_prefix = %s", path_prefix);
  }

  autoar_extract_signal_scanned (arextract);
}

static void
autoar_extract_step_set_dest (AutoarExtract *arextract)
{
  /* Step 1: Set destination based on client preferences or archive contents */

  AutoarExtractPrivate *priv;

  priv = arextract->priv;

  g_debug ("autoar_extract_step_set_dest: called");

  if (priv->output_is_dest) {
    priv->destination_dir = g_object_ref (arextract->priv->output_file);
    return;
  }

  if (priv->prefix != NULL) {
    /* We must check if the archive and the prefix have the same name (without
     * the extension). If they do, then the destination should be the output
     * directory itself.
     */
    g_autofree char *prefix_name;
    g_autofree char *prefix_name_no_ext;

    prefix_name = g_file_get_basename (priv->prefix);
    prefix_name_no_ext = autoar_common_get_basename_remove_extension (prefix_name);

    if (g_strcmp0 (prefix_name_no_ext, priv->suggested_destname) == 0) {
      priv->destination_dir = g_object_ref (priv->output_file);
    } else {
      g_clear_object (&priv->prefix);
    }
  }
  /* If none of the above situations apply, the top level directory gets the
   * name suggested when creating the AutoarExtract object
   */
  if (priv->destination_dir == NULL) {
    priv->destination_dir = g_file_get_child (priv->output_file,
                                              priv->suggested_destname);
  }
}

static void
autoar_extract_step_decide_dest (AutoarExtract *arextract)
{
  /* Step 2: Confirm destination */

  AutoarExtractPrivate *priv;
  GList *files = NULL;
  GList *l;
  GFile *new_destination = NULL;

  priv = arextract->priv;

  for (l = priv->files_list; l != NULL; l = l->next) {
    char *relative_path;
    GFile *file;

    relative_path = g_file_get_relative_path (priv->output_file, l->data);
    file = g_file_resolve_relative_path (priv->destination_dir,
                                         relative_path);
    files = g_list_prepend (files, file);

    g_free (relative_path);
  }

  files = g_list_reverse (files);

  /* When it exists, the common prefix is the actual output of the extraction
   * and the client has the opportunity to change it. Also, the old prefix is
   * needed in order to replace it with the new one
   */
  if (priv->prefix != NULL) {
    autoar_extract_signal_decide_destination (arextract,
                                              priv->prefix,
                                              files,
                                              &new_destination);

    priv->new_prefix = new_destination;
  } else {
    autoar_extract_signal_decide_destination (arextract,
                                              priv->destination_dir,
                                              files,
                                              &new_destination);

    if (new_destination) {
      g_object_unref (priv->destination_dir);
      priv->destination_dir = new_destination;
    }
  }

  {
    g_autofree char *destination_name;

    destination_name = g_file_get_path (priv->new_prefix != NULL ?
                                        priv->new_prefix :
                                        priv->destination_dir);
    g_debug ("autoar_extract_step_decide_dest: destination %s", destination_name);
  }

  g_file_make_directory_with_parents (priv->destination_dir, priv->cancellable,
                                      &(priv->error));

  if (g_error_matches (priv->error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
    GFileType file_type;

    file_type = g_file_query_file_type (priv->destination_dir,
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL);

    if (file_type == G_FILE_TYPE_DIRECTORY) {
      g_debug ("autoar_extract_step_decide_dest: destination directory exists");
      g_clear_error (&priv->error);
    }
  }

  g_list_free_full (files, g_object_unref);
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
    g_autoptr (GFile) extracted_filename = NULL;
    g_autoptr (GFile) hardlink_filename = NULL;

    if (g_cancellable_is_cancelled (priv->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    hardlink = archive_entry_hardlink (entry);

    extracted_filename =
      autoar_extract_do_sanitize_pathname (arextract, pathname);

    if (hardlink != NULL) {
      hardlink_filename =
        autoar_extract_do_sanitize_pathname (arextract, pathname);
    }

    /* Attempt to solve any name conflict before doing any operations */
    if (autoar_extract_check_file_conflict (extracted_filename,
                                            archive_entry_filetype (entry))) {
      GFile *new_extracted_filename = NULL;
      AutoarConflictAction action;

      action = autoar_extract_signal_conflict (arextract,
                                               extracted_filename,
                                               &new_extracted_filename);

      switch (action) {
        case AUTOAR_CONFLICT_OVERWRITE:
          break;
        case AUTOAR_CONFLICT_CHANGE_DESTINATION:
          g_assert_nonnull (new_extracted_filename);
          g_clear_object (&extracted_filename);
          extracted_filename = new_extracted_filename;
          break;
        case AUTOAR_CONFLICT_SKIP:
          archive_read_data_skip (a);
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      if (action == AUTOAR_CONFLICT_SKIP) {
        continue;
      }
    }

    autoar_extract_do_write_entry (arextract, a, entry,
                                   extracted_filename, hardlink_filename);

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
  priv->notify_last = 0;
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
  steps[i++] = autoar_extract_step_scan_toplevel;
  steps[i++] = autoar_extract_step_set_dest;
  steps[i++] = autoar_extract_step_decide_dest;
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

/**
 * autoar_extract_start:
 * @arextract: an #AutoarExtract object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Runs the archive extracting work. All callbacks will be called in the same
 * thread as the caller of this functions.
 **/
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


/**
 * autoar_extract_start_async:
 * @arextract: an #AutoarExtract object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Asynchronously runs the archive extracting work. You should connect to
 * #AutoarExtract::cancelled, #AutoarExtract::error, and
 * #AutoarExtract::completed signal to get notification when the work is
 * terminated. All callbacks will be called in the main thread, so you can
 * safely manipulate GTK+ widgets in the callbacks.
 **/
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

/**
 * autoar_extract_free_source_buffer:
 * @arextract: an #AutoarExtract object
 * @free_func: a function to free the memory buffer
 *
 * Free the source memory archive provided in autoar_extract_new_memory() or
 * autoar_extract_new_memory_file(). This functions should only be called
 * after the extracting job is completed. That is, you should only call this
 * function after you receives one of #AutoarExtract::cancelled,
 * #AutoarExtract::error, or #AutoarExtract::completed signal.
 **/
void
autoar_extract_free_source_buffer (AutoarExtract *arextract,
                                   GDestroyNotify free_func)
{
  if (arextract->priv->source_buffer != NULL)
    (*free_func)((void*)(arextract->priv->source_buffer));

  arextract->priv->source_buffer = NULL;
  arextract->priv->source_buffer_size = 0;
}

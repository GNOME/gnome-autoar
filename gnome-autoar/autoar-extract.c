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

/**
 * autoar_extract_quark:
 *
 * Gets the #AutoarExtract Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (autoar-extract, autoar_extract)

#define BUFFER_SIZE (64 * 1024)
#define NOT_AN_ARCHIVE_ERRNO 2013
#define EMPTY_ARCHIVE_ERRNO 2014

typedef struct _GFileAndInfo GFileAndInfo;

struct _AutoarExtract
{
  GObject parent_instance;

  GFile *source_file;
  GFile *output_file;

  char *source_basename;

  int output_is_dest : 1;
  gboolean delete_after_extraction;

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

G_DEFINE_TYPE (AutoarExtract, autoar_extract, G_TYPE_OBJECT)

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
  PROP_SOURCE_FILE,
  PROP_OUTPUT_FILE,
  PROP_SIZE,
  PROP_COMPLETED_SIZE,
  PROP_FILES,
  PROP_COMPLETED_FILES,
  PROP_OUTPUT_IS_DEST,
  PROP_DELETE_AFTER_EXTRACTION,
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

  arextract = AUTOAR_EXTRACT (object);

  switch (property_id) {
    case PROP_SOURCE_FILE:
      g_value_set_object (value, arextract->source_file);
      break;
    case PROP_OUTPUT_FILE:
      g_value_set_object (value, arextract->output_file);
      break;
    case PROP_SIZE:
      g_value_set_uint64 (value, arextract->size);
      break;
    case PROP_COMPLETED_SIZE:
      g_value_set_uint64 (value, arextract->completed_size);
      break;
    case PROP_FILES:
      g_value_set_uint (value, arextract->files);
      break;
    case PROP_COMPLETED_FILES:
      g_value_set_uint (value, arextract->completed_files);
      break;
    case PROP_OUTPUT_IS_DEST:
      g_value_set_boolean (value, arextract->output_is_dest);
      break;
    case PROP_DELETE_AFTER_EXTRACTION:
      g_value_set_boolean (value, arextract->delete_after_extraction);
      break;
    case PROP_NOTIFY_INTERVAL:
      g_value_set_int64 (value, arextract->notify_interval);
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

  arextract = AUTOAR_EXTRACT (object);

  switch (property_id) {
    case PROP_SOURCE_FILE:
      g_clear_object (&(arextract->source_file));
      arextract->source_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_OUTPUT_FILE:
      g_clear_object (&(arextract->output_file));
      arextract->output_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_OUTPUT_IS_DEST:
      autoar_extract_set_output_is_dest (arextract, g_value_get_boolean (value));
      break;
    case PROP_DELETE_AFTER_EXTRACTION:
      autoar_extract_set_delete_after_extraction (arextract,
                                                  g_value_get_boolean (value));
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
 * autoar_extract_get_source_file:
 * @arextract: an #AutoarExtract
 *
 * Gets the #GFile object which represents the source archive that will be
 * extracted for this object.
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_extract_get_source_file (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), NULL);
  return arextract->source_file;
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
  return arextract->output_file;
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
  return arextract->size;
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
  return arextract->completed_size;
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
  return arextract->files;
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
  return arextract->completed_files;
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
  return arextract->output_is_dest;
}

/**
 * autoar_extract_get_delete_after_extraction:
 * @arextract: an #AutoarExtract
 *
 * Whether the source archive will be deleted after a successful extraction.
 *
 * Returns: %TRUE if the source archive will be deleted after a succesful
 * extraction
 **/
gboolean
autoar_extract_get_delete_after_extraction (AutoarExtract *arextract)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACT (arextract), FALSE);
  return arextract->delete_after_extraction;
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
  return arextract->notify_interval;
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
 * destination will be notified via #AutoarExtract::decide-destination, when it
 * is possible to set a new destination.
 *
 * #AutoarExtract will attempt to create the destination regardless to whether
 * its path was internally decided or not.

 * This function should only be called before calling autoar_extract_start() or
 * autoar_extract_start_async().
 **/
void
autoar_extract_set_output_is_dest  (AutoarExtract *arextract,
                                    gboolean output_is_dest)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->output_is_dest = output_is_dest;
}

/**
 * autoar_extract_set_delete_after_extraction:
 * @arextract: an #AutoarExtract
 * @delete_after_extraction: %TRUE if the source archive should be deleted after
 * a successful extraction
 *
 * By default #AutoarExtract:delete-after-extraction is set to %FALSE so the
 * source archive will not be automatically deleted if extraction succeeds.
 **/
void
autoar_extract_set_delete_after_extraction (AutoarExtract *arextract,
                                            gboolean       delete_after_extraction)
{
  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));
  arextract->delete_after_extraction = delete_after_extraction;
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
  arextract->notify_interval = notify_interval;
}

static void
autoar_extract_dispose (GObject *object)
{
  AutoarExtract *arextract;

  arextract = AUTOAR_EXTRACT (object);

  g_debug ("AutoarExtract: dispose");

  if (arextract->istream != NULL) {
    if (!g_input_stream_is_closed (arextract->istream)) {
      g_input_stream_close (arextract->istream, arextract->cancellable, NULL);
    }
    g_object_unref (arextract->istream);
    arextract->istream = NULL;
  }

  g_clear_object (&(arextract->source_file));
  g_clear_object (&(arextract->output_file));
  g_clear_object (&(arextract->destination_dir));
  g_clear_object (&(arextract->cancellable));
  g_clear_object (&(arextract->prefix));
  g_clear_object (&(arextract->new_prefix));

  g_list_free_full (arextract->files_list, g_object_unref);
  arextract->files_list = NULL;

  if (arextract->userhash != NULL) {
    g_hash_table_unref (arextract->userhash);
    arextract->userhash = NULL;
  }

  if (arextract->grouphash != NULL) {
    g_hash_table_unref (arextract->grouphash);
    arextract->grouphash = NULL;
  }

  if (arextract->extracted_dir_list != NULL) {
    g_array_unref (arextract->extracted_dir_list);
    arextract->extracted_dir_list = NULL;
  }

  G_OBJECT_CLASS (autoar_extract_parent_class)->dispose (object);
}

static void
autoar_extract_finalize (GObject *object)
{
  AutoarExtract *arextract;

  arextract = AUTOAR_EXTRACT (object);

  g_debug ("AutoarExtract: finalize");

  g_free (arextract->buffer);
  arextract->buffer = NULL;

  if (arextract->error != NULL) {
    g_error_free (arextract->error);
    arextract->error = NULL;
  }

  g_free (arextract->suggested_destname);
  arextract->suggested_destname = NULL;

  G_OBJECT_CLASS (autoar_extract_parent_class)->finalize (object);
}

static int
libarchive_read_open_cb (struct archive *ar_read,
                         void *client_data)
{
  AutoarExtract *arextract;
  GFileInputStream *istream;

  g_debug ("libarchive_read_open_cb: called");

  arextract = AUTOAR_EXTRACT (client_data);

  if (arextract->error != NULL)
    return ARCHIVE_FATAL;

  istream = g_file_read (arextract->source_file,
                         arextract->cancellable,
                         &(arextract->error));
  arextract->istream = G_INPUT_STREAM (istream);

  if (arextract->error != NULL)
    return ARCHIVE_FATAL;

  g_debug ("libarchive_read_open_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static int
libarchive_read_close_cb (struct archive *ar_read,
                          void *client_data)
{
  AutoarExtract *arextract;

  g_debug ("libarchive_read_close_cb: called");

  arextract = AUTOAR_EXTRACT (client_data);

  if (arextract->error != NULL)
    return ARCHIVE_FATAL;

  if (arextract->istream != NULL) {
    g_input_stream_close (arextract->istream, arextract->cancellable, NULL);
    g_object_unref (arextract->istream);
    arextract->istream = NULL;
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
  gssize read_size;

  g_debug ("libarchive_read_read_cb: called");

  arextract = AUTOAR_EXTRACT (client_data);

  if (arextract->error != NULL || arextract->istream == NULL)
    return -1;

  *buffer = arextract->buffer;
  read_size = g_input_stream_read (arextract->istream,
                                   arextract->buffer,
                                   arextract->buffer_size,
                                   arextract->cancellable,
                                   &(arextract->error));
  if (arextract->error != NULL)
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
  GSeekable *seekable;
  GSeekType  seektype;
  off_t new_offset;

  g_debug ("libarchive_read_seek_cb: called");

  arextract = AUTOAR_EXTRACT (client_data);
  seekable = (GSeekable*)(arextract->istream);
  if (arextract->error != NULL || arextract->istream == NULL)
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
                   arextract->cancellable,
                   &(arextract->error));
  new_offset = g_seekable_tell (seekable);
  if (arextract->error != NULL)
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
  GSeekable *seekable;
  off_t old_offset, new_offset;

  g_debug ("libarchive_read_skip_cb: called");

  arextract = AUTOAR_EXTRACT (client_data);
  seekable = (GSeekable*)(arextract->istream);
  if (arextract->error != NULL || arextract->istream == NULL) {
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
  autoar_common_g_signal_emit (arextract, arextract->in_thread,
                               autoar_extract_signals[SCANNED], 0,
                               arextract->files);
}

static inline void
autoar_extract_signal_decide_destination (AutoarExtract *arextract,
                                          GFile *destination,
                                          GList *files,
                                          GFile **new_destination)
{
  autoar_common_g_signal_emit (arextract, arextract->in_thread,
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
  if (mtime - arextract->notify_last >= arextract->notify_interval) {
    autoar_common_g_signal_emit (arextract, arextract->in_thread,
                                 autoar_extract_signals[PROGRESS], 0,
                                 arextract->completed_size,
                                 arextract->completed_files);
    arextract->notify_last = mtime;
  }
}

static AutoarConflictAction
autoar_extract_signal_conflict (AutoarExtract *arextract,
                                GFile *file,
                                GFile **new_file)
{
  AutoarConflictAction action = AUTOAR_CONFLICT_OVERWRITE;

  autoar_common_g_signal_emit (arextract, arextract->in_thread,
                               autoar_extract_signals[CONFLICT], 0,
                               file,
                               new_file,
                               &action);

  if (*new_file) {
    g_autofree char *previous_path;
    g_autofree char *new_path;

    previous_path = g_file_get_path (file);
    new_path = g_file_get_path (*new_file);

    g_debug ("autoar_extract_signal_conflict: %s => %s", previous_path, new_path);
  }

  return action;
}

static inline void
autoar_extract_signal_cancelled (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->in_thread,
                               autoar_extract_signals[CANCELLED], 0);

}

static inline void
autoar_extract_signal_completed (AutoarExtract *arextract)
{
  autoar_common_g_signal_emit (arextract, arextract->in_thread,
                               autoar_extract_signals[COMPLETED], 0);

}

static inline void
autoar_extract_signal_error (AutoarExtract *arextract)
{
  if (arextract->error != NULL) {
    if (arextract->error->domain == G_IO_ERROR &&
        arextract->error->code == G_IO_ERROR_CANCELLED) {
      g_error_free (arextract->error);
      arextract->error = NULL;
      autoar_extract_signal_cancelled (arextract);
    } else {
      autoar_common_g_signal_emit (arextract, arextract->in_thread,
                                   autoar_extract_signals[AR_ERROR], 0,
                                   arextract->error);
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
  /* This can happen if the archive contains malformed paths that point outside
   * of it
   */
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
  GFile *extracted_filename;
  gboolean valid_filename;
  g_autofree char *sanitized_pathname;

  extracted_filename = g_file_get_child (arextract->destination_dir, pathname);

  valid_filename = g_file_equal (extracted_filename, arextract->destination_dir) ||
                   g_file_has_prefix (extracted_filename, arextract->destination_dir);

  if (!valid_filename) {
    g_autofree char *basename;

    basename = g_file_get_basename (extracted_filename);

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (arextract->destination_dir,
                                           basename);
  }

  if (arextract->prefix != NULL && arextract->new_prefix != NULL) {
    g_autofree char *relative_path;
    /* Replace the old prefix with the new one */
    relative_path = g_file_get_relative_path (arextract->prefix,
                                              extracted_filename);

    relative_path = relative_path != NULL ? relative_path : g_strdup ("");

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (arextract->new_prefix, relative_path);
  }

  sanitized_pathname = g_file_get_path (extracted_filename);

  g_debug ("autoar_extract_do_sanitize_pathname: %s", sanitized_pathname);

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
    return FALSE;
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
  GFileInfo *info;
  mode_t filetype;
  int r;

  {
    GFile *parent;
    parent = g_file_get_parent (dest);
    if (!g_file_query_exists (parent, arextract->cancellable))
      g_file_make_directory_with_parents (parent, arextract->cancellable, NULL);
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
      if (g_hash_table_lookup_extended (arextract->userhash, uname, NULL, &got_uid) == TRUE) {
        uid = GPOINTER_TO_UINT (got_uid);
      } else {
        struct passwd *pwd = getpwnam (uname);
        if (pwd == NULL) {
          uid = archive_entry_uid (entry);
        } else {
          uid = pwd->pw_uid;
          g_hash_table_insert (arextract->userhash, g_strdup (uname), GUINT_TO_POINTER (uid));
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
      if (g_hash_table_lookup_extended (arextract->grouphash, gname, NULL, &got_gid) == TRUE) {
        gid = GPOINTER_TO_UINT (got_gid);
      } else {
        struct group *grp = getgrnam (gname);
        if (grp == NULL) {
          gid = archive_entry_gid (entry);
        } else {
          gid = grp->gr_gid;
          g_hash_table_insert (arextract->grouphash, g_strdup (gname), GUINT_TO_POINTER (gid));
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
                                                  arextract->cancellable,
                                                  &(arextract->error));
        if (arextract->error != NULL) {
          g_object_unref (info);
          return;
        }

        if (ostream != NULL) {
          /* Archive entry size may be zero if we use raw format. */
          if (archive_entry_size(entry) > 0 || arextract->use_raw_format) {
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
                                         arextract->cancellable,
                                         &(arextract->error));
              if (arextract->error != NULL) {
                g_output_stream_close (ostream, arextract->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              if (g_cancellable_is_cancelled (arextract->cancellable)) {
                g_output_stream_close (ostream, arextract->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              arextract->completed_size += written;
              autoar_extract_signal_progress (arextract);
            }
          }
          g_output_stream_close (ostream, arextract->cancellable, NULL);
          g_object_unref (ostream);
        }
      }
      break;
    case AE_IFDIR:
      {
        GFileAndInfo fileandinfo;

        g_debug ("autoar_extract_do_write_entry: case DIR");

        g_file_make_directory_with_parents (dest, arextract->cancellable, &(arextract->error));

        if (arextract->error != NULL) {
          /* "File exists" is not a fatal error, as long as the existing file
           * is a directory
           */
          GFileType file_type;

          file_type = g_file_query_file_type (dest,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL);

          if (g_error_matches (arextract->error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
              file_type == G_FILE_TYPE_DIRECTORY) {
            g_clear_error (&arextract->error);
          } else {
            g_object_unref (info);
            return;
          }
        }

        fileandinfo.file = g_object_ref (dest);
        fileandinfo.info = g_object_ref (info);
        g_array_append_val (arextract->extracted_dir_list, fileandinfo);
      }
      break;
    case AE_IFLNK:
      g_debug ("autoar_extract_do_write_entry: case LNK");
      g_file_make_symbolic_link (dest,
                                 archive_entry_symlink (entry),
                                 arextract->cancellable,
                                 &(arextract->error));
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
    ostream = (GOutputStream*)g_file_append_to (dest, G_FILE_CREATE_NONE, arextract->cancellable, NULL);
    if (ostream != NULL) {
      g_output_stream_close (ostream, arextract->cancellable, NULL);
      g_object_unref (ostream);
    }
  }
#endif

applyinfo:
  g_debug ("autoar_extract_do_write_entry: applying info");
  g_file_set_attributes_from_info (dest,
                                   info,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   arextract->cancellable,
                                   &(arextract->error));

  if (arextract->error != NULL) {
    g_debug ("autoar_extract_do_write_entry: %s\n", arextract->error->message);
    g_error_free (arextract->error);
    arextract->error = NULL;
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

  object_class->get_property = autoar_extract_get_property;
  object_class->set_property = autoar_extract_set_property;
  object_class->dispose = autoar_extract_dispose;
  object_class->finalize = autoar_extract_finalize;

  g_object_class_install_property (object_class, PROP_SOURCE_FILE,
                                   g_param_spec_object ("source-file",
                                                        "Source archive GFile",
                                                        "The archive GFile to be extracted",
                                                        G_TYPE_FILE,
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

  g_object_class_install_property (object_class, PROP_OUTPUT_IS_DEST,
                                   g_param_spec_boolean ("output-is-dest",
                                                         "Output is destination",
                                                         "Whether output direcotry is used as destination",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_DELETE_AFTER_EXTRACTION,
                                   g_param_spec_boolean ("delete-after-extraction",
                                                         "Delete after extraction",
                                                         "Whether the source archive is deleted after "
                                                         "a successful extraction",
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
 * This signal is emitted when the path of the destination is determined. It is
 * useful for solving name conflicts or for setting a new destination, based on
 * the contents of the archive.
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
  arextract->cancellable = NULL;

  arextract->size = 0;
  arextract->completed_size = 0;

  arextract->files_list = NULL;

  arextract->files = 0;
  arextract->completed_files = 0;

  arextract->notify_last = 0;

  arextract->istream = NULL;
  arextract->buffer_size = BUFFER_SIZE;
  arextract->buffer = g_new (char, arextract->buffer_size);
  arextract->error = NULL;

  arextract->userhash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  arextract->grouphash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  arextract->extracted_dir_list = g_array_new (FALSE, FALSE, sizeof (GFileAndInfo));
  g_array_set_clear_func (arextract->extracted_dir_list, g_file_and_info_free);
  arextract->destination_dir = NULL;
  arextract->new_prefix = NULL;

  arextract->suggested_destname = NULL;

  arextract->in_thread = FALSE;
  arextract->use_raw_format = FALSE;
}

/**
 * autoar_extract_new:
 * @source_file: source archive
 * @output_file: output directory of extracted file or directory, or the
 * file name of the extracted file or directory itself if you set
 * #AutoarExtract:output-is-dest on the returned object
 *
 * Create a new #AutoarExtract object.
 *
 * Returns: (transfer full): a new #AutoarExtract object
 **/
AutoarExtract*
autoar_extract_new (GFile *source_file,
                    GFile *output_file)
{
  AutoarExtract *arextract;

  g_return_val_if_fail (source_file != NULL, NULL);
  g_return_val_if_fail (output_file != NULL, NULL);

  arextract = g_object_new (AUTOAR_TYPE_EXTRACT,
                            "source-file", source_file,
                            "output-file", output_file,
                            NULL);

  arextract->source_basename = g_file_get_basename (arextract->source_file);
  arextract->suggested_destname = autoar_common_get_basename_remove_extension (arextract->source_basename);

  return arextract;
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

  int r;

  g_debug ("autoar_extract_step_scan_toplevel: called");

  r = libarchive_create_read_object (FALSE, arextract, &a);
  if (r != ARCHIVE_OK) {
    archive_read_free (a);
    r = libarchive_create_read_object (TRUE, arextract, &a);
    if (r != ARCHIVE_OK) {
      if (arextract->error == NULL)
        arextract->error = autoar_common_g_error_new_a (a, arextract->source_basename);
      return;
    } else if (archive_filter_count (a) <= 1){
      /* If we only use raw format and filter count is one, libarchive will
       * not do anything except for just copying the source file. We do not
       * want this thing to happen because it does unnecesssary copying. */
      if (arextract->error == NULL)
        arextract->error = g_error_new (AUTOAR_EXTRACT_ERROR, NOT_AN_ARCHIVE_ERRNO,
                                        "\'%s\': %s", arextract->source_basename, "not an archive");
      return;
    }
    arextract->use_raw_format = TRUE;
  }

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;

    if (g_cancellable_is_cancelled (arextract->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    g_debug ("autoar_extract_step_scan_toplevel: %d: pathname = %s", arextract->files, pathname);

    arextract->files_list = g_list_prepend (arextract->files_list,
                                            g_file_get_child (arextract->output_file, pathname));
    arextract->files++;
    arextract->size += archive_entry_size (entry);
    archive_read_data_skip (a);
  }

  if (arextract->files_list == NULL) {
    if (arextract->error == NULL) {
      arextract->error = g_error_new (AUTOAR_EXTRACT_ERROR, EMPTY_ARCHIVE_ERRNO,
                                      "\'%s\': %s", arextract->source_basename, "empty archive");
    }
    archive_read_free (a);
    return;
  }

  if (r != ARCHIVE_EOF) {
    if (arextract->error == NULL) {
      arextract->error = autoar_common_g_error_new_a (a, arextract->source_basename);
    }
    archive_read_free (a);
    return;
  }

  /* If we are unable to determine the total size, set it to a positive
   * number to prevent strange percentage. */
  if (arextract->size <= 0)
    arextract->size = G_MAXUINT64;

  archive_read_free (a);

  g_debug ("autoar_extract_step_scan_toplevel: files = %d", arextract->files);

  arextract->files_list = g_list_reverse (arextract->files_list);

  arextract->prefix = autoar_extract_get_common_prefix (arextract->files_list, arextract->output_file);

  if (arextract->prefix != NULL) {
    g_autofree char *path_prefix;

    path_prefix = g_file_get_path (arextract->prefix);
    g_debug ("autoar_extract_step_scan_toplevel: pathname_prefix = %s", path_prefix);
  }

  autoar_extract_signal_scanned (arextract);
}

static void
autoar_extract_step_set_destination (AutoarExtract *arextract)
{
  /* Step 1: Set destination based on client preferences or archive contents */

  g_debug ("autoar_extract_step_set_destination: called");

  if (arextract->output_is_dest) {
    arextract->destination_dir = g_object_ref (arextract->output_file);
    return;
  }

  if (arextract->prefix != NULL) {
    /* We must check if the archive and the prefix have the same name (without
     * the extension). If they do, then the destination should be the output
     * directory itself.
     */
    g_autofree char *prefix_name;
    g_autofree char *prefix_name_no_ext;

    prefix_name = g_file_get_basename (arextract->prefix);
    prefix_name_no_ext = autoar_common_get_basename_remove_extension (prefix_name);

    if (g_strcmp0 (prefix_name_no_ext, arextract->suggested_destname) == 0) {
      arextract->destination_dir = g_object_ref (arextract->output_file);
    } else {
      g_clear_object (&arextract->prefix);
    }
  }
  /* If none of the above situations apply, the top level directory gets the
   * name suggested when creating the AutoarExtract object
   */
  if (arextract->destination_dir == NULL) {
    arextract->destination_dir = g_file_get_child (arextract->output_file,
                                              arextract->suggested_destname);
  }
}

static void
autoar_extract_step_decide_destination (AutoarExtract *arextract)
{
  /* Step 2: Decide destination */

  GList *files = NULL;
  GList *l;
  GFile *new_destination = NULL;
  g_autofree char *destination_name;

  for (l = arextract->files_list; l != NULL; l = l->next) {
    char *relative_path;
    GFile *file;

    relative_path = g_file_get_relative_path (arextract->output_file, l->data);
    file = g_file_resolve_relative_path (arextract->destination_dir,
                                         relative_path);
    files = g_list_prepend (files, file);

    g_free (relative_path);
  }

  files = g_list_reverse (files);

  /* When it exists, the common prefix is the actual output of the extraction
   * and the client has the opportunity to change it. Also, the old prefix is
   * needed in order to replace it with the new one
   */
  if (arextract->prefix != NULL) {
    autoar_extract_signal_decide_destination (arextract,
                                              arextract->prefix,
                                              files,
                                              &new_destination);

    arextract->new_prefix = new_destination;
  } else {
    autoar_extract_signal_decide_destination (arextract,
                                              arextract->destination_dir,
                                              files,
                                              &new_destination);

    if (new_destination) {
      g_object_unref (arextract->destination_dir);
      arextract->destination_dir = new_destination;
    }
  }

  destination_name = g_file_get_path (arextract->new_prefix != NULL ?
                                      arextract->new_prefix :
                                      arextract->destination_dir);
  g_debug ("autoar_extract_step_decide_destination: destination %s", destination_name);

  g_file_make_directory_with_parents (arextract->destination_dir, arextract->cancellable,
                                      &(arextract->error));

  if (g_error_matches (arextract->error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
    GFileType file_type;

    file_type = g_file_query_file_type (arextract->destination_dir,
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL);

    if (file_type == G_FILE_TYPE_DIRECTORY) {
      /* FIXME: Implement a way to solve directory conflicts */
      g_debug ("autoar_extract_step_decide_destination: destination directory exists");
      g_clear_error (&arextract->error);
    }
  }

  g_list_free_full (files, g_object_unref);
}

static void
autoar_extract_step_extract (AutoarExtract *arextract) {
  /* Step 3: Extract files
   * We have to re-open the archive to extract files
   */

  struct archive *a;
  struct archive_entry *entry;

  int r;

  g_debug ("autoar_extract_step_extract: called");

  r = libarchive_create_read_object (arextract->use_raw_format, arextract, &a);
  if (r != ARCHIVE_OK) {
    if (arextract->error == NULL) {
      arextract->error = autoar_common_g_error_new_a (a, arextract->source_basename);
    }
    archive_read_free (a);
    return;
  }

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;
    const char *hardlink;
    g_autoptr (GFile) extracted_filename = NULL;
    g_autoptr (GFile) hardlink_filename = NULL;
    AutoarConflictAction action;
    gboolean file_conflict;

    if (g_cancellable_is_cancelled (arextract->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    hardlink = archive_entry_hardlink (entry);

    extracted_filename =
      autoar_extract_do_sanitize_pathname (arextract, pathname);

    if (hardlink != NULL) {
      hardlink_filename =
        autoar_extract_do_sanitize_pathname (arextract, hardlink);
    }

    /* Attempt to solve any name conflict before doing any operations */
    file_conflict = autoar_extract_check_file_conflict (extracted_filename,
                                                        archive_entry_filetype (entry));
    while (file_conflict) {
      GFile *new_extracted_filename = NULL;

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

      if (action != AUTOAR_CONFLICT_CHANGE_DESTINATION) {
        break;
      }

      file_conflict = autoar_extract_check_file_conflict (extracted_filename,
                                                          archive_entry_filetype (entry));
    }

    if (file_conflict && action == AUTOAR_CONFLICT_SKIP) {
      continue;
    }

    autoar_extract_do_write_entry (arextract, a, entry,
                                   extracted_filename, hardlink_filename);

    if (arextract->error != NULL) {
      archive_read_free (a);
      return;
    }

    arextract->completed_files++;
    autoar_extract_signal_progress (arextract);
  }

  if (r != ARCHIVE_EOF) {
    if (arextract->error == NULL) {
      arextract->error = autoar_common_g_error_new_a (a, arextract->source_basename);
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
   * writing of files in the directory.
   */

  int i;

  g_debug ("autoar_extract_step_apply_dir_fileinfo: called");

  for (i = 0; i < arextract->extracted_dir_list->len; i++) {
    GFile *file = g_array_index (arextract->extracted_dir_list, GFileAndInfo, i).file;
    GFileInfo *info = g_array_index (arextract->extracted_dir_list, GFileAndInfo, i).info;
    g_file_set_attributes_from_info (file, info,
                                     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                     arextract->cancellable, NULL);
    if (g_cancellable_is_cancelled (arextract->cancellable)) {
      return;
    }
  }
}

static void
autoar_extract_step_cleanup (AutoarExtract *arextract) {
  /* Step 5: Force progress to be 100% and remove the source archive file
   * If the extraction is completed successfully, remove the source file.
   * Errors are not fatal because we have completed our work.
   */

  g_debug ("autoar_extract_step_cleanup: called");

  arextract->completed_size = arextract->size;
  arextract->completed_files = arextract->files;
  arextract->notify_last = 0;
  autoar_extract_signal_progress (arextract);
  g_debug ("autoar_extract_step_cleanup: Update progress");

  if (arextract->delete_after_extraction) {
    g_debug ("autoar_extract_step_cleanup: Delete");
    g_file_delete (arextract->source_file, arextract->cancellable, NULL);
  }
}

static void
autoar_extract_run (AutoarExtract *arextract)
{
  /* Numbers of steps.
   * The array size must be modified if more steps are added. */
  void (*steps[7])(AutoarExtract*);

  int i;

  g_return_if_fail (AUTOAR_IS_EXTRACT (arextract));

  g_return_if_fail (arextract->source_file != NULL);
  g_return_if_fail (arextract->output_file != NULL);

  if (g_cancellable_is_cancelled (arextract->cancellable)) {
    autoar_extract_signal_cancelled (arextract);
    return;
  }

  i = 0;
  steps[i++] = autoar_extract_step_scan_toplevel;
  steps[i++] = autoar_extract_step_set_destination;
  steps[i++] = autoar_extract_step_decide_destination;
  steps[i++] = autoar_extract_step_extract;
  steps[i++] = autoar_extract_step_apply_dir_fileinfo;
  steps[i++] = autoar_extract_step_cleanup;
  steps[i++] = NULL;

  for (i = 0; steps[i] != NULL; i++) {
    g_debug ("autoar_extract_run: Step %d Begin", i);
    (*steps[i])(arextract);
    g_debug ("autoar_extract_run: Step %d End", i);
    if (arextract->error != NULL) {
      autoar_extract_signal_error (arextract);
      return;
    }
    if (g_cancellable_is_cancelled (arextract->cancellable)) {
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
  arextract->cancellable = cancellable;
  arextract->in_thread = FALSE;
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
  arextract->cancellable = cancellable;
  arextract->in_thread = TRUE;

  task = g_task_new (arextract, NULL, NULL, NULL);
  g_task_set_task_data (task, NULL, NULL);
  g_task_run_in_thread (task, autoar_extract_start_async_thread);
}

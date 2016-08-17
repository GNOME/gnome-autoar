/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-compressor.c
 * Automatically create archives in some GNOME programs
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
#include "autoar-compressor.h"

#include "autoar-misc.h"
#include "autoar-private.h"
#include "autoar-format-filter.h"
#include "autoar-enum-types.h"

#include <archive.h>
#include <archive_entry.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * SECTION:autoar-compressor
 * @Short_description: Automatically compress files
 * @Title: AutoarCompressor
 * @Include: gnome-autoar/autoar.h
 *
 * The #AutoarCompressor object is used to automatically compress files and
 * directories into an archive. The new archive can contain a top-level directory.
 * Applying multiple filters is currently not supported because most
 * applications do not need this function. GIO is used for both read and write
 * operations. A few POSIX functions are also used to get more information from
 * files if GIO does not provide relevant functions.
 *
 * When #AutoarCompressor stop all work, it will emit one of the three signals:
 * #AutoarCompressor::cancelled, #AutoarCompressor::error, and
 * #AutoarCompressor::completed. After one of these signals is received, the
 * #AutoarCompressor object should be destroyed because it cannot be used to
 * start another archive operation. An #AutoarCompressor object can only be
 * used once and create one archive.
 **/

/**
 * autoar_compressor_quark:
 *
 * Gets the #AutoarCompressor Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (autoar-compressor, autoar_compressor)

#define BUFFER_SIZE (64 * 1024)
#define ARCHIVE_WRITE_RETRY_TIMES 5

#define INVALID_FORMAT 1
#define INVALID_FILTER 2

struct _AutoarCompressor
{
  GObject parent_instance;

  GList *source_files;
  GFile *output_file;
  AutoarFormat format;
  AutoarFilter filter;

  int output_is_dest : 1;

  gboolean started;

  guint64 size; /* This field is currently unused */
  guint64 completed_size;

  guint files;
  guint completed_files;

  gint64 notify_last;
  gint64 notify_interval;

  GOutputStream *ostream;
  void          *buffer;
  gssize         buffer_size;
  GError        *error;

  GCancellable *cancellable;

  struct archive                    *a;
  struct archive_entry              *entry;
  struct archive_entry_linkresolver *resolver;
  GFile                             *dest;
  GHashTable                        *pathname_to_g_file;
  char                              *source_basename_noext;
  char                              *extension;

  int in_thread        : 1;
  gboolean create_top_level_directory;
};

G_DEFINE_TYPE (AutoarCompressor, autoar_compressor, G_TYPE_OBJECT)

enum
{
  DECIDE_DEST,
  PROGRESS,
  CANCELLED,
  COMPLETED,
  AR_ERROR,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SOURCE_FILES,
  PROP_OUTPUT_FILE,
  PROP_FORMAT,
  PROP_FILTER,
  PROP_CREATE_TOP_LEVEL_DIRECTORY,
  PROP_SIZE, /* This property is currently unused */
  PROP_COMPLETED_SIZE,
  PROP_FILES,
  PROP_COMPLETED_FILES,
  PROP_OUTPUT_IS_DEST,
  PROP_NOTIFY_INTERVAL
};

static guint autoar_compressor_signals[LAST_SIGNAL] = { 0 };

static void
autoar_compressor_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  AutoarCompressor *compressor;

  compressor = AUTOAR_COMPRESSOR (object);

  switch (property_id) {
    case PROP_SOURCE_FILES:
      g_value_set_pointer (value, compressor->source_files);
      break;
    case PROP_OUTPUT_FILE:
      g_value_set_object (value, compressor->output_file);
      break;
    case PROP_FORMAT:
      g_value_set_enum (value, compressor->format);
      break;
    case PROP_FILTER:
      g_value_set_enum (value, compressor->format);
      break;
    case PROP_CREATE_TOP_LEVEL_DIRECTORY:
      g_value_set_boolean (value, compressor->create_top_level_directory);
      break;
    case PROP_SIZE:
      g_value_set_uint64 (value, compressor->size);
      break;
    case PROP_COMPLETED_SIZE:
      g_value_set_uint64 (value, compressor->completed_size);
      break;
    case PROP_FILES:
      g_value_set_uint (value, compressor->files);
      break;
    case PROP_COMPLETED_FILES:
      g_value_set_uint (value, compressor->completed_files);
      break;
    case PROP_OUTPUT_IS_DEST:
      g_value_set_boolean (value, compressor->output_is_dest);
      break;
    case PROP_NOTIFY_INTERVAL:
      g_value_set_int64 (value, compressor->notify_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
autoar_compressor_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  AutoarCompressor *compressor;

  compressor = AUTOAR_COMPRESSOR (object);

  switch (property_id) {
    case PROP_SOURCE_FILES:
      if (compressor->source_files != NULL)
        g_list_free_full (compressor->source_files, g_object_unref);
      compressor->source_files = g_list_copy_deep (g_value_get_pointer (value),
                                                   (GCopyFunc)g_object_ref,
                                                   NULL);
      break;
    case PROP_OUTPUT_FILE:
      autoar_common_g_object_unref (compressor->output_file);
      compressor->output_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_FORMAT:
      compressor->format = g_value_get_enum (value);
      break;
    case PROP_FILTER:
      compressor->filter = g_value_get_enum (value);
      break;
    case PROP_CREATE_TOP_LEVEL_DIRECTORY:
      compressor->create_top_level_directory = g_value_get_boolean (value);
      break;
    case PROP_OUTPUT_IS_DEST:
      compressor->output_is_dest = g_value_get_boolean (value);
      break;
    case PROP_NOTIFY_INTERVAL:
      compressor->notify_interval = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 * autoar_compressor_get_source_files:
 * @compressor: an #AutoarCompressor
 *
 * Gets the list of source files.
 *
 * Returns: (transfer none): a #GList with the source files
 **/
GList*
autoar_compressor_get_source_files (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), NULL);
  return compressor->source_files;
}

/**
 * autoar_compressor_get_output_file:
 * @compressor: an #AutoarCompressor
 *
 * If #AutoarCompressor:output_is_dest is %FALSE, gets the directory which
 * contains the new archive. Otherwise, gets the the new archive. See
 * autoar_compressor_set_output_is_dest().
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_compressor_get_output_file (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), NULL);
  return compressor->output_file;
}

/**
 * autoar_compressor_get_format:
 * @compressor: an #AutoarCompressor
 *
 * Gets the compression format
 *
 * Returns: the compression format
 **/
AutoarFormat
autoar_compressor_get_format (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), AUTOAR_FORMAT_0);
  return compressor->format;
}

/**
 * autoar_compressor_get_filter:
 * @compressor: an #AutoarCompressor
 *
 * Gets the compression filter
 *
 * Returns: the compression filter
 **/
AutoarFilter
autoar_compressor_get_filter (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), AUTOAR_FILTER_0);
  return compressor->filter;
}

/**
 * autoar_compressor_get_create_top_level_directory:
 * @compressor: an #AutoarCompressor
 *
 * Gets whether a top level directory will be created in the archive. See
 * autoar_compressor_set_create_top_level_directory() for more details.
 *
 * Returns: whether a top level directory will be created
 **/
gboolean
autoar_compressor_get_create_top_level_directory (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), FALSE);
  return compressor->create_top_level_directory;
}

/**
 * autoar_compressor_get_size:
 * @compressor: an #AutoarCompressor
 *
 * Gets the size in bytes will be read when the operation is completed. This
 * value is currently unset, so calling this function is useless.
 *
 * Returns: total file size in bytes
 **/
guint64
autoar_compressor_get_size (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->size;
}

/**
 * autoar_compressor_get_completed_size:
 * @compressor: an #AutoarCompressor
 *
 * Gets the size in bytes has been read from the source files and directories.
 *
 * Returns: file size in bytes has been read
 **/
guint64
autoar_compressor_get_completed_size (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->completed_size;
}

/**
 * autoar_compressor_get_files:
 * @compressor: an #AutoarCompressor
 *
 * Gets the number of files will be read when the operation is completed. This
 * value is currently unset, so calling this function is useless.
 *
 * Returns: total number of files
 **/
guint
autoar_compressor_get_files (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->files;
}

/**
 * autoar_compressor_get_completed_files:
 * @compressor: an #AutoarCompressor
 *
 * Gets the number of files has been read
 *
 * Returns: number of files has been read
 **/
guint
autoar_compressor_get_completed_files (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->completed_files;
}

/**
 * autoar_compressor_get_output_is_dest:
 * @compressor: an #AutoarCompressor
 *
 * See autoar_compressor_set_output_is_dest().
 *
 * Returns: %TRUE if #AutoarCompressor:output is the location of the new
 * archive.
 **/
gboolean
autoar_compressor_get_output_is_dest (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->output_is_dest;
}

/**
 * autoar_compressor_get_notify_interval:
 * @compressor: an #AutoarCompressor
 *
 * See autoar_compressor_set_notify_interval().
 *
 * Returns: the minimal interval in microseconds between the emission of the
 * #AutoarCompressor::progress signal.
 **/
gint64
autoar_compressor_get_notify_interval (AutoarCompressor *compressor)
{
  g_return_val_if_fail (AUTOAR_IS_COMPRESSOR (compressor), 0);
  return compressor->notify_interval;
}

/**
 * autoar_compressor_set_create_top_level_directory:
 * @compressor: an #AutoarCompressor
 * @create_top_level_directory: %TRUE if a top level directory should be
 * created in the new archive
 *
 * By default #AutoarCompressor:create-top-level-directory is set to %FALSE, so
 * the source files will be added directly to the archive's root. By setting
 * #AutoarCompressor:create-top-level-directory to %TRUE a top level directory
 * will be created and it will have the name of the archive without the
 * extension. Setting the property once the operation is started will have no
 * effect.
 **/
void
autoar_compressor_set_create_top_level_directory (AutoarCompressor *compressor,
                                                  gboolean          create_top_level_directory)
{
  g_return_if_fail (AUTOAR_IS_COMPRESSOR (compressor));
  g_return_if_fail (compressor->started);
  compressor->create_top_level_directory = create_top_level_directory;
}

/**
 * autoar_compressor_set_output_is_dest:
 * @compressor: an #AutoarCompressor
 * @output_is_dest: %TRUE if the location of the new archive has been already
 * decided
 *
 * By default #AutoarCompressor:output-is-dest is set to %FALSE, which means
 * the new archive will be created as a regular file under
 * #AutoarCompressor:output directory. The name of the new archive will be
 * automatically generated and you will be notified via
 * #AutoarCompressor::decide-dest when the name is decided. If you have already
 * decided the location of the new archive, and you do not want
 * #AutoarCompressor to decide it for you, you can set
 * #AutoarCompressor:output-is-dest to %TRUE. #AutoarCompressor will use
 * #AutoarCompressor:output as the location of the new archive, and it will
 * neither check whether the file exists nor create the necessary
 * directories for you. This function should only be called before calling
 * autoar_compressor_start() or autoar_compressor_start_async().
 **/
void
autoar_compressor_set_output_is_dest (AutoarCompressor *compressor,
                                      gboolean          output_is_dest)
{
  g_return_if_fail (AUTOAR_IS_COMPRESSOR (compressor));
  compressor->output_is_dest = output_is_dest;
}

/**
 * autoar_compressor_set_notify_interval:
 * @compressor: an #AutoarCompressor
 * @notify_interval: the minimal interval in microseconds
 *
 * Sets the minimal interval between emission of #AutoarCompressor::progress
 * signal. This prevent too frequent signal emission, which may cause
 * performance impact. If you do not want this feature, you can set the
 * interval to 0, so you will receive every progress update.
 **/
void
autoar_compressor_set_notify_interval (AutoarCompressor *compressor,
                                       gint64            notify_interval)
{
  g_return_if_fail (AUTOAR_IS_COMPRESSOR (compressor));
  g_return_if_fail (notify_interval >= 0);
  compressor->notify_interval = notify_interval;
}

static void
autoar_compressor_dispose (GObject *object)
{
  AutoarCompressor *compressor;

  compressor = AUTOAR_COMPRESSOR (object);

  g_debug ("AutoarCompressor: dispose");

  if (compressor->ostream != NULL) {
    if (!g_output_stream_is_closed (compressor->ostream)) {
      g_output_stream_close (compressor->ostream,
                             compressor->cancellable,
                             NULL);
    }
    g_object_unref (compressor->ostream);
    compressor->ostream = NULL;
  }

  g_clear_object (&(compressor->dest));
  g_clear_object (&(compressor->cancellable));
  g_clear_object (&(compressor->output_file));

  if (compressor->pathname_to_g_file != NULL) {
    g_hash_table_unref (compressor->pathname_to_g_file);
    compressor->pathname_to_g_file = NULL;
  }

  if (compressor->source_files != NULL) {
    g_list_free_full (compressor->source_files, g_object_unref);
    compressor->source_files = NULL;
  }

  G_OBJECT_CLASS (autoar_compressor_parent_class)->dispose (object);
}

static void
autoar_compressor_finalize (GObject *object)
{
  AutoarCompressor *compressor;

  compressor = AUTOAR_COMPRESSOR (object);

  g_debug ("AutoarCompressor: finalize");

  g_free (compressor->buffer);
  compressor->buffer = NULL;

  /* If compressor->error == NULL, no errors occurs. Therefore, we can safely
   * free libarchive objects because it will not call the callbacks during the
   * the process of freeing.
   * If compressor->error != NULL, we must free libarchive objects beforce
   * freeing compressor->error in order to prevent libarchive callbacks from
   * accessing freed private objects and buffers.
   */
  if (compressor->a != NULL) {
    archive_write_free (compressor->a);
    compressor->a = NULL;
  }

  if (compressor->entry != NULL) {
    archive_entry_free (compressor->entry);
    compressor->entry = NULL;
  }

  if (compressor->resolver != NULL) {
    archive_entry_linkresolver_free (compressor->resolver);
    compressor->resolver = NULL;
  }

  if (compressor->error != NULL) {
    g_error_free (compressor->error);
    compressor->error = NULL;
  }

  g_free (compressor->source_basename_noext);
  compressor->source_basename_noext = NULL;

  g_free (compressor->extension);
  compressor->extension = NULL;

  G_OBJECT_CLASS (autoar_compressor_parent_class)->finalize (object);
}

static int
libarchive_write_open_cb (struct archive *ar_write,
                          void           *client_data)
{
  AutoarCompressor *compressor;

  g_debug ("libarchive_write_open_cb: called");

  compressor = AUTOAR_COMPRESSOR (client_data);
  if (compressor->error != NULL) {
    return ARCHIVE_FATAL;
  }

  compressor->ostream = (GOutputStream*)g_file_create (compressor->dest,
                                                       G_FILE_CREATE_NONE,
                                                       compressor->cancellable,
                                                       &(compressor->error));
  if (compressor->error != NULL) {
    g_debug ("libarchive_write_open_cb: ARCHIVE_FATAL");
    return ARCHIVE_FATAL;
  }

  g_debug ("libarchive_write_open_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static int
libarchive_write_close_cb (struct archive *ar_write,
                           void           *client_data)
{
  AutoarCompressor *compressor;

  g_debug ("libarchive_write_close_cb: called");

  compressor = AUTOAR_COMPRESSOR (client_data);
  if (compressor->error != NULL) {
    return ARCHIVE_FATAL;
  }

  if (compressor->ostream != NULL) {
    g_output_stream_close (compressor->ostream,
                           compressor->cancellable, &(compressor->error));
    g_object_unref (compressor->ostream);
    compressor->ostream = NULL;
  }

  if (compressor->error != NULL) {
    g_debug ("libarchive_write_close_cb: ARCHIVE_FATAL");
    return ARCHIVE_FATAL;
  }

  g_debug ("libarchive_write_close_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static ssize_t
libarchive_write_write_cb (struct archive *ar_write,
                           void           *client_data,
                           const void     *buffer,
                           size_t          length)
{
  AutoarCompressor *compressor;
  gssize write_size;

  g_debug ("libarchive_write_write_cb: called");

  compressor = AUTOAR_COMPRESSOR (client_data);
  if (compressor->error != NULL || compressor->ostream == NULL) {
    return -1;
  }

  write_size = g_output_stream_write (compressor->ostream,
                                      buffer,
                                      length,
                                      compressor->cancellable,
                                      &(compressor->error));
  if (compressor->error != NULL)
    return -1;

  g_debug ("libarchive_write_write_cb: %" G_GSSIZE_FORMAT, write_size);
  return write_size;
}

static inline void
autoar_compressor_signal_decide_dest (AutoarCompressor *compressor)
{
  autoar_common_g_signal_emit (compressor, compressor->in_thread,
                               autoar_compressor_signals[DECIDE_DEST], 0,
                               compressor->dest);
}

static inline void
autoar_compressor_signal_progress (AutoarCompressor *compressor)
{
  gint64 mtime;
  mtime = g_get_monotonic_time ();
  if (mtime - compressor->notify_last >= compressor->notify_interval) {
    autoar_common_g_signal_emit (compressor, compressor->in_thread,
                                 autoar_compressor_signals[PROGRESS], 0,
                                 compressor->completed_size,
                                 compressor->completed_files);
    compressor->notify_last = mtime;
  }
}

static inline void
autoar_compressor_signal_cancelled (AutoarCompressor *compressor)
{
  autoar_common_g_signal_emit (compressor, compressor->in_thread,
                               autoar_compressor_signals[CANCELLED], 0);

}

static inline void
autoar_compressor_signal_completed (AutoarCompressor *compressor)
{
  autoar_common_g_signal_emit (compressor, compressor->in_thread,
                               autoar_compressor_signals[COMPLETED], 0);

}

static inline void
autoar_compressor_signal_error (AutoarCompressor *compressor)
{
  if (compressor->error != NULL) {
    if (compressor->error->domain == G_IO_ERROR &&
        compressor->error->code == G_IO_ERROR_CANCELLED) {
      g_error_free (compressor->error);
      compressor->error = NULL;
      autoar_compressor_signal_cancelled (compressor);
    } else {
      autoar_common_g_signal_emit (compressor, compressor->in_thread,
                                   autoar_compressor_signals[AR_ERROR], 0,
                                   compressor->error);
    }
  }
}

static void
autoar_compressor_do_write_data (AutoarCompressor     *compressor,
                                 struct archive_entry *entry,
                                 GFile                *file)
{
  int r;

  g_debug ("autoar_compressor_do_write_data: called");

  if (compressor->error != NULL)
    return;

  if (g_cancellable_is_cancelled (compressor->cancellable))
    return;

  while ((r = archive_write_header (compressor->a, entry)) == ARCHIVE_RETRY);
  if (r == ARCHIVE_FATAL) {
    if (compressor->error == NULL)
      compressor->error =
        autoar_common_g_error_new_a_entry (compressor->a, entry);
    return;
  }

  g_debug ("autoar_compressor_do_write_data: write header OK");

  /* Non-regular files have no content to write */
  if (archive_entry_size (entry) > 0 && archive_entry_filetype (entry) == AE_IFREG) {
    GInputStream *istream;
    ssize_t read_actual, written_actual, written_acc;
    int written_try;

    g_debug ("autoar_compressor_do_write_data: entry size is %"G_GUINT64_FORMAT,
             archive_entry_size (entry));

    written_actual = 0;
    written_try = 0;

    istream = (GInputStream*)g_file_read (file,
                                          compressor->cancellable,
                                          &(compressor->error));
    if (istream == NULL)
      return;

    compressor->completed_files++;

    do {
      read_actual = g_input_stream_read (istream,
                                         compressor->buffer,
                                         compressor->buffer_size,
                                         compressor->cancellable,
                                         &(compressor->error));
      compressor->completed_size += read_actual > 0 ? read_actual : 0;
      autoar_compressor_signal_progress (compressor);
      if (read_actual > 0) {
        written_acc = 0;
        written_try = 0;
        do {
          written_actual =
            archive_write_data (compressor->a,
                                (const char*)(compressor->buffer) + written_acc,
                                read_actual);
          written_acc += written_actual > 0 ? written_actual : 0;
          written_try = written_actual ? 0 : written_try + 1;
          /* archive_write_data may return zero, so we have to limit the
           * retry times to prevent infinite loop */
        } while (written_acc < read_actual && written_actual >= 0 && written_try < ARCHIVE_WRITE_RETRY_TIMES);
      }
    } while (read_actual > 0 && written_actual >= 0);


    g_input_stream_close (istream, compressor->cancellable, NULL);
    g_object_unref (istream);

    if (read_actual < 0)
      return;

    if (written_actual < 0 || written_try >= ARCHIVE_WRITE_RETRY_TIMES) {
      if (compressor->error == NULL)
        compressor->error =
          autoar_common_g_error_new_a_entry (compressor->a, entry);
      return;
    }
    g_debug ("autoar_compressor_do_write_data: write data OK");
  } else {
    g_debug ("autoar_compressor_do_write_data: no data, return now!");
    compressor->completed_files++;
    autoar_compressor_signal_progress (compressor);
  }
}

static void
autoar_compressor_do_add_to_archive (AutoarCompressor *compressor,
                                     GFile *root,
                                     GFile *file)
{
  GFileInfo *info;
  GFileType  filetype;

  if (compressor->error != NULL)
    return;

  if (g_cancellable_is_cancelled (compressor->cancellable))
    return;

  archive_entry_clear (compressor->entry);
  info = g_file_query_info (file, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                            compressor->cancellable, &(compressor->error));
  if (info == NULL)
    return;

  filetype = g_file_info_get_file_type (info);
  switch (archive_format (compressor->a)) {
    case ARCHIVE_FORMAT_AR:
    case ARCHIVE_FORMAT_AR_GNU:
    case ARCHIVE_FORMAT_AR_BSD:
      if (filetype == G_FILE_TYPE_DIRECTORY ||
          filetype == G_FILE_TYPE_SYMBOLIC_LINK ||
          filetype == G_FILE_TYPE_SPECIAL) {
        /* ar only support regular files, so we abort this operation to
         * prevent producing a malformed archive. */
        g_object_unref (info);
        return;
      }
      break;

    case ARCHIVE_FORMAT_ZIP:
      if (filetype == G_FILE_TYPE_SPECIAL) {
        /* Add special files to zip archives cause unknown fatal error
         * in libarchive. */
        g_object_unref (info);
        return;
      }
      break;
  }

  {
    char *root_basename;
    char *pathname_relative;
    char *pathname;

    switch (archive_format (compressor->a)) {
      /* ar format does not support directories */
      case ARCHIVE_FORMAT_AR:
      case ARCHIVE_FORMAT_AR_GNU:
      case ARCHIVE_FORMAT_AR_BSD:
        pathname = g_file_get_basename (file);
        archive_entry_set_pathname (compressor->entry, pathname);
        g_free (pathname);
        break;

      default:
        root_basename = g_file_get_basename (root);
        pathname_relative = g_file_get_relative_path (root, file);
        pathname =
          g_strconcat (compressor->create_top_level_directory ?
                       compressor->source_basename_noext : "",
                       compressor->create_top_level_directory ? "/" : "",
                       root_basename,
                       pathname_relative != NULL ? "/" : "",
                       pathname_relative != NULL ? pathname_relative : "",
                       NULL);
        archive_entry_set_pathname (compressor->entry, pathname);
        g_free (root_basename);
        g_free (pathname_relative);
        g_free (pathname);
    }
  }

  g_debug ("autoar_compressor_do_add_to_archive: %s",
           archive_entry_pathname (compressor->entry));

  {
    time_t atime, btime, ctime, mtime;
    long atimeu, btimeu, ctimeu, mtimeu;

    atime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_ACCESS);
    btime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CREATED);
    ctime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_CHANGED);
    mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

    atimeu = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_ACCESS_USEC);
    btimeu = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_CREATED_USEC);
    ctimeu = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_CHANGED_USEC);
    mtimeu = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);

    archive_entry_set_atime (compressor->entry, atime, atimeu * 1000);
    archive_entry_set_birthtime (compressor->entry, btime, btimeu * 1000);
    archive_entry_set_ctime (compressor->entry, ctime, ctimeu * 1000);
    archive_entry_set_mtime (compressor->entry, mtime, mtimeu * 1000);

    archive_entry_set_uid (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID));
    archive_entry_set_gid (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_GID));
    archive_entry_set_uname (compressor->entry, g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_OWNER_USER));
    archive_entry_set_gname (compressor->entry, g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_OWNER_GROUP));
    archive_entry_set_mode (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE));
  }

  archive_entry_set_size (compressor->entry, g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE));
  archive_entry_set_dev (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_DEVICE));
  archive_entry_set_ino64 (compressor->entry, g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_UNIX_INODE));
  archive_entry_set_nlink (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_NLINK));
  archive_entry_set_rdev (compressor->entry, g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_RDEV));

  switch (filetype) {
    case G_FILE_TYPE_DIRECTORY:
      g_debug ("autoar_compressor_do_add_to_archive: file type set to DIR");
      archive_entry_set_filetype (compressor->entry, AE_IFDIR);
      break;

    case G_FILE_TYPE_SYMBOLIC_LINK:
      g_debug ("autoar_compressor_do_add_to_archive: file type set to SYMLINK");
      archive_entry_set_filetype (compressor->entry, AE_IFLNK);
      archive_entry_set_symlink (compressor->entry,
                                 g_file_info_get_attribute_byte_string (info,
                                                                        G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET));
      break;

    case G_FILE_TYPE_SPECIAL:
#if (defined HAVE_STAT) && \
    (defined S_ISBLK) && (defined S_ISSOCK) && \
    (defined S_ISCHR) && (defined S_ISFIFO)
      {
        struct stat filestat;
        char *local_pathname;

        local_pathname = g_file_get_path (file);
        if (local_pathname != NULL && stat (local_pathname, &filestat) >= 0) {
          if (S_ISBLK (filestat.st_mode)) {
            g_debug ("autoar_compressor_do_add_to_archive: file type set to BLOCK");
            archive_entry_set_filetype (compressor->entry, AE_IFBLK);
          } else if (S_ISSOCK (filestat.st_mode)) {
            g_debug ("autoar_compressor_do_add_to_archive: file type set to SOCKET");
            archive_entry_set_filetype (compressor->entry, AE_IFSOCK);
          } else if (S_ISCHR (filestat.st_mode)) {
            g_debug ("autoar_compressor_do_add_to_archive: file type set to CHAR");
            archive_entry_set_filetype (compressor->entry, AE_IFCHR);
          } else if (S_ISFIFO (filestat.st_mode)) {
            g_debug ("autoar_compressor_do_add_to_archive: file type set to FIFO");
            archive_entry_set_filetype (compressor->entry, AE_IFIFO);
          } else {
            g_debug ("autoar_compressor_do_add_to_archive: file type set to REGULAR");
            archive_entry_set_filetype (compressor->entry, AE_IFREG);
          }
          g_free (local_pathname);
        } else {
          g_debug ("autoar_compressor_do_add_to_archive: file type set to REGULAR");
          archive_entry_set_filetype (compressor->entry, AE_IFREG);
        }
      }
      break;

#endif
    case G_FILE_TYPE_UNKNOWN:
    case G_FILE_TYPE_SHORTCUT:
    case G_FILE_TYPE_MOUNTABLE:
    case G_FILE_TYPE_REGULAR:
    default:
      g_debug ("autoar_compressor_do_add_to_archive: file type set to REGULAR");
      archive_entry_set_filetype (compressor->entry, AE_IFREG);
      break;
  }

  g_hash_table_insert (compressor->pathname_to_g_file,
                       g_strdup (archive_entry_pathname (compressor->entry)),
                       g_object_ref (file));

  {
    struct archive_entry *entry, *sparse;

    entry = compressor->entry;
    archive_entry_linkify (compressor->resolver, &entry, &sparse);

    if (entry != NULL) {
      GFile *file_to_read;
      const char *pathname_in_entry;
      pathname_in_entry = archive_entry_pathname (entry);
      file_to_read = g_hash_table_lookup (compressor->pathname_to_g_file,
                                          pathname_in_entry);
      autoar_compressor_do_write_data (compressor, entry, file_to_read);
      /* Entries for non-regular files might have their size attribute
       * different to their actual size on the disk
       */
      if (archive_entry_filetype (entry) != AE_IFREG &&
          archive_entry_size (entry) != g_file_info_get_size (info)) {
        compressor->completed_size += g_file_info_get_size (info);
        autoar_compressor_signal_progress (compressor);
      }

      g_hash_table_remove (compressor->pathname_to_g_file, pathname_in_entry);
      /* We have registered g_object_unref function to free the GFile object,
       * so we do not have to unref it here. */
    }

    if (sparse != NULL) {
      GFile *file_to_read;
      const char *pathname_in_entry;
      pathname_in_entry = archive_entry_pathname (entry);
      file_to_read = g_hash_table_lookup (compressor->pathname_to_g_file,
                                          pathname_in_entry);
      autoar_compressor_do_write_data (compressor, sparse, file_to_read);
      g_hash_table_remove (compressor->pathname_to_g_file, pathname_in_entry);
    }
  }

  g_object_unref (info);
};

static void
autoar_compressor_do_recursive_read (AutoarCompressor *compressor,
                                     GFile *root,
                                     GFile *file)
{
  GFileEnumerator *enumerator;
  GFileInfo *info;
  GFile *thisfile;
  const char *thisname;

  enumerator = g_file_enumerate_children (file,
                                          "standard::*",
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          compressor->cancellable,
                                          &(compressor->error));
  if (enumerator == NULL)
    return;

  while ((info = g_file_enumerator_next_file (enumerator,
                                              compressor->cancellable,
                                              &(compressor->error))) != NULL) {
    thisname = g_file_info_get_name (info);
    thisfile = g_file_get_child (file, thisname);
    autoar_compressor_do_add_to_archive (compressor, root, thisfile);
    if (compressor->error != NULL) {
      g_object_unref (thisfile);
      g_object_unref (info);
      break;
    }

    if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
      autoar_compressor_do_recursive_read (compressor, root, thisfile);
    g_object_unref (thisfile);
    g_object_unref (info);

    if (compressor->error != NULL)
      break;
    if (g_cancellable_is_cancelled (compressor->cancellable))
      break;
  }

  g_object_unref (enumerator);
}

static void
autoar_compressor_class_init (AutoarCompressorClass *klass)
{
  GObjectClass *object_class;
  GType type;

  object_class = G_OBJECT_CLASS (klass);
  type = G_TYPE_FROM_CLASS (klass);

  object_class->get_property = autoar_compressor_get_property;
  object_class->set_property = autoar_compressor_set_property;
  object_class->dispose = autoar_compressor_dispose;
  object_class->finalize = autoar_compressor_finalize;

  g_object_class_install_property (object_class, PROP_SOURCE_FILES,
                                   g_param_spec_pointer ("source-files",
                                                         "Source files list",
                                                         "The list of GFiles to be archived",
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OUTPUT_FILE,
                                   g_param_spec_object ("output-file",
                                                        "Output directory GFile",
                                                        "Output directory (GFile) of created archive",
                                                        G_TYPE_FILE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FORMAT,
                                   g_param_spec_enum ("format",
                                                      "Compression format",
                                                      "The compression format that will be used",
                                                      AUTOAR_TYPE_FORMAT,
                                                      AUTOAR_FORMAT_ZIP,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILTER,
                                   g_param_spec_enum ("filter",
                                                      "Compression filter",
                                                      "The compression filter that will be used",
                                                      AUTOAR_TYPE_FILTER,
                                                      AUTOAR_FILTER_NONE,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CREATE_TOP_LEVEL_DIRECTORY,
                                   g_param_spec_boolean ("create-top-level-directory",
                                                         "Create top level directory",
                                                         "Whether to create a top level directory",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT |
                                                         G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (object_class, PROP_SIZE, /* This propery is unused! */
                                   g_param_spec_uint64 ("size",
                                                        "Size",
                                                        "Total bytes will be read from disk",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_SIZE,
                                   g_param_spec_uint64 ("completed-size",
                                                        "Read file size",
                                                        "Bytes has read from disk",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_FILES,
                                   g_param_spec_uint ("files",
                                                      "Files",
                                                      "Number of files will be compressed",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_COMPLETED_FILES,
                                   g_param_spec_uint ("completed-files",
                                                      "Read files",
                                                      "Number of files has been read",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_OUTPUT_IS_DEST,
                                   g_param_spec_boolean ("output-is-dest",
                                                         "Output is destination",
                                                         "Whether output file is used as destination",
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
 * AutoarCompressor::decide-dest:
 * @compressor: the #AutoarCompressor
 * @destination: the location of the new archive
 *
 * This signal is emitted when the location of the new archive is determined.
 **/
  autoar_compressor_signals[DECIDE_DEST] =
    g_signal_new ("decide-dest",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_FILE);

/**
 * AutoarCompressor::progress:
 * @compressor: the #AutoarCompressor
 * @completed_size: bytes has been read from source files and directories
 * @completed_files: number of files and directories has been read
 *
 * This signal is used to report progress of creating archives. The value of
 * @completed_size and @completed_files are the same as the
 * #AutoarCompressor:completed_size and #AutoarCompressor:completed_files properties,
 * respectively.
 **/
  autoar_compressor_signals[PROGRESS] =
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
 * AutoarCompressor::cancelled:
 * @compressor: the #AutoarCompressor
 *
 * This signal is emitted after archive creating job is cancelled by the
 * #GCancellable.
 **/
  autoar_compressor_signals[CANCELLED] =
    g_signal_new ("cancelled",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarCompressor::completed:
 * @compressor: the #AutoarCompressor
 *
 * This signal is emitted after the archive creating job is successfully
 * completed.
 **/
  autoar_compressor_signals[COMPLETED] =
    g_signal_new ("completed",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarCompressor::error:
 * @compressor: the #AutoarCompressor
 * @error: the #GError
 *
 * This signal is emitted when error occurs and all jobs should be terminated.
 * Possible error domains are %AUTOAR_COMPRESSOR_ERROR, %G_IO_ERROR, and
 * %AUTOAR_LIBARCHIVE_ERROR, which represent error occurs in #AutoarCompressor,
 * GIO, and libarchive, respectively. The #GError is owned by #AutoarCompressor
 * and should not be freed.
 **/
  autoar_compressor_signals[AR_ERROR] =
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
autoar_compressor_init (AutoarCompressor *compressor)
{
  compressor->size = 0;
  compressor->completed_size = 0;
  compressor->files = 0;
  compressor->completed_files = 0;

  compressor->notify_last = 0;

  compressor->ostream = NULL;
  compressor->buffer_size = BUFFER_SIZE;
  compressor->buffer = g_new (char, compressor->buffer_size);
  compressor->error = NULL;

  compressor->cancellable = NULL;

  compressor->a = archive_write_new ();
  compressor->entry = archive_entry_new ();
  compressor->resolver = archive_entry_linkresolver_new ();
  compressor->pathname_to_g_file = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          g_object_unref);
  compressor->source_basename_noext = NULL;
  compressor->extension = NULL;

  compressor->in_thread = FALSE;
}

/**
 * autoar_compressor_new:
 * @source_files: a #GList of source #GFiles to be archived
 * @output_file: output directory of the new archive, or the file name of the
 * new archive if you set #AutoarCompressor:output-is-dest on the returned object
 * @format: the compression format
 * @filter: the compression filter
 *
 * Create a new #AutoarCompressor object.
 *
 * Returns: (transfer full): a new #AutoarCompressor object
 **/
AutoarCompressor*
autoar_compressor_new (GList        *source_files,
                       GFile        *output_file,
                       AutoarFormat  format,
                       AutoarFilter  filter)
{
  AutoarCompressor *compressor;

  compressor =
    g_object_new (AUTOAR_TYPE_COMPRESSOR,
                  "source-files", g_list_copy_deep (source_files,
                                                    (GCopyFunc)g_object_ref,
                                                    NULL),
                  "output-file", g_object_ref (output_file),
                  "format", format,
                  "filter", filter,
                  NULL);

  return compressor;
}

static void
autoar_compressor_step_initialize_object (AutoarCompressor *compressor)
{
  /* Step 0: Setup the libarchive object and the file name extension */

  AutoarFormatFunc format_func;
  AutoarFilterFunc filter_func;

  int r;

  if (!autoar_format_is_valid (compressor->format)) {
    compressor->error = g_error_new (AUTOAR_COMPRESSOR_ERROR, INVALID_FORMAT,
                               "Format %d is invalid", compressor->format);
    return;
  }

  if (!autoar_filter_is_valid (compressor->filter)) {
    compressor->error = g_error_new (AUTOAR_COMPRESSOR_ERROR, INVALID_FILTER,
                               "Filter %d is invalid", compressor->filter);
    return;
  }

  compressor->extension = autoar_format_filter_get_extension (compressor->format,
                                                        compressor->filter);

  r = archive_write_set_bytes_in_last_block (compressor->a, 1);
  if (r != ARCHIVE_OK) {
    compressor->error = autoar_common_g_error_new_a (compressor->a, NULL);
    return;
  }

  format_func = autoar_format_get_libarchive_write (compressor->format);
  r = (*format_func)(compressor->a);
  if (r != ARCHIVE_OK) {
    compressor->error = autoar_common_g_error_new_a (compressor->a, NULL);
    return;
  }

  filter_func = autoar_filter_get_libarchive_write (compressor->filter);
  r = (*filter_func)(compressor->a);
  if (r != ARCHIVE_OK) {
    compressor->error = autoar_common_g_error_new_a (compressor->a, NULL);
    return;
  }
}

static void
autoar_compressor_step_decide_dest (AutoarCompressor *compressor)
{
  /* Step 1: Set the destination file name
   * Use the first source file name */

  g_debug ("autoar_compressor_step_decide_dest: called");

  {
    GFile *file_source; /* Do not unref */
    GFileInfo *source_info;
    char *source_basename;

    file_source = compressor->source_files->data;
    source_info = g_file_query_info (file_source,
                                     G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                     compressor->cancellable,
                                     &(compressor->error));
    if (source_info == NULL)
      return;

    source_basename = g_file_get_basename (file_source);
    if (g_file_info_get_file_type (source_info) == G_FILE_TYPE_REGULAR)
      compressor->source_basename_noext =
        autoar_common_get_basename_remove_extension (source_basename);
    else
      compressor->source_basename_noext = g_strdup (source_basename);

    g_object_unref (source_info);
    g_free (source_basename);
  }

  {
    char *dest_basename;
    int i;

    dest_basename = g_strconcat (compressor->source_basename_noext,
                                 compressor->extension, NULL);
    compressor->dest = g_file_get_child (compressor->output_file, dest_basename);

    for (i = 1;
         g_file_query_exists (compressor->dest, compressor->cancellable);
         i++) {
      g_free (dest_basename);
      g_object_unref (compressor->dest);

      if (g_cancellable_is_cancelled (compressor->cancellable))
        return;

      dest_basename = g_strdup_printf ("%s(%d)%s",
                                       compressor->source_basename_noext,
                                       i, compressor->extension);
      compressor->dest = g_file_get_child (compressor->output_file,
                                           dest_basename);
    }

    g_free (dest_basename);
  }

  if (!g_file_query_exists (compressor->output_file, compressor->cancellable)) {
    g_file_make_directory_with_parents (compressor->output_file,
                                        compressor->cancellable,
                                        &(compressor->error));
    if (compressor->error != NULL)
      return;
  }

  autoar_compressor_signal_decide_dest (compressor);
}

static void
autoar_compressor_step_decide_dest_already (AutoarCompressor *compressor)
{
  /* Alternative step 1: Output is destination */

  char *output_basename;
  compressor->dest = g_object_ref (compressor->output_file);
  output_basename = g_file_get_basename (compressor->output_file);
  compressor->source_basename_noext =
    autoar_common_get_basename_remove_extension (output_basename);
  g_free (output_basename);

  autoar_compressor_signal_decide_dest (compressor);
}

static void
autoar_compressor_step_create (AutoarCompressor *compressor)
{
  /* Step 2: Create and open the new archive file */
  GList *l;
  int r;

  g_debug ("autoar_compressor_step_create: called");

  r = archive_write_open (compressor->a, compressor,
                          libarchive_write_open_cb,
                          libarchive_write_write_cb,
                          libarchive_write_close_cb);
  if (r != ARCHIVE_OK) {
    if (compressor->error == NULL)
      compressor->error = autoar_common_g_error_new_a (compressor->a, NULL);
    return;
  }

  archive_entry_linkresolver_set_strategy (compressor->resolver,
                                           archive_format (compressor->a));

  for (l = compressor->source_files; l != NULL; l = l->next) {
    GFile *file; /* Do not unref */
    GFileType filetype;
    GFileInfo *fileinfo;
    g_autofree gchar *pathname;

    file = l->data;

    pathname = g_file_get_path (file);
    g_debug ("autoar_compressor_step_create: %s", pathname);

    fileinfo = g_file_query_info (file, 
                                  G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                  compressor->cancellable,
                                  &(compressor->error));
    if (compressor->error != NULL)
      return;

    filetype = g_file_info_get_file_type (fileinfo);
    g_object_unref (fileinfo);

    autoar_compressor_do_add_to_archive (compressor, file, file);

    if (filetype == G_FILE_TYPE_DIRECTORY)
      autoar_compressor_do_recursive_read (compressor, file, file);

    if (compressor->error != NULL)
      return;

    if (g_cancellable_is_cancelled (compressor->cancellable))
      return;
  }

  /* Process the final entry */
  {
    struct archive_entry *entry, *sparse;
    entry = NULL;
    archive_entry_linkify (compressor->resolver, &entry, &sparse);
    if (entry != NULL) {
      GFile *file_to_read;
      const char *pathname_in_entry;
      pathname_in_entry = archive_entry_pathname (entry);
      file_to_read = g_hash_table_lookup (compressor->pathname_to_g_file,
                                          pathname_in_entry);
      autoar_compressor_do_write_data (compressor, entry, file_to_read);
      /* I think we do not have to remove the entry in the hash table now
       * because we are going to free the entire hash table. */
    }
  }
}

static void
autoar_compressor_step_cleanup (AutoarCompressor *compressor)
{
  /* Step 3: Close the libarchive object and force progress to be updated.
   * We do not have to do other cleanup because they are handled in dispose
   * and finalize functions. */
  compressor->notify_last = 0;
  autoar_compressor_signal_progress (compressor);
  if (archive_write_close (compressor->a) != ARCHIVE_OK) {
    g_autofree gchar *output_name;

    output_name = autoar_common_g_file_get_name (compressor->output_file);

    if (compressor->error == NULL)
      compressor->error =
        autoar_common_g_error_new_a (compressor->a, output_name);
    return;
  }
}

static void
autoar_compressor_run (AutoarCompressor *compressor)
{
  /* Numbers of steps.
   * The array size must be modified if more steps are added. */
  void (*steps[5])(AutoarCompressor*);

  int i;

  g_return_if_fail (AUTOAR_IS_COMPRESSOR (compressor));

  compressor->started = TRUE;

  g_return_if_fail (compressor->source_files != NULL);
  g_return_if_fail (compressor->output_file != NULL);

  /* A GFile* list without a GFile* is not allowed */
  g_return_if_fail (compressor->source_files->data != NULL);

  if (g_cancellable_is_cancelled (compressor->cancellable)) {
    autoar_compressor_signal_cancelled (compressor);
    return;
  }

  i = 0;
  steps[i++] = autoar_compressor_step_initialize_object;
  steps[i++] = compressor->output_is_dest ?
               autoar_compressor_step_decide_dest_already :
               autoar_compressor_step_decide_dest;
  steps[i++] = autoar_compressor_step_create;
  steps[i++] = autoar_compressor_step_cleanup;
  steps[i++] = NULL;

  for (i = 0; steps[i] != NULL; i++) {
    g_debug ("autoar_compressor_run: Step %d Begin", i);
    (*steps[i])(compressor);
    g_debug ("autoar_compressor_run: Step %d End", i);
    if (compressor->error != NULL) {
      autoar_compressor_signal_error (compressor);
      return;
    }
    if (g_cancellable_is_cancelled (compressor->cancellable)) {
      autoar_compressor_signal_cancelled (compressor);
      return;
    }
  }

  autoar_compressor_signal_completed (compressor);
}

/**
 * autoar_compressor_start:
 * @compressor: an #AutoarCompressor object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Runs the archive creating work. All callbacks will be called in the same
 * thread as the caller of this functions.
 **/
void
autoar_compressor_start (AutoarCompressor *compressor,
                         GCancellable     *cancellable)
{
  if (cancellable != NULL)
    g_object_ref (cancellable);
  compressor->cancellable = cancellable;
  compressor->in_thread = FALSE;
  autoar_compressor_run (compressor);
}

static void
autoar_compressor_start_async_thread (GTask        *task,
                                      gpointer      source_object,
                                      gpointer      task_data,
                                      GCancellable *cancellable)
{
  AutoarCompressor *compressor = source_object;
  autoar_compressor_run (compressor);
  g_task_return_pointer (task, NULL, g_free);
  g_object_unref (compressor);
  g_object_unref (task);
}

/**
 * autoar_compressor_start_async:
 * @compressor: an #AutoarCompressor object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Asynchronously runs the archive creating work. You should connect to
 * #AutoarCompressor::cancelled, #AutoarCompressor::error, and
 * #AutoarCompressor::completed signal to get notification when the work is
 * terminated. All callbacks will be called in the main thread, so you can
 * safely manipulate GTK+ widgets in the callbacks.
 **/
void
autoar_compressor_start_async (AutoarCompressor *compressor,
                               GCancellable     *cancellable)
{
  GTask *task;

  g_object_ref (compressor);
  if (cancellable != NULL)
    g_object_ref (cancellable);
  compressor->cancellable = cancellable;
  compressor->in_thread = TRUE;

  task = g_task_new (compressor, NULL, NULL, NULL);
  g_task_set_task_data (task, NULL, NULL);
  g_task_run_in_thread (task, autoar_compressor_start_async_thread);
}

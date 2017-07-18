/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-extractor.c
 * Automatically extract archives in some GNOME programs
 *
 * Copyright (C) 2013  Ting-Wei Lan
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "autoar-extractor.h"

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
 * SECTION:autoar-extractor
 * @Short_description: Automatically extract an archive
 * @Title: AutoarExtractor
 * @Include: gnome-autoar/autoar.h
 *
 * The #AutoarExtractor object is used to automatically extract files and
 * directories from an archive. By default, it will only create one file or
 * directory in the output directory. This is done to avoid clutter on the
 * user's output directory. If the archive contains only one file, the file
 * will be extracted to the output directory. If the archive has more than one
 * file, the files will be extracted in a directory having the same name as the
 * archive, except the extension. It is also possible to just extract all files
 * to the output directory (note that this will not perform any checks) by
 * using autoar_extractor_set_output_is_dest().

 * #AutoarExtractor will not attempt to solve any name conflicts. If the
 * destination directory already exists, it will proceed normally. If the
 * destionation directory cannot be created, it will fail with an error.
 * It is possible however to change the destination, when
 * #AutoarExtractor::decide-destination is emitted. The signal provides the decided
 * destination and the list of files to be extracted. The signal also allows a
 * new output destination to be used instead of the one provided by
 * #AutoarExtractor. This is convenient for solving name conflicts and
 * implementing specific logic based on the contents of the archive.
 *
 * When #AutoarExtractor stops all work, it will emit one of the three signals:
 * #AutoarExtractor::cancelled, #AutoarExtractor::error, and
 * #AutoarExtractor::completed. After one of these signals is received,
 * the #AutoarExtractor object should be destroyed because it cannot be used to
 * start another archive operation. An #AutoarExtractor object can only be used
 * once and extract one archive.
 **/

/**
 * autoar_extractor_quark:
 *
 * Gets the #AutoarExtractor Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (autoar-extractor, autoar_extractor)

#define BUFFER_SIZE (64 * 1024)
#define NOT_AN_ARCHIVE_ERRNO 2013
#define EMPTY_ARCHIVE_ERRNO 2014

typedef struct _GFileAndInfo GFileAndInfo;

struct _AutoarExtractor
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
  guint64 total_size;
  guint64 completed_size;

  guint total_files;
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

G_DEFINE_TYPE (AutoarExtractor, autoar_extractor, G_TYPE_OBJECT)

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
  PROP_TOTAL_SIZE,
  PROP_COMPLETED_SIZE,
  PROP_TOTAL_FILES,
  PROP_COMPLETED_FILES,
  PROP_OUTPUT_IS_DEST,
  PROP_DELETE_AFTER_EXTRACTION,
  PROP_NOTIFY_INTERVAL
};

static guint autoar_extractor_signals[LAST_SIGNAL] = { 0 };

static void
autoar_extractor_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  AutoarExtractor *self;

  self = AUTOAR_EXTRACTOR (object);

  switch (property_id) {
    case PROP_SOURCE_FILE:
      g_value_set_object (value, self->source_file);
      break;
    case PROP_OUTPUT_FILE:
      g_value_set_object (value, self->output_file);
      break;
    case PROP_TOTAL_SIZE:
      g_value_set_uint64 (value, self->total_size);
      break;
    case PROP_COMPLETED_SIZE:
      g_value_set_uint64 (value, self->completed_size);
      break;
    case PROP_TOTAL_FILES:
      g_value_set_uint (value, self->total_files);
      break;
    case PROP_COMPLETED_FILES:
      g_value_set_uint (value, self->completed_files);
      break;
    case PROP_OUTPUT_IS_DEST:
      g_value_set_boolean (value, self->output_is_dest);
      break;
    case PROP_DELETE_AFTER_EXTRACTION:
      g_value_set_boolean (value, self->delete_after_extraction);
      break;
    case PROP_NOTIFY_INTERVAL:
      g_value_set_int64 (value, self->notify_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
autoar_extractor_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  AutoarExtractor *self;

  self = AUTOAR_EXTRACTOR (object);

  switch (property_id) {
    case PROP_SOURCE_FILE:
      g_clear_object (&(self->source_file));
      self->source_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_OUTPUT_FILE:
      g_clear_object (&(self->output_file));
      self->output_file = g_object_ref (g_value_get_object (value));
      break;
    case PROP_OUTPUT_IS_DEST:
      autoar_extractor_set_output_is_dest (self,
                                           g_value_get_boolean (value));
      break;
    case PROP_DELETE_AFTER_EXTRACTION:
      autoar_extractor_set_delete_after_extraction (self,
                                                    g_value_get_boolean (value));
      break;
    case PROP_NOTIFY_INTERVAL:
      autoar_extractor_set_notify_interval (self,
                                            g_value_get_int64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/**
 * autoar_extractor_get_source_file:
 * @self: an #AutoarExtractor
 *
 * Gets the #GFile object which represents the source archive that will be
 * extracted for this object.
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_extractor_get_source_file (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), NULL);
  return self->source_file;
}

/**
 * autoar_extractor_get_output_file:
 * @self: an #AutoarExtractor
 *
 * This function is similar to autoar_extractor_get_output(), except for the
 * return value is a #GFile.
 *
 * Returns: (transfer none): a #GFile
 **/
GFile*
autoar_extractor_get_output_file (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), NULL);
  return self->output_file;
}

/**
 * autoar_extractor_get_total_size:
 * @self: an #AutoarExtractor
 *
 * Gets the size in bytes will be written when the operation is completed.
 *
 * Returns: total size of extracted files in bytes
 **/
guint64
autoar_extractor_get_total_size (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), 0);
  return self->total_size;
}

/**
 * autoar_extractor_get_completed_size:
 * @self: an #AutoarExtractor
 *
 * Gets the size in bytes has been written to disk.
 *
 * Returns: size in bytes has been written
 **/
guint64
autoar_extractor_get_completed_size (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), 0);
  return self->completed_size;
}

/**
 * autoar_extractor_get_total_files:
 * @self: an #AutoarExtractor
 *
 * Gets the total number of files will be written when the operation is
 * completed.
 *
 * Returns: total number of extracted files
 **/
guint
autoar_extractor_get_total_files (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), 0);
  return self->total_files;
}

/**
 * autoar_extractor_get_completed_files:
 * @self: an #AutoarExtractor
 *
 * Gets the number of files has been written to disk.
 *
 * Returns: number of files has been written to disk
 **/
guint
autoar_extractor_get_completed_files (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), 0);
  return self->completed_files;
}

/**
 * autoar_extractor_get_output_is_dest:
 * @self: an #AutoarExtractor
 *
 * See autoar_extractor_set_output_is_dest().
 *
 * Returns: %TRUE if #AutoarExtractor:output is the location of extracted file
 * or directory
 **/
gboolean
autoar_extractor_get_output_is_dest (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), FALSE);
  return self->output_is_dest;
}

/**
 * autoar_extractor_get_delete_after_extraction:
 * @self: an #AutoarExtractor
 *
 * Whether the source archive will be deleted after a successful extraction.
 *
 * Returns: %TRUE if the source archive will be deleted after a succesful
 * extraction
 **/
gboolean
autoar_extractor_get_delete_after_extraction (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), FALSE);
  return self->delete_after_extraction;
}

/**
 * autoar_extractor_get_notify_interval:
 * @self: an #AutoarExtractor
 *
 * See autoar_extractor_set_notify_interval().
 *
 * Returns: the minimal interval in microseconds between the emission of the
 * #AutoarExtractor::progress signal.
 **/
gint64
autoar_extractor_get_notify_interval (AutoarExtractor *self)
{
  g_return_val_if_fail (AUTOAR_IS_EXTRACTOR (self), 0);
  return self->notify_interval;
}

/**
 * autoar_extractor_set_output_is_dest:
 * @self: an #AutoarExtractor
 * @output_is_dest: %TRUE if the location of the extracted directory or file
 * has been already decided
 *
 * By default #AutoarExtractor:output-is-dest is set to %FALSE, which means
 * only one file or directory will be generated. The destination is internally
 * determined by analyzing the contents of the archive. If this is not wanted,
 * #AutoarExtractor:output-is-dest can be set to %TRUE, which will make
 * #AutoarExtractor:output the destination for extracted files. In any case, the
 * destination will be notified via #AutoarExtractor::decide-destination, when
 * it is possible to set a new destination.
 *
 * #AutoarExtractor will attempt to create the destination regardless to whether
 * its path was internally decided or not.
 *
 * This function should only be called before calling autoar_extractor_start() or
 * autoar_extractor_start_async().
 **/
void
autoar_extractor_set_output_is_dest  (AutoarExtractor *self,
                                      gboolean         output_is_dest)
{
  g_return_if_fail (AUTOAR_IS_EXTRACTOR (self));
  self->output_is_dest = output_is_dest;
}

/**
 * autoar_extractor_set_delete_after_extraction:
 * @self: an #AutoarExtractor
 * @delete_after_extraction: %TRUE if the source archive should be deleted
 * after a successful extraction
 *
 * By default #AutoarExtractor:delete-after-extraction is set to %FALSE so the
 * source archive will not be automatically deleted if extraction succeeds.
 **/
void
autoar_extractor_set_delete_after_extraction (AutoarExtractor *self,
                                              gboolean         delete_after_extraction)
{
  g_return_if_fail (AUTOAR_IS_EXTRACTOR (self));
  self->delete_after_extraction = delete_after_extraction;
}

/**
 * autoar_extractor_set_notify_interval:
 * @self: an #AutoarExtractor
 * @notify_interval: the minimal interval in microseconds
 *
 * Sets the minimal interval between emission of #AutoarExtractor::progress
 * signal. This prevent too frequent signal emission, which may cause
 * performance impact. If you do not want this feature, you can set the interval
 * to 0, so you will receive every progress update.
 **/
void
autoar_extractor_set_notify_interval (AutoarExtractor *self,
                                      gint64           notify_interval)
{
  g_return_if_fail (AUTOAR_IS_EXTRACTOR (self));
  g_return_if_fail (notify_interval >= 0);
  self->notify_interval = notify_interval;
}

static void
autoar_extractor_dispose (GObject *object)
{
  AutoarExtractor *self;

  self = AUTOAR_EXTRACTOR (object);

  g_debug ("AutoarExtractor: dispose");

  if (self->istream != NULL) {
    if (!g_input_stream_is_closed (self->istream)) {
      g_input_stream_close (self->istream, self->cancellable, NULL);
    }
    g_object_unref (self->istream);
    self->istream = NULL;
  }

  g_clear_object (&(self->source_file));
  g_clear_object (&(self->output_file));
  g_clear_object (&(self->destination_dir));
  g_clear_object (&(self->cancellable));
  g_clear_object (&(self->prefix));
  g_clear_object (&(self->new_prefix));

  g_list_free_full (self->files_list, g_object_unref);
  self->files_list = NULL;

  if (self->userhash != NULL) {
    g_hash_table_unref (self->userhash);
    self->userhash = NULL;
  }

  if (self->grouphash != NULL) {
    g_hash_table_unref (self->grouphash);
    self->grouphash = NULL;
  }

  if (self->extracted_dir_list != NULL) {
    g_array_unref (self->extracted_dir_list);
    self->extracted_dir_list = NULL;
  }

  G_OBJECT_CLASS (autoar_extractor_parent_class)->dispose (object);
}

static void
autoar_extractor_finalize (GObject *object)
{
  AutoarExtractor *self;

  self = AUTOAR_EXTRACTOR (object);

  g_debug ("AutoarExtractor: finalize");

  g_free (self->buffer);
  self->buffer = NULL;

  if (self->error != NULL) {
    g_error_free (self->error);
    self->error = NULL;
  }

  g_free (self->suggested_destname);
  self->suggested_destname = NULL;

  G_OBJECT_CLASS (autoar_extractor_parent_class)->finalize (object);
}

static int
libarchive_read_open_cb (struct archive *ar_read,
                         void           *client_data)
{
  AutoarExtractor *self;
  GFileInputStream *istream;

  g_debug ("libarchive_read_open_cb: called");

  self = AUTOAR_EXTRACTOR (client_data);

  if (self->error != NULL)
    return ARCHIVE_FATAL;

  istream = g_file_read (self->source_file,
                         self->cancellable,
                         &(self->error));
  self->istream = G_INPUT_STREAM (istream);

  if (self->error != NULL)
    return ARCHIVE_FATAL;

  g_debug ("libarchive_read_open_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static int
libarchive_read_close_cb (struct archive *ar_read,
                          void           *client_data)
{
  AutoarExtractor *self;

  g_debug ("libarchive_read_close_cb: called");

  self = AUTOAR_EXTRACTOR (client_data);

  if (self->error != NULL)
    return ARCHIVE_FATAL;

  if (self->istream != NULL) {
    g_input_stream_close (self->istream, self->cancellable, NULL);
    g_object_unref (self->istream);
    self->istream = NULL;
  }

  g_debug ("libarchive_read_close_cb: ARCHIVE_OK");
  return ARCHIVE_OK;
}

static ssize_t
libarchive_read_read_cb (struct archive  *ar_read,
                         void            *client_data,
                         const void     **buffer)
{
  AutoarExtractor *self;
  gssize read_size;

  g_debug ("libarchive_read_read_cb: called");

  self = AUTOAR_EXTRACTOR (client_data);

  if (self->error != NULL || self->istream == NULL)
    return -1;

  *buffer = self->buffer;
  read_size = g_input_stream_read (self->istream,
                                   self->buffer,
                                   self->buffer_size,
                                   self->cancellable,
                                   &(self->error));
  if (self->error != NULL)
    return -1;

  g_debug ("libarchive_read_read_cb: %" G_GSSIZE_FORMAT, read_size);
  return read_size;
}

static gint64
libarchive_read_seek_cb (struct archive *ar_read,
                         void           *client_data,
                         gint64          request,
                         int             whence)
{
  AutoarExtractor *self;
  GSeekable *seekable;
  GSeekType  seektype;
  off_t new_offset;

  g_debug ("libarchive_read_seek_cb: called");

  self = AUTOAR_EXTRACTOR (client_data);
  seekable = (GSeekable*)(self->istream);
  if (self->error != NULL || self->istream == NULL)
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
                   self->cancellable,
                   &(self->error));
  new_offset = g_seekable_tell (seekable);
  if (self->error != NULL)
    return -1;

  g_debug ("libarchive_read_seek_cb: %"G_GOFFSET_FORMAT, (goffset)new_offset);
  return new_offset;
}

static gint64
libarchive_read_skip_cb (struct archive *ar_read,
                         void           *client_data,
                         gint64          request)
{
  AutoarExtractor *self;
  GSeekable *seekable;
  off_t old_offset, new_offset;

  g_debug ("libarchive_read_skip_cb: called");

  self = AUTOAR_EXTRACTOR (client_data);
  seekable = (GSeekable*)(self->istream);
  if (self->error != NULL || self->istream == NULL) {
    return -1;
  }

  old_offset = g_seekable_tell (seekable);
  new_offset = libarchive_read_seek_cb (ar_read, client_data, request, SEEK_CUR);
  if (new_offset > old_offset)
    return (new_offset - old_offset);

  return 0;
}

static int
libarchive_create_read_object (gboolean          use_raw_format,
                               AutoarExtractor  *self,
                               struct archive  **a)
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
  archive_read_set_callback_data (*a, self);

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
autoar_extractor_signal_scanned (AutoarExtractor *self)
{
  autoar_common_g_signal_emit (self, self->in_thread,
                               autoar_extractor_signals[SCANNED], 0,
                               self->total_files);
}

static inline void
autoar_extractor_signal_decide_destination (AutoarExtractor *self,
                                            GFile *destination,
                                            GList *files,
                                            GFile **new_destination)
{
  autoar_common_g_signal_emit (self, self->in_thread,
                               autoar_extractor_signals[DECIDE_DESTINATION], 0,
                               destination,
                               files,
                               new_destination);
}

static inline void
autoar_extractor_signal_progress (AutoarExtractor *self)
{
  gint64 mtime;
  mtime = g_get_monotonic_time ();
  if (mtime - self->notify_last >= self->notify_interval) {
    autoar_common_g_signal_emit (self, self->in_thread,
                                 autoar_extractor_signals[PROGRESS], 0,
                                 self->completed_size,
                                 self->completed_files);
    self->notify_last = mtime;
  }
}

static AutoarConflictAction
autoar_extractor_signal_conflict (AutoarExtractor  *self,
                                  GFile            *file,
                                  GFile           **new_file)
{
  AutoarConflictAction action = AUTOAR_CONFLICT_OVERWRITE;

  autoar_common_g_signal_emit (self, self->in_thread,
                               autoar_extractor_signals[CONFLICT], 0,
                               file,
                               new_file,
                               &action);

  if (*new_file) {
    g_autofree char *previous_path;
    g_autofree char *new_path;

    previous_path = g_file_get_path (file);
    new_path = g_file_get_path (*new_file);

    g_debug ("autoar_extractor_signal_conflict: %s => %s",
             previous_path, new_path);
  }

  return action;
}

static inline void
autoar_extractor_signal_cancelled (AutoarExtractor *self)
{
  autoar_common_g_signal_emit (self, self->in_thread,
                               autoar_extractor_signals[CANCELLED], 0);

}

static inline void
autoar_extractor_signal_completed (AutoarExtractor *self)
{
  autoar_common_g_signal_emit (self, self->in_thread,
                               autoar_extractor_signals[COMPLETED], 0);

}

static inline void
autoar_extractor_signal_error (AutoarExtractor *self)
{
  if (self->error != NULL) {
    if (self->error->domain == G_IO_ERROR &&
        self->error->code == G_IO_ERROR_CANCELLED) {
      g_error_free (self->error);
      self->error = NULL;
      autoar_extractor_signal_cancelled (self);
    } else {
      autoar_common_g_signal_emit (self, self->in_thread,
                                   autoar_extractor_signals[AR_ERROR], 0,
                                   self->error);
    }
  }
}

static GFile*
autoar_extractor_get_common_prefix (GList *files,
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
autoar_extractor_do_sanitize_pathname (AutoarExtractor *self,
                                       const char      *pathname_bytes)
{
  GFile *extracted_filename;
  gboolean valid_filename;
  g_autofree char *sanitized_pathname;
  g_autofree char *utf8_pathname;

  utf8_pathname = autoar_common_get_utf8_pathname (pathname_bytes);
  extracted_filename = g_file_get_child (self->destination_dir,
                                         utf8_pathname ?  utf8_pathname : pathname_bytes);

  valid_filename =
    g_file_equal (extracted_filename, self->destination_dir) ||
    g_file_has_prefix (extracted_filename, self->destination_dir);

  if (!valid_filename) {
    g_autofree char *basename;

    basename = g_file_get_basename (extracted_filename);

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (self->destination_dir,
                                           basename);
  }

  if (self->prefix != NULL && self->new_prefix != NULL) {
    g_autofree char *relative_path;
    /* Replace the old prefix with the new one */
    relative_path = g_file_get_relative_path (self->prefix,
                                              extracted_filename);

    relative_path = relative_path != NULL ? relative_path : g_strdup ("");

    g_object_unref (extracted_filename);

    extracted_filename = g_file_get_child (self->new_prefix,
                                           relative_path);
  }

  sanitized_pathname = g_file_get_path (extracted_filename);

  g_debug ("autoar_extractor_do_sanitize_pathname: %s", sanitized_pathname);

  return extracted_filename;
}

static gboolean
autoar_extractor_check_file_conflict (GFile  *file,
                                      mode_t  extracted_filetype)
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
autoar_extractor_do_write_entry (AutoarExtractor      *self,
                                 struct archive       *a,
                                 struct archive_entry *entry,
                                 GFile                *dest,
                                 GFile                *hardlink)
{
  GFileInfo *info;
  mode_t filetype;
  int r;

  {
    GFile *parent;
    parent = g_file_get_parent (dest);
    if (!g_file_query_exists (parent, self->cancellable))
      g_file_make_directory_with_parents (parent,
                                          self->cancellable,
                                          NULL);
    g_object_unref (parent);
  }

  info = g_file_info_new ();

  /* time */
  g_debug ("autoar_extractor_do_write_entry: time");
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

    g_debug ("autoar_extractor_do_write_entry: user");
#ifdef HAVE_GETPWNAM
    if ((uname = archive_entry_uname (entry)) != NULL) {
      void *got_uid;
      if (g_hash_table_lookup_extended (self->userhash, uname, NULL, &got_uid) == TRUE) {
        uid = GPOINTER_TO_UINT (got_uid);
      } else {
        struct passwd *pwd = getpwnam (uname);
        if (pwd == NULL) {
          uid = archive_entry_uid (entry);
        } else {
          uid = pwd->pw_uid;
          g_hash_table_insert (self->userhash, g_strdup (uname), GUINT_TO_POINTER (uid));
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

    g_debug ("autoar_extractor_do_write_entry: group");
#ifdef HAVE_GETGRNAM
    if ((gname = archive_entry_gname (entry)) != NULL) {
      void *got_gid;
      if (g_hash_table_lookup_extended (self->grouphash, gname, NULL, &got_gid) == TRUE) {
        gid = GPOINTER_TO_UINT (got_gid);
      } else {
        struct group *grp = getgrnam (gname);
        if (grp == NULL) {
          gid = archive_entry_gid (entry);
        } else {
          gid = grp->gr_gid;
          g_hash_table_insert (self->grouphash, g_strdup (gname), GUINT_TO_POINTER (gid));
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
  g_debug ("autoar_extractor_do_write_entry: permissions");
  g_file_info_set_attribute_uint32 (info,
                                    G_FILE_ATTRIBUTE_UNIX_MODE,
                                    archive_entry_perm (entry));

#ifdef HAVE_LINK
  if (hardlink != NULL) {
    char *hardlink_path, *dest_path;
    r = link (hardlink_path = g_file_get_path (hardlink),
              dest_path = g_file_get_path (dest));
    g_debug ("autoar_extractor_do_write_entry: hard link, %s => %s, %d",
             dest_path, hardlink_path, r);
    g_free (hardlink_path);
    g_free (dest_path);
    if (r >= 0) {
      g_debug ("autoar_extractor_do_write_entry: skip file creation");
      goto applyinfo;
    }
  }
#endif

  g_debug ("autoar_extractor_do_write_entry: writing");
  r = 0;

  switch (filetype = archive_entry_filetype (entry)) {
    default:
    case AE_IFREG:
      {
        GOutputStream *ostream;
        const void *buffer;
        size_t size, written;
        gint64 offset;

        g_debug ("autoar_extractor_do_write_entry: case REG");

        ostream = (GOutputStream*)g_file_replace (dest,
                                                  NULL,
                                                  FALSE,
                                                  G_FILE_CREATE_NONE,
                                                  self->cancellable,
                                                  &(self->error));
        if (self->error != NULL) {
          g_object_unref (info);
          return;
        }

        if (ostream != NULL) {
          /* Archive entry size may be zero if we use raw format. */
          if (archive_entry_size(entry) > 0 || self->use_raw_format) {
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
                                         self->cancellable,
                                         &(self->error));
              if (self->error != NULL) {
                g_output_stream_close (ostream, self->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              if (g_cancellable_is_cancelled (self->cancellable)) {
                g_output_stream_close (ostream, self->cancellable, NULL);
                g_object_unref (ostream);
                g_object_unref (info);
                return;
              }
              self->completed_size += written;
              autoar_extractor_signal_progress (self);
            }
          }
          g_output_stream_close (ostream, self->cancellable, NULL);
          g_object_unref (ostream);
        }
      }
      break;
    case AE_IFDIR:
      {
        GFileAndInfo fileandinfo;

        g_debug ("autoar_extractor_do_write_entry: case DIR");

        g_file_make_directory_with_parents (dest, self->cancellable, &(self->error));

        if (self->error != NULL) {
          /* "File exists" is not a fatal error, as long as the existing file
           * is a directory
           */
          GFileType file_type;

          file_type = g_file_query_file_type (dest,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL);

          if (g_error_matches (self->error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
              file_type == G_FILE_TYPE_DIRECTORY) {
            g_clear_error (&self->error);
          } else {
            g_object_unref (info);
            return;
          }
        }

        fileandinfo.file = g_object_ref (dest);
        fileandinfo.info = g_object_ref (info);
        g_array_append_val (self->extracted_dir_list, fileandinfo);
      }
      break;
    case AE_IFLNK:
      g_debug ("autoar_extractor_do_write_entry: case LNK");
      g_file_make_symbolic_link (dest,
                                 archive_entry_symlink (entry),
                                 self->cancellable,
                                 &(self->error));
      break;
    /* FIFOs, sockets, block files, character files are not important
     * in the regular archives, so errors are not fatal. */
#if defined HAVE_MKFIFO || defined HAVE_MKNOD
    case AE_IFIFO:
      {
        char *path;
        g_debug ("autoar_extractor_do_write_entry: case FIFO");
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
        g_debug ("autoar_extractor_do_write_entry: case SOCK");
        r = mknod (path = g_file_get_path (dest),
                   S_IFSOCK | archive_entry_perm (entry),
                   0);
        g_free (path);
      }
      break;
    case AE_IFBLK:
      {
        char *path;
        g_debug ("autoar_extractor_do_write_entry: case BLK");
        r = mknod (path = g_file_get_path (dest),
                   S_IFBLK | archive_entry_perm (entry),
                   archive_entry_rdev (entry));
        g_free (path);
      }
      break;
    case AE_IFCHR:
      {
        char *path;
        g_debug ("autoar_extractor_do_write_entry: case CHR");
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
    ostream = (GOutputStream*)g_file_append_to (dest, G_FILE_CREATE_NONE, self->cancellable, NULL);
    if (ostream != NULL) {
      g_output_stream_close (ostream, self->cancellable, NULL);
      g_object_unref (ostream);
    }
  }
#endif

applyinfo:
  g_debug ("autoar_extractor_do_write_entry: applying info");
  g_file_set_attributes_from_info (dest,
                                   info,
                                   G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                   self->cancellable,
                                   &(self->error));

  if (self->error != NULL) {
    g_debug ("autoar_extractor_do_write_entry: %s\n", self->error->message);
    g_error_free (self->error);
    self->error = NULL;
  }

  g_object_unref (info);
}

static void
autoar_extractor_class_init (AutoarExtractorClass *klass)
{
  GObjectClass *object_class;
  GType type;

  object_class = G_OBJECT_CLASS (klass);
  type = G_TYPE_FROM_CLASS (klass);

  object_class->get_property = autoar_extractor_get_property;
  object_class->set_property = autoar_extractor_set_property;
  object_class->dispose = autoar_extractor_dispose;
  object_class->finalize = autoar_extractor_finalize;

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

  g_object_class_install_property (object_class, PROP_TOTAL_SIZE,
                                   g_param_spec_uint64 ("total-size",
                                                        "Total files size",
                                                        "Total size of the extracted files",
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

  g_object_class_install_property (object_class, PROP_TOTAL_FILES,
                                   g_param_spec_uint ("total-files",
                                                      "Total files",
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
 * AutoarExtractor::scanned:
 * @self: the #AutoarExtractor
 * @files: the number of files will be extracted from the source archive
 *
 * This signal is emitted when #AutoarExtractor finish scanning filename entries
 * in the source archive.
 **/
  autoar_extractor_signals[SCANNED] =
    g_signal_new ("scanned",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_UINT);

/**
 * AutoarExtractor::decide-destination:
 * @self: the #AutoarExtractor
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
  autoar_extractor_signals[DECIDE_DESTINATION] =
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
 * AutoarExtractor::progress:
 * @self: the #AutoarExtractor
 * @completed_size: bytes has been written to disk
 * @completed_files: number of files have been written to disk
 *
 * This signal is used to report progress of creating archives.
 **/
  autoar_extractor_signals[PROGRESS] =
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
 * AutoarExtractor::conflict:
 * @self: the #AutoarExtractor
 * @file: the file that caused a conflict
 * @new_file: an address to store the new destination for a conflict file
 *
 * Returns: the action to be performed by #AutoarExtractor
 *
 * This signal is used to report and offer the possibility to solve name
 * conflicts when extracting files.
 **/
  autoar_extractor_signals[CONFLICT] =
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
 * AutoarExtractor::cancelled:
 * @self: the #AutoarExtractor
 *
 * This signal is emitted after archive extracting job is cancelled by the
 * #GCancellable.
 **/
  autoar_extractor_signals[CANCELLED] =
    g_signal_new ("cancelled",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarExtractor::completed:
 * @self: the #AutoarExtractor
 *
 * This signal is emitted after the archive extracting job is successfully
 * completed.
 **/
  autoar_extractor_signals[COMPLETED] =
    g_signal_new ("completed",
                  type,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

/**
 * AutoarExtractor::error:
 * @self: the #AutoarExtractor
 * @error: the #GError
 *
 * This signal is emitted when error occurs and all jobs should be terminated.
 * Possible error domains are %AUTOAR_EXTRACTOR_ERROR, %G_IO_ERROR, and
 * %AUTOAR_LIBARCHIVE_ERROR, which represent error occurs in #AutoarExtractor,
 * GIO, and libarchive, respectively. The #GError is owned by #AutoarExtractor
 * and should not be freed.
 **/
  autoar_extractor_signals[AR_ERROR] =
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
autoar_extractor_init (AutoarExtractor *self)
{
  self->cancellable = NULL;

  self->total_size = 0;
  self->completed_size = 0;

  self->files_list = NULL;

  self->total_files = 0;
  self->completed_files = 0;

  self->notify_last = 0;

  self->istream = NULL;
  self->buffer_size = BUFFER_SIZE;
  self->buffer = g_new (char, self->buffer_size);
  self->error = NULL;

  self->userhash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->grouphash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->extracted_dir_list = g_array_new (FALSE, FALSE, sizeof (GFileAndInfo));
  g_array_set_clear_func (self->extracted_dir_list, g_file_and_info_free);
  self->destination_dir = NULL;
  self->new_prefix = NULL;

  self->suggested_destname = NULL;

  self->in_thread = FALSE;
  self->use_raw_format = FALSE;
}

/**
 * autoar_extractor_new:
 * @source_file: source archive
 * @output_file: output directory of extracted file or directory, or the
 * file name of the extracted file or directory itself if you set
 * #AutoarExtractor:output-is-dest on the returned object
 *
 * Create a new #AutoarExtractor object.
 *
 * Returns: (transfer full): a new #AutoarExtractor object
 **/
AutoarExtractor*
autoar_extractor_new (GFile *source_file,
                      GFile *output_file)
{
  AutoarExtractor *self;

  g_return_val_if_fail (source_file != NULL, NULL);
  g_return_val_if_fail (output_file != NULL, NULL);

  self = g_object_new (AUTOAR_TYPE_EXTRACTOR,
                       "source-file", source_file,
                       "output-file", output_file,
                       NULL);

  self->source_basename = g_file_get_basename (self->source_file);
  self->suggested_destname = autoar_common_get_basename_remove_extension (self->source_basename);

  return self;
}

static void
autoar_extractor_step_scan_toplevel (AutoarExtractor *self)
{
  /* Step 0: Scan all file names in the archive
   * We have to check whether the archive contains a top-level directory
   * before performing the extraction. We emit the "scanned" signal when
   * the checking is completed. */

  struct archive *a;
  struct archive_entry *entry;

  int r;

  g_debug ("autoar_extractor_step_scan_toplevel: called");

  r = libarchive_create_read_object (FALSE, self, &a);
  if (r != ARCHIVE_OK) {
    archive_read_free (a);
    r = libarchive_create_read_object (TRUE, self, &a);
    if (r != ARCHIVE_OK) {
      if (self->error == NULL)
        self->error = autoar_common_g_error_new_a (a, self->source_basename);
      return;
    } else if (archive_filter_count (a) <= 1){
      /* If we only use raw format and filter count is one, libarchive will
       * not do anything except for just copying the source file. We do not
       * want this thing to happen because it does unnecesssary copying. */
      if (self->error == NULL)
        self->error = g_error_new (AUTOAR_EXTRACTOR_ERROR,
                                        NOT_AN_ARCHIVE_ERRNO,
                                        "\'%s\': %s",
                                        self->source_basename,
                                        "not an archive");
      return;
    }
    self->use_raw_format = TRUE;
  }

  while ((r = archive_read_next_header (a, &entry)) == ARCHIVE_OK) {
    const char *pathname;
    g_autofree char *utf8_pathname = NULL;

    if (g_cancellable_is_cancelled (self->cancellable)) {
      archive_read_free (a);
      return;
    }

    if (archive_entry_is_encrypted (entry)) {
      break;
    }

    if (self->use_raw_format) {
      pathname = autoar_common_get_basename_remove_extension (g_file_get_path(self->source_file));
      g_debug ("autoar_extractor_step_scan_toplevel: %d: raw pathname = %s",
               self->total_files, pathname);
    } else {
      pathname = archive_entry_pathname (entry);
      utf8_pathname = autoar_common_get_utf8_pathname (pathname);

      g_debug ("autoar_extractor_step_scan_toplevel: %d: pathname = %s %s%s",
               self->total_files, pathname,
               utf8_pathname ? "utf8 pathname = " :"",
               utf8_pathname ? utf8_pathname : "");
    }
    self->files_list =
      g_list_prepend (self->files_list,
                      g_file_get_child (self->output_file,
                                        utf8_pathname ? utf8_pathname : pathname));
    self->total_files++;
    self->total_size += archive_entry_size (entry);
    archive_read_data_skip (a);
  }

  if (entry && archive_entry_is_encrypted (entry)) {
    g_debug ("autoar_extractor_step_scan_toplevel: encrypted entry");
    if (self->error == NULL) {
      self->error = g_error_new (G_IO_ERROR,
                                 G_IO_ERROR_NOT_SUPPORTED,
                                 "Encrypted archives are not supported.");
    }
    archive_read_free (a);
    return;
  }

  if (self->files_list == NULL) {
    if (self->error == NULL) {
      self->error = g_error_new (AUTOAR_EXTRACTOR_ERROR,
                                      EMPTY_ARCHIVE_ERRNO,
                                      "\'%s\': %s",
                                      self->source_basename,
                                      "empty archive");
    }
    archive_read_free (a);
    return;
  }

  if (r != ARCHIVE_EOF) {
    if (self->error == NULL) {
      self->error =
        autoar_common_g_error_new_a (a, self->source_basename);
    }
    archive_read_free (a);
    return;
  }

  /* If we are unable to determine the total size, set it to a positive
   * number to prevent strange percentage. */
  if (self->total_size <= 0)
    self->total_size = G_MAXUINT64;

  archive_read_free (a);

  g_debug ("autoar_extractor_step_scan_toplevel: files = %d",
           self->total_files);

  self->files_list = g_list_reverse (self->files_list);

  self->prefix =
    autoar_extractor_get_common_prefix (self->files_list,
                                        self->output_file);

  if (self->prefix != NULL) {
    g_autofree char *path_prefix;

    path_prefix = g_file_get_path (self->prefix);
    g_debug ("autoar_extractor_step_scan_toplevel: pathname_prefix = %s",
             path_prefix);
  }

  autoar_extractor_signal_scanned (self);
}

static void
autoar_extractor_step_set_destination (AutoarExtractor *self)
{
  /* Step 1: Set destination based on client preferences or archive contents */

  g_debug ("autoar_extractor_step_set_destination: called");

  if (self->output_is_dest) {
    self->destination_dir = g_object_ref (self->output_file);
    return;
  }

  if (self->prefix != NULL) {
    /* We must check if the archive and the prefix have the same name (without
     * the extension). If they do, then the destination should be the output
     * directory itself.
     */
    g_autofree char *prefix_name;
    g_autofree char *prefix_name_no_ext;

    prefix_name = g_file_get_basename (self->prefix);
    prefix_name_no_ext = autoar_common_get_basename_remove_extension (prefix_name);

    if (g_strcmp0 (prefix_name, self->suggested_destname) == 0 ||
        g_strcmp0 (prefix_name_no_ext, self->suggested_destname) == 0) {
      self->destination_dir = g_object_ref (self->output_file);
    } else {
      g_clear_object (&self->prefix);
    }
  }
  /* If none of the above situations apply, the top level directory gets the
   * name suggested when creating the AutoarExtractor object
   */
  if (self->destination_dir == NULL) {
    self->destination_dir = g_file_get_child (self->output_file,
                                              self->suggested_destname);
  }
}

static void
autoar_extractor_step_decide_destination (AutoarExtractor *self)
{
  /* Step 2: Decide destination */

  GList *files = NULL;
  GList *l;
  GFile *new_destination = NULL;
  g_autofree char *destination_name;

  for (l = self->files_list; l != NULL; l = l->next) {
    char *relative_path;
    GFile *file;

    relative_path = g_file_get_relative_path (self->output_file, l->data);
    file = g_file_resolve_relative_path (self->destination_dir,
                                         relative_path);
    files = g_list_prepend (files, file);

    g_free (relative_path);
  }

  files = g_list_reverse (files);

  /* When it exists, the common prefix is the actual output of the extraction
   * and the client has the opportunity to change it. Also, the old prefix is
   * needed in order to replace it with the new one
   */
  if (self->prefix != NULL) {
    autoar_extractor_signal_decide_destination (self,
                                                self->prefix,
                                                files,
                                                &new_destination);

    self->new_prefix = new_destination;
  } else {
    autoar_extractor_signal_decide_destination (self,
                                                self->destination_dir,
                                                files,
                                                &new_destination);

    if (new_destination) {
      g_object_unref (self->destination_dir);
      self->destination_dir = new_destination;
    }
  }

  destination_name = g_file_get_path (self->new_prefix != NULL ?
                                      self->new_prefix :
                                      self->destination_dir);
  g_debug ("autoar_extractor_step_decide_destination: destination %s", destination_name);

  g_file_make_directory_with_parents (self->destination_dir,
                                      self->cancellable,
                                      &(self->error));

  if (g_error_matches (self->error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
    GFileType file_type;

    file_type = g_file_query_file_type (self->destination_dir,
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL);

    if (file_type == G_FILE_TYPE_DIRECTORY) {
      /* FIXME: Implement a way to solve directory conflicts */
      g_debug ("autoar_extractor_step_decide_destination: destination directory exists");
      g_clear_error (&self->error);
    }
  }

  g_list_free_full (files, g_object_unref);
}

static void
autoar_extractor_step_extract (AutoarExtractor *self) {
  /* Step 3: Extract files
   * We have to re-open the archive to extract files
   */

  struct archive *a;
  struct archive_entry *entry;

  int r;

  g_debug ("autoar_extractor_step_extract: called");

  r = libarchive_create_read_object (self->use_raw_format, self, &a);
  if (r != ARCHIVE_OK) {
    if (self->error == NULL) {
      self->error =
        autoar_common_g_error_new_a (a, self->source_basename);
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

    if (g_cancellable_is_cancelled (self->cancellable)) {
      archive_read_free (a);
      return;
    }

    pathname = archive_entry_pathname (entry);
    hardlink = archive_entry_hardlink (entry);

    extracted_filename =
      autoar_extractor_do_sanitize_pathname (self, pathname);

    if (hardlink != NULL) {
      hardlink_filename =
        autoar_extractor_do_sanitize_pathname (self, hardlink);
    }

    /* Attempt to solve any name conflict before doing any operations */
    file_conflict = autoar_extractor_check_file_conflict (extracted_filename,
                                                          archive_entry_filetype (entry));
    while (file_conflict) {
      GFile *new_extracted_filename = NULL;

      action = autoar_extractor_signal_conflict (self,
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

      file_conflict = autoar_extractor_check_file_conflict (extracted_filename,
                                                            archive_entry_filetype (entry));
    }

    if (file_conflict && action == AUTOAR_CONFLICT_SKIP) {
      continue;
    }

    autoar_extractor_do_write_entry (self, a, entry,
                                     extracted_filename, hardlink_filename);

    if (self->error != NULL) {
      archive_read_free (a);
      return;
    }

    self->completed_files++;
    autoar_extractor_signal_progress (self);
  }

  if (r != ARCHIVE_EOF) {
    if (self->error == NULL) {
      self->error =
        autoar_common_g_error_new_a (a, self->source_basename);
    }
    archive_read_free (a);
    return;
  }

  archive_read_free (a);
}

static void
autoar_extractor_step_apply_dir_fileinfo (AutoarExtractor *self) {
  /* Step 4: Re-apply file info to all directories
   * It is required because modification times may be updated during the
   * writing of files in the directory.
   */

  int i;

  g_debug ("autoar_extractor_step_apply_dir_fileinfo: called");

  for (i = 0; i < self->extracted_dir_list->len; i++) {
    GFile *file = g_array_index (self->extracted_dir_list, GFileAndInfo, i).file;
    GFileInfo *info = g_array_index (self->extracted_dir_list, GFileAndInfo, i).info;
    g_file_set_attributes_from_info (file, info,
                                     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                     self->cancellable, NULL);
    if (g_cancellable_is_cancelled (self->cancellable)) {
      return;
    }
  }
}

static void
autoar_extractor_step_cleanup (AutoarExtractor *self) {
  /* Step 5: Force progress to be 100% and remove the source archive file
   * If the extraction is completed successfully, remove the source file.
   * Errors are not fatal because we have completed our work.
   */

  g_debug ("autoar_extractor_step_cleanup: called");

  self->completed_size = self->total_size;
  self->completed_files = self->total_files;
  self->notify_last = 0;
  autoar_extractor_signal_progress (self);
  g_debug ("autoar_extractor_step_cleanup: Update progress");

  if (self->delete_after_extraction) {
    g_debug ("autoar_extractor_step_cleanup: Delete");
    g_file_delete (self->source_file, self->cancellable, NULL);
  }
}

static void
autoar_extractor_run (AutoarExtractor *self)
{
  /* Numbers of steps.
   * The array size must be modified if more steps are added. */
  void (*steps[7])(AutoarExtractor*);

  int i;

  g_return_if_fail (AUTOAR_IS_EXTRACTOR (self));

  g_return_if_fail (self->source_file != NULL);
  g_return_if_fail (self->output_file != NULL);

  if (g_cancellable_is_cancelled (self->cancellable)) {
    autoar_extractor_signal_cancelled (self);
    return;
  }

  i = 0;
  steps[i++] = autoar_extractor_step_scan_toplevel;
  steps[i++] = autoar_extractor_step_set_destination;
  steps[i++] = autoar_extractor_step_decide_destination;
  steps[i++] = autoar_extractor_step_extract;
  steps[i++] = autoar_extractor_step_apply_dir_fileinfo;
  steps[i++] = autoar_extractor_step_cleanup;
  steps[i++] = NULL;

  for (i = 0; steps[i] != NULL; i++) {
    g_debug ("autoar_extractor_run: Step %d Begin", i);
    (*steps[i])(self);
    g_debug ("autoar_extractor_run: Step %d End", i);
    if (self->error != NULL) {
      autoar_extractor_signal_error (self);
      return;
    }
    if (g_cancellable_is_cancelled (self->cancellable)) {
      autoar_extractor_signal_cancelled (self);
      return;
    }
  }

  autoar_extractor_signal_completed (self);
}

/**
 * autoar_extractor_start:
 * @self: an #AutoarExtractor object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Runs the archive extracting work. All callbacks will be called in the same
 * thread as the caller of this functions.
 **/
void
autoar_extractor_start (AutoarExtractor *self,
                        GCancellable    *cancellable)
{
  if (cancellable != NULL)
    g_object_ref (cancellable);
  self->cancellable = cancellable;
  self->in_thread = FALSE;
  autoar_extractor_run (self);
}

static void
autoar_extractor_start_async_thread (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  AutoarExtractor *self = source_object;
  autoar_extractor_run (self);
  g_task_return_pointer (task, NULL, g_free);
  g_object_unref (self);
  g_object_unref (task);
}


/**
 * autoar_extractor_start_async:
 * @self: an #AutoarExtractor object
 * @cancellable: optional #GCancellable object, or %NULL to ignore
 *
 * Asynchronously runs the archive extracting work. You should connect to
 * #AutoarExtractor::cancelled, #AutoarExtractor::error, and
 * #AutoarExtractor::completed signal to get notification when the work is
 * terminated. All callbacks will be called in the main thread, so you can
 * safely manipulate GTK+ widgets in the callbacks.
 **/
void
autoar_extractor_start_async (AutoarExtractor *self,
                              GCancellable    *cancellable)
{
  GTask *task;

  g_object_ref (self);
  if (cancellable != NULL)
    g_object_ref (cancellable);
  self->cancellable = cancellable;
  self->in_thread = TRUE;

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_task_data (task, NULL, NULL);
  g_task_run_in_thread (task, autoar_extractor_start_async_thread);
}

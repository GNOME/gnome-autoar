/*
 * SPDX-FileCopyrightText: 2013  Ting-Wei Lan
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef AUTOAR_COMPRESSOR_H
#define AUTOAR_COMPRESSOR_H

#include <glib-object.h>
#include <gio/gio.h>

#include "autoar-format-filter.h"

G_BEGIN_DECLS

#define AUTOAR_TYPE_COMPRESSOR autoar_compressor_get_type ()

G_DECLARE_FINAL_TYPE (AutoarCompressor, autoar_compressor, AUTOAR, COMPRESSOR, GObject)

/**
 * AUTOAR_COMPRESSOR_ERROR:
 *
 * Error domain for #AutoarCompressor. Not all error occurs in #AutoarCompressor uses
 * this domain. It is only used for error occurs in #AutoarCompressor itself.
 * See #AutoarCompressor::error signal for more information.
 **/
#define AUTOAR_COMPRESSOR_ERROR autoar_compressor_quark()

GQuark             autoar_compressor_quark                          (void);

AutoarCompressor * autoar_compressor_new                            (GList        *source_files,
                                                                     GFile        *output_file,
                                                                     AutoarFormat  format,
                                                                     AutoarFilter  filter,
                                                                     gboolean      create_top_level_directory);

void               autoar_compressor_start                          (AutoarCompressor *self,
                                                                     GCancellable     *cancellable);
void               autoar_compressor_start_async                    (AutoarCompressor *self,
                                                                     GCancellable     *cancellable);

GList *            autoar_compressor_get_source_files               (AutoarCompressor *self);
GFile *            autoar_compressor_get_output_file                (AutoarCompressor *self);
AutoarFormat       autoar_compressor_get_format                     (AutoarCompressor *self);
AutoarFilter       autoar_compressor_get_filter                     (AutoarCompressor *self);
gboolean           autoar_compressor_get_create_top_level_directory (AutoarCompressor *self);
guint64            autoar_compressor_get_size                       (AutoarCompressor *self);
guint64            autoar_compressor_get_completed_size             (AutoarCompressor *self);
guint              autoar_compressor_get_files                      (AutoarCompressor *self);
guint              autoar_compressor_get_completed_files            (AutoarCompressor *self);
gboolean           autoar_compressor_get_output_is_dest             (AutoarCompressor *self);
gint64             autoar_compressor_get_notify_interval            (AutoarCompressor *self);

void               autoar_compressor_set_output_is_dest             (AutoarCompressor *self,
                                                                     gboolean          output_is_dest);
void               autoar_compressor_set_notify_interval            (AutoarCompressor *self,
                                                                     gint64            notify_interval);
void               autoar_compressor_set_passphrase                 (AutoarCompressor *self,
                                                                     const gchar      *passphrase);
void               autoar_compressor_set_multithreaded              (AutoarCompressor *self,
                                                                     gboolean          multithreaded);
G_END_DECLS

#endif /* AUTOAR_COMPRESSOR_H */

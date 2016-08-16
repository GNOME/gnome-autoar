/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-extractor.h
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

#ifndef AUTOAR_EXTRACTOR_H
#define AUTOAR_EXTRACTOR_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AUTOAR_TYPE_EXTRACTOR autoar_extractor_get_type ()

G_DECLARE_FINAL_TYPE (AutoarExtractor, autoar_extractor, AUTOAR, EXTRACTOR, GObject)

/**
 * AUTOAR_EXTRACT_ERROR:
 *
 * Error domain for #AutoarExtractor. Not all error occurs in #AutoarExtractor uses
 * this domain. It is only used for error occurs in #AutoarExtractor itself.
 * See #AutoarExtractor::error signal for more information.
 **/
#define AUTOAR_EXTRACTOR_ERROR autoar_extractor_quark()

GQuark           autoar_extractor_quark                 (void);

GType            autoar_extractor_get_type              (void) G_GNUC_CONST;

AutoarExtractor *autoar_extractor_new                   (GFile *source_file,
                                                         GFile *output_file);

void             autoar_extractor_start                 (AutoarExtractor *extractor,
                                                         GCancellable *cancellable);
void             autoar_extractor_start_async           (AutoarExtractor *extractor,
                                                         GCancellable *cancellable);

char            *autoar_extractor_get_source            (AutoarExtractor *extractor);
GFile           *autoar_extractor_get_source_file       (AutoarExtractor *extractor);
char            *autoar_extractor_get_output            (AutoarExtractor *extractor);
GFile           *autoar_extractor_get_output_file       (AutoarExtractor *extractor);
guint64          autoar_extractor_get_size              (AutoarExtractor *extractor);
guint64          autoar_extractor_get_completed_size    (AutoarExtractor *extractor);
guint            autoar_extractor_get_files             (AutoarExtractor *extractor);
guint            autoar_extractor_get_completed_files   (AutoarExtractor *extractor);
gboolean         autoar_extractor_get_output_is_dest    (AutoarExtractor *extractor);
gboolean         autoar_extractor_get_delete_if_succeed (AutoarExtractor *extractor);
gint64           autoar_extractor_get_notify_interval   (AutoarExtractor *extractor);

void             autoar_extractor_set_output_is_dest    (AutoarExtractor *extractor,
                                                         gboolean output_is_dest);
void             autoar_extractor_set_delete_if_succeed (AutoarExtractor *extractor,
                                                         gboolean delete_if_succeed);
void             autoar_extractor_set_notify_interval   (AutoarExtractor *extractor,
                                                         gint64 notify_interval);

typedef enum {
    AUTOAR_CONFLICT_OVERWRITE = 0,
    AUTOAR_CONFLICT_CHANGE_DESTINATION,
    AUTOAR_CONFLICT_SKIP
} AutoarConflictAction;

G_END_DECLS

#endif /* AUTOAR_EXTRACTOR_H */

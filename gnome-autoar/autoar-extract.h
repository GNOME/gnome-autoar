/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-extract.h
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

#ifndef AUTOAR_EXTRACT_H
#define AUTOAR_EXTRACT_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AUTOAR_TYPE_EXTRACT autoar_extract_get_type ()

G_DECLARE_FINAL_TYPE (AutoarExtract, autoar_extract, AUTOAR, EXTRACT, GObject)

/**
 * AUTOAR_EXTRACT_ERROR:
 *
 * Error domain for #AutoarExtract. Not all error occurs in #AutoarExtract uses
 * this domain. It is only used for error occurs in #AutoarExtract itself.
 * See #AutoarExtract::error signal for more information.
 **/
#define AUTOAR_EXTRACT_ERROR autoar_extract_quark()

GQuark          autoar_extract_quark                 (void);

GType           autoar_extract_get_type              (void) G_GNUC_CONST;

AutoarExtract  *autoar_extract_new                   (GFile *source_file,
                                                      GFile *output_file);

void            autoar_extract_start                 (AutoarExtract *arextract,
                                                      GCancellable *cancellable);
void            autoar_extract_start_async           (AutoarExtract *arextract,
                                                      GCancellable *cancellable);

char           *autoar_extract_get_source            (AutoarExtract *arextract);
GFile          *autoar_extract_get_source_file       (AutoarExtract *arextract);
char           *autoar_extract_get_output            (AutoarExtract *arextract);
GFile          *autoar_extract_get_output_file       (AutoarExtract *arextract);
guint64         autoar_extract_get_size              (AutoarExtract *arextract);
guint64         autoar_extract_get_completed_size    (AutoarExtract *arextract);
guint           autoar_extract_get_files             (AutoarExtract *arextract);
guint           autoar_extract_get_completed_files   (AutoarExtract *arextract);
gboolean        autoar_extract_get_output_is_dest    (AutoarExtract *arextract);
gboolean        autoar_extract_get_delete_if_succeed (AutoarExtract *arextract);
gint64          autoar_extract_get_notify_interval   (AutoarExtract *arextract);

void            autoar_extract_set_output_is_dest    (AutoarExtract *arextract,
                                                      gboolean output_is_dest);
void            autoar_extract_set_delete_if_succeed (AutoarExtract *arextract,
                                                      gboolean delete_if_succeed);
void            autoar_extract_set_notify_interval   (AutoarExtract *arextract,
                                                      gint64 notify_interval);

typedef enum {
    AUTOAR_CONFLICT_OVERWRITE = 0,
    AUTOAR_CONFLICT_CHANGE_DESTINATION,
    AUTOAR_CONFLICT_SKIP
} AutoarConflictAction;

G_END_DECLS

#endif /* AUTOAR_EXTRACT_H */

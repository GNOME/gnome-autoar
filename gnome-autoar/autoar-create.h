/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-create.h
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

#ifndef AUTOAR_CREATE_H
#define AUTOAR_CREATE_H

#include <glib-object.h>
#include <gio/gio.h>

#include "autoar-format-filter.h"

G_BEGIN_DECLS

#define AUTOAR_TYPE_CREATE autoar_create_get_type ()

G_DECLARE_FINAL_TYPE (AutoarCreate, autoar_create, AUTOAR, CREATE, GObject)

/**
 * AUTOAR_CREATE_ERROR:
 *
 * Error domain for #AutoarCreate. Not all error occurs in #AutoarCreate uses
 * this domain. It is only used for error occurs in #AutoarCreate itself.
 * See #AutoarCreate::error signal for more information.
 **/
#define AUTOAR_CREATE_ERROR autoar_create_quark()

GQuark         autoar_create_quark                          (void);

GType          autoar_create_get_type                       (void) G_GNUC_CONST;

AutoarCreate * autoar_create_new                            (GList *source_files,
                                                             GFile *output_file,
                                                             AutoarFormat format,
                                                             AutoarFilter filter,
                                                             gboolean create_top_level_directory);

void           autoar_create_start                          (AutoarCreate *arcreate,
                                                             GCancellable *cancellable);
void           autoar_create_start_async                    (AutoarCreate *arcreate,
                                                             GCancellable *cancellable);

GList *        autoar_create_get_source_files               (AutoarCreate *arcreate);
GFile *        autoar_create_get_output_file                (AutoarCreate *arcreate);
AutoarFormat   autoar_create_get_format                     (AutoarCreate *arcreate);
AutoarFilter   autoar_create_get_filter                     (AutoarCreate *arcreate);
gboolean       autoar_create_get_create_top_level_directory (AutoarCreate *arcreate);
guint64        autoar_create_get_size                       (AutoarCreate *arcreate);
guint64        autoar_create_get_completed_size             (AutoarCreate *arcreate);
guint          autoar_create_get_files                      (AutoarCreate *arcreate);
guint          autoar_create_get_completed_files            (AutoarCreate *arcreate);
gboolean       autoar_create_get_output_is_dest             (AutoarCreate *arcreate);
gint64         autoar_create_get_notify_interval            (AutoarCreate *arcreate);

void           autoar_create_set_output_is_dest             (AutoarCreate *arcreate,
                                                             gboolean      output_is_dest);
void           autoar_create_set_notify_interval            (AutoarCreate *arcreate,
                                                             gint64        notify_interval);
G_END_DECLS

#endif /* AUTOAR_CREATE_H */

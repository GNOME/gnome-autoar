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

#include "autoar-pref.h"

G_BEGIN_DECLS

#define AUTOAR_TYPE_CREATE              autoar_create_get_type ()
#define AUTOAR_CREATE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), AUTOAR_TYPE_CREATE, AutoarCreate))
#define AUTOAR_CREATE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), AUTOAR_TYPE_CREATE, AutoarCreateClass))
#define AUTOAR_IS_CREATE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), AUTOAR_TYPE_CREATE))
#define AUTOAR_IS_CREATE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), AUTOAR_TYPE_CREATE))
#define AUTOAR_CREATE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), AUTOAR_TYPE_CREATE, AutoarCreateClass))

typedef struct _AutoarCreate AutoarCreate;
typedef struct _AutoarCreateClass AutoarCreateClass;
typedef struct _AutoarCreatePrivate AutoarCreatePrivate;

struct _AutoarCreate
{
  GObject parent;

  AutoarCreatePrivate *priv;
};

struct _AutoarCreateClass
{
  GObjectClass parent_class;

  void (* decide_dest)(AutoarCreate *arcreate,
                       GFile *destination);
  void (* progress)   (AutoarCreate *arcreate,
                       guint64 completed_size,
                       guint completed_files);
  void (* cancelled)  (AutoarCreate *arcreate);
  void (* completed)  (AutoarCreate *arcreate);
  void (* error)      (AutoarCreate *arcreate,
                       GError *error);
};

#define AUTOAR_CREATE_ERROR autoar_create_quark()

GQuark          autoar_create_quark               (void);

GType           autoar_create_get_type            (void) G_GNUC_CONST;

AutoarCreate*   autoar_create_new                 (AutoarPref *arpref,
                                                   const char *output,
                                                   ...);
AutoarCreate*   autoar_create_new_file            (AutoarPref *arpref,
                                                   GFile      *output_file,
                                                   ...);
AutoarCreate*   autoar_create_newv                (AutoarPref  *arpref,
                                                   const char  *output,
                                                   const char **source);
AutoarCreate*   autoar_create_new_filev           (AutoarPref  *arpref,
                                                   GFile       *output_file,
                                                   GFile      **source_file);

void            autoar_create_start               (AutoarCreate *arcreate,
                                                   GCancellable *cancellable);
void            autoar_create_start_async         (AutoarCreate *arcreate,
                                                   GCancellable *cancellable);

char          **autoar_create_get_source          (AutoarCreate *arcreate);
GPtrArray      *autoar_create_get_source_file     (AutoarCreate *arcreate);
char           *autoar_create_get_output          (AutoarCreate *arcreate);
GFile          *autoar_create_get_output_file     (AutoarCreate *arcreate);
guint64         autoar_create_get_size            (AutoarCreate *arcreate);
guint64         autoar_create_get_completed_size  (AutoarCreate *arcreate);
guint           autoar_create_get_files           (AutoarCreate *arcreate);
guint           autoar_create_get_completed_files (AutoarCreate *arcreate);
gboolean        autoar_create_get_output_is_dest  (AutoarCreate *arcreate);
gint64          autoar_create_get_notify_interval (AutoarCreate *arcreate);

void            autoar_create_set_output_is_dest  (AutoarCreate *arcreate,
                                                   gboolean output_is_dest);
void            autoar_create_set_notify_interval (AutoarCreate *arcreate,
                                                   gint64 notify_interval);
G_END_DECLS

#endif /* AUTOAR_CREATE_H */

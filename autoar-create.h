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
  void (* completed)  (AutoarCreate *arcreate);
  void (* error)      (AutoarCreate *arcreate,
                       GError *error);
};

GType           autoar_create_get_type            (void) G_GNUC_CONST;

AutoarCreate*   autoar_create_new                 (AutoarPref *arpref,
                                                   const char *output,
                                                   ...);
AutoarCreate*   autoar_create_newv                (AutoarPref  *arpref,
                                                   const char  *output,
                                                   const char **source);

void            autoar_create_start               (AutoarCreate *arcreate);
void            autoar_create_start_async         (AutoarCreate *arcreate);

char          **autoar_create_get_source          (AutoarCreate *arcreate);
char           *autoar_create_get_output          (AutoarCreate *arcreate);
guint64         autoar_create_get_completed_size  (AutoarCreate *arcreate);
guint           autoar_create_get_files           (AutoarCreate *arcreate);
guint           autoar_create_get_completed_files (AutoarCreate *arcreate);

void            autoar_create_set_completed_size  (AutoarCreate *arcreate,
                                                   guint64 completed_size);
void            autoar_create_set_files           (AutoarCreate *arcreate,
                                                   guint files);
void            autoar_create_set_completed_files (AutoarCreate *arcreate,
                                                   guint completed_files);

G_END_DECLS

#endif /* AUTOAR_CREATE_H */

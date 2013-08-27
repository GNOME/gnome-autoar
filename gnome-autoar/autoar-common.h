/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-common.h
 * Some common functions used in several classes of autoarchive
 * This file does NOT declare any new classes!
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

#ifndef AUTOAR_COMMON_H
#define AUTOAR_COMMON_H

/* archive.h use time_t */
#include <time.h>

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

char*     autoar_common_get_basename_remove_extension  (const char *filename);
char*     autoar_common_get_filename_extension         (const char *filename);

void      autoar_common_g_signal_emit                  (gpointer instance,
                                                        gboolean in_thread,
                                                        guint signal_id,
                                                        GQuark detail,
                                                        ...);

GError*   autoar_common_g_error_new_a                  (GQuark quark,
                                                        struct archive *a,
                                                        const char *pathname);
GError*   autoar_common_g_error_new_a_entry            (GQuark quark,
                                                        struct archive *a,
                                                        struct archive_entry *entry);

G_END_DECLS

#endif /* AUTOAR_COMMON_H */

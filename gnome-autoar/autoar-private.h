/*
 * SPDX-FileCopyrightText: 2013, 2014  Ting-Wei Lan
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Some common functions used in several classes of gnome-autoar
 * This file does NOT declare any new classes and it should NOT
 * be used outside the library itself!
 */

#ifndef AUTOAR_PRIVATE_H
#define AUTOAR_PRIVATE_H

/* archive.h use time_t */
#include <time.h>

#include <archive.h>
#include <archive_entry.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

char*     autoar_common_get_basename_remove_extension  (const char *filename);

void      autoar_common_g_signal_emit                  (gpointer instance,
                                                        gboolean in_thread,
                                                        guint signal_id,
                                                        GQuark detail,
                                                        ...);

GError*   autoar_common_g_error_new_a                  (struct archive *a,
                                                        const char *pathname);
GError*   autoar_common_g_error_new_a_entry            (struct archive *a,
                                                        struct archive_entry *entry);

char*     autoar_common_g_file_get_name                (GFile *file);
char*     autoar_common_get_utf8_pathname              (const char *pathname);

G_END_DECLS

#endif /* AUTOAR_COMMON_H */

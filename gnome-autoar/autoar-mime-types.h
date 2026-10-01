/*
 * SPDX-FileCopyrightText: 2016  Razvan Chitu <razvan.ch95@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Functions for checking autoar support for various mime types
 */

#ifndef AUTOAR_MIME_TYPES_H
#define AUTOAR_MIME_TYPES_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean autoar_check_mime_type_supported (const gchar *mime_type);
gboolean autoar_query_mime_type_supported (GFile *file);

G_END_DECLS

#endif /* AUTOAR_MIME_TYPES_H */

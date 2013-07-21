/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-pref.h
 * User preferences of automatic archives creation and extraction
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

#ifndef AUTOAR_PREF_H
#define AUTOAR_PREF_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
  AUTOAR_PREF_FORMAT_0, /*< skip >*/
  AUTOAR_PREF_FORMAT_ZIP,
  AUTOAR_PREF_FORMAT_TAR,
  AUTOAR_PREF_FORMAT_CPIO,
  AUTOAR_PREF_FORMAT_ISO9660,
  AUTOAR_PREF_FORMAT_LAST /*< skip >*/
} AutoarPrefFormat;

typedef enum {
  AUTOAR_PREF_FILTER_0, /*< skip >*/
  AUTOAR_PREF_FILTER_NONE,
  AUTOAR_PREF_FILTER_COMPRESS,
  AUTOAR_PREF_FILTER_GZIP,
  AUTOAR_PREF_FILTER_BZIP2,
  AUTOAR_PREF_FILTER_LZMA,
  AUTOAR_PREF_FILTER_XZ,
  AUTOAR_PREF_FILTER_LZIP,
  AUTOAR_PREF_FILTER_LAST /*< skip >*/
} AutoarPrefFilter;

#define AUTOAR_TYPE_PREF                autoar_pref_get_type ()
#define AUTOAR_PREF(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), AUTOAR_TYPE_PREF, AutoarPref))
#define AUTOAR_PREF_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), AUTOAR_TYPE_PREF, AutoarPrefClass))
#define AUTOAR_IS_PREF(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), AUTOAR_TYPE_PREF))
#define AUTOAR_IS_PREF_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), AUTOAR_TYPE_PREF))
#define AUTOAR_PREF_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), AUTOAR_TYPE_PREF, AutoarPrefClass))

typedef struct _AutoarPref AutoarPref;
typedef struct _AutoarPrefClass AutoarPrefClass;
typedef struct _AutoarPrefPrivate AutoarPrefPrivate;

struct _AutoarPref
{
  GObject parent;

  AutoarPrefPrivate *priv;
};

struct _AutoarPrefClass
{
  GObjectClass parent_class;
};

GType              autoar_pref_get_type              (void) G_GNUC_CONST;

AutoarPref        *autoar_pref_new                   (void);
AutoarPref        *autoar_pref_new_with_gsettings    (GSettings *settings);

gboolean           autoar_pref_read_gsettings        (AutoarPref *arpref,
                                                      GSettings *settings);
gboolean           autoar_pref_write_gsettings       (AutoarPref *arpref,
                                                      GSettings *settings);
gboolean           autoar_pref_write_gsettings_all   (AutoarPref *arpref,
                                                      GSettings *settings);

gboolean           autoar_pref_has_changes           (AutoarPref *arpref);
void               autoar_pref_forget_changes        (AutoarPref *arpref);

AutoarPrefFormat   autoar_pref_get_default_format    (AutoarPref *arpref);
AutoarPrefFilter   autoar_pref_get_default_filter    (AutoarPref *arpref);
const char       **autoar_pref_get_file_name_suffix  (AutoarPref *arpref);
const char       **autoar_pref_get_file_mime_type    (AutoarPref *arpref);
const char       **autoar_pref_get_pattern_to_ignore (AutoarPref *arpref);
gboolean           autoar_pref_get_delete_if_succeed (AutoarPref *arpref);

void               autoar_pref_set_default_format    (AutoarPref *arpref,
                                                      AutoarPrefFormat format);
void               autoar_pref_set_default_filter    (AutoarPref *arpref,
                                                      AutoarPrefFilter filter);
void               autoar_pref_set_file_name_suffix  (AutoarPref *arpref,
                                                      const char **strv,
                                                      size_t len);
void               autoar_pref_set_file_mime_type    (AutoarPref *arpref,
                                                      const char **strv,
                                                      size_t len);
void               autoar_pref_set_pattern_to_ignore (AutoarPref *arpref,
                                                      const char **strv,
                                                      size_t len);
void               autoar_pref_set_delete_if_succeed (AutoarPref *arpref,
                                                      gboolean delete_yes);


G_END_DECLS

#endif /* AUTOAR_PREF_H */

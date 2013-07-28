/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-pref.c
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

#include "config.h"

#include "autoar-pref.h"
#include "autoar-enum-types.h"

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

G_DEFINE_TYPE (AutoarPref, autoar_pref, G_TYPE_OBJECT)

#define AUTOAR_PREF_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_PREF, AutoarPrefPrivate))

struct _AutoarPrefPrivate
{
  unsigned int modification_flags;
  gboolean     modification_enabled;

  /* Archive creating preferences */
  AutoarPrefFormat   default_format;
  AutoarPrefFilter   default_filter;

  /* Archive extracting preferences */
  char     **file_name_suffix;
  char     **file_mime_type;
  char     **pattern_to_ignore;
  gboolean   delete_if_succeed;
};

enum
{
  PROP_0,
  PROP_DEFAULT_FORMAT,
  PROP_DEFAULT_FILTER,
  PROP_FILE_NAME_SUFFIX,
  PROP_FILE_MIME_TYPE,
  PROP_PATTERN_TO_IGNORE,
  PROP_DELETE_IF_SUCCEED
};

enum
{
  MODIFIED_NONE = 0,
  MODIFIED_DEFAULT_FORMAT = 1 << 0,
  MODIFIED_DEFAULT_FILTER = 1 << 1,
  MODIFIED_FILE_NAME_SUFFIX = 1 << 2,
  MODIFIED_FILE_MIME_TYPE = 1 << 3,
  MODIFIED_PATTERN_TO_IGNORE = 1 << 4,
  MODIFIED_DELETE_IF_SUCCEED = 1 << 5
};

#define KEY_DEFAULT_FORMAT     "default-format"
#define KEY_DEFAULT_FILTER     "default-filter"
#define KEY_FILE_NAME_SUFFIX   "file-name-suffix"
#define KEY_FILE_MIME_TYPE     "file-mime-type"
#define KEY_PATTERN_TO_IGNORE  "pattern-to-ignore"
#define KEY_DELETE_IF_SUCCEED  "delete-if-succeed"

static void
autoar_pref_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  AutoarPref *arpref;
  AutoarPrefPrivate *priv;

  GVariant *variant;

  const char* const* strv;

  arpref = AUTOAR_PREF (object);
  priv = arpref->priv;

  switch (property_id) {
    case PROP_DEFAULT_FORMAT:
      g_value_set_enum (value, priv->default_format);
      break;
    case PROP_DEFAULT_FILTER:
      g_value_set_enum (value, priv->default_filter);
      break;
    case PROP_FILE_NAME_SUFFIX:
      strv = (const char* const*)(priv->file_name_suffix);
      variant = g_variant_new_strv (strv, -1);
      g_value_take_variant (value, variant);
      break;
    case PROP_FILE_MIME_TYPE:
      strv = (const char* const*)(priv->file_mime_type);
      variant = g_variant_new_strv (strv, -1);
      g_value_take_variant (value, variant);
      break;
    case PROP_PATTERN_TO_IGNORE:
      strv = (const char* const*)(priv->pattern_to_ignore);
      variant = g_variant_new_strv (strv, -1);
      g_value_take_variant (value, variant);
      break;
    case PROP_DELETE_IF_SUCCEED:
      g_value_set_boolean (value, priv->delete_if_succeed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
autoar_pref_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  AutoarPref *arpref;
  const char **strv;

  arpref = AUTOAR_PREF (object);

  switch (property_id) {
    case PROP_DEFAULT_FORMAT:
      autoar_pref_set_default_format (arpref, g_value_get_enum (value));
      break;
    case PROP_DEFAULT_FILTER:
      autoar_pref_set_default_filter (arpref, g_value_get_enum (value));
      break;
    case PROP_FILE_NAME_SUFFIX:
      strv = g_variant_get_strv (g_value_get_variant (value), NULL);
      autoar_pref_set_file_name_suffix (arpref, strv);
      break;
    case PROP_FILE_MIME_TYPE:
      strv = g_variant_get_strv (g_value_get_variant (value), NULL);
      autoar_pref_set_file_mime_type (arpref, strv);
      break;
    case PROP_PATTERN_TO_IGNORE:
      strv = g_variant_get_strv (g_value_get_variant (value), NULL);
      autoar_pref_set_pattern_to_ignore (arpref, strv);
      break;
    case PROP_DELETE_IF_SUCCEED:
      autoar_pref_set_delete_if_succeed (arpref, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

AutoarPrefFormat
autoar_pref_get_default_format (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), AUTOAR_PREF_FORMAT_ZIP);
  return arpref->priv->default_format;
}

AutoarPrefFilter
autoar_pref_get_default_filter (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), AUTOAR_PREF_FILTER_NONE);
  return arpref->priv->default_filter;
}

const char**
autoar_pref_get_file_name_suffix (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), NULL);
  return (const char**)(arpref->priv->file_name_suffix);
}

const char**
autoar_pref_get_file_mime_type (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), NULL);
  return (const char**)(arpref->priv->file_mime_type);
}

const char**
autoar_pref_get_pattern_to_ignore (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), NULL);
  return (const char**)(arpref->priv->pattern_to_ignore);
}

gboolean
autoar_pref_get_delete_if_succeed (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), TRUE);
  return arpref->priv->delete_if_succeed;
}

void
autoar_pref_set_default_format (AutoarPref *arpref,
                                AutoarPrefFormat format)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (format > 0 && format < AUTOAR_PREF_FORMAT_LAST);
  if (arpref->priv->modification_enabled && format != arpref->priv->default_format)
    arpref->priv->modification_flags |= MODIFIED_DEFAULT_FORMAT;
  arpref->priv->default_format = format;
}

void
autoar_pref_set_default_filter (AutoarPref *arpref,
                                AutoarPrefFilter filter)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (filter > 0 && filter < AUTOAR_PREF_FILTER_LAST);
  if (arpref->priv->modification_enabled && filter != arpref->priv->default_filter)
    arpref->priv->modification_flags |= MODIFIED_DEFAULT_FILTER;
  arpref->priv->default_filter = filter;
}

void
autoar_pref_set_file_name_suffix (AutoarPref *arpref,
                                  const char **strv)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  if (arpref->priv->modification_enabled)
    arpref->priv->modification_flags |= MODIFIED_FILE_NAME_SUFFIX;
  g_strfreev (arpref->priv->file_name_suffix);
  arpref->priv->file_name_suffix = g_strdupv ((char**)strv);
}

void
autoar_pref_set_file_mime_type (AutoarPref *arpref,
                                const char **strv)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  if (arpref->priv->modification_enabled)
    arpref->priv->modification_flags |= MODIFIED_FILE_MIME_TYPE;
  g_strfreev (arpref->priv->file_mime_type);
  arpref->priv->file_mime_type = g_strdupv ((char**)strv);
}

void
autoar_pref_set_pattern_to_ignore (AutoarPref *arpref,
                                   const char **strv)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  if (arpref->priv->modification_enabled)
    arpref->priv->modification_flags |= MODIFIED_PATTERN_TO_IGNORE;
  g_strfreev (arpref->priv->pattern_to_ignore);
  arpref->priv->pattern_to_ignore = g_strdupv ((char**)strv);
}

void
autoar_pref_set_delete_if_succeed (AutoarPref *arpref,
                                   gboolean delete_yes)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  if (delete_yes)
    delete_yes = TRUE;
  if (arpref->priv->modification_enabled && delete_yes != arpref->priv->delete_if_succeed)
    arpref->priv->modification_flags |= MODIFIED_DELETE_IF_SUCCEED;
  arpref->priv->delete_if_succeed = delete_yes;
}

static void
autoar_pref_finalize (GObject *object)
{
  AutoarPref *arpref;
  AutoarPrefPrivate *priv;

  arpref = AUTOAR_PREF (object);
  priv = arpref->priv;

  g_strfreev (priv->file_name_suffix);
  g_strfreev (priv->file_mime_type);
  g_strfreev (priv->pattern_to_ignore);

  G_OBJECT_CLASS (autoar_pref_parent_class)->finalize (object);
}

static void
autoar_pref_class_init (AutoarPrefClass *klass)
{
  GObjectClass *object_class;
  GPtrArray *tmparr;

  object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (AutoarPrefPrivate));

  object_class->get_property = autoar_pref_get_property;
  object_class->set_property = autoar_pref_set_property;
  object_class->finalize = autoar_pref_finalize;

  g_object_class_install_property (object_class, PROP_DEFAULT_FORMAT,
                                   g_param_spec_enum (KEY_DEFAULT_FORMAT,
                                                      "Default format",
                                                      "Default file format for new archives",
                                                      AUTOAR_TYPE_PREF_FORMAT,
                                                      AUTOAR_PREF_FORMAT_ZIP,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_DEFAULT_FILTER,
                                   g_param_spec_enum (KEY_DEFAULT_FILTER,
                                                      "Default format",
                                                      "Default filter to create archives",
                                                      AUTOAR_TYPE_PREF_FORMAT,
                                                      AUTOAR_PREF_FORMAT_ZIP,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  tmparr = g_ptr_array_new ();
  g_ptr_array_add (tmparr, NULL);

  g_object_class_install_property (object_class, PROP_FILE_NAME_SUFFIX,
                                   g_param_spec_variant (KEY_FILE_NAME_SUFFIX,
                                                         "File name suffix",
                                                         "File name suffix whitelist for automatic extraction",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_FILE_MIME_TYPE,
                                   g_param_spec_variant (KEY_FILE_MIME_TYPE,
                                                         "File MIME type",
                                                         "File MIME type whitelist for automatic extraction",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_PATTERN_TO_IGNORE,
                                   g_param_spec_variant (KEY_PATTERN_TO_IGNORE,
                                                         "Pattern to ignore",
                                                         "Pattern of file name to skip when extracting files",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_DELETE_IF_SUCCEED,
                                   g_param_spec_boolean (KEY_DELETE_IF_SUCCEED,
                                                         "Delete if succeed",
                                                         "Delete the archive file if extraction is succeeded",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));
  g_ptr_array_unref (tmparr);
}

static void
autoar_pref_init (AutoarPref *arpref)
{
  AutoarPrefPrivate *priv;

  priv = AUTOAR_PREF_GET_PRIVATE (arpref);
  arpref->priv = priv;

  priv->modification_flags = MODIFIED_NONE;
  priv->modification_enabled = FALSE;

  priv->default_format = AUTOAR_PREF_FORMAT_ZIP;
  priv->default_filter = AUTOAR_PREF_FILTER_NONE;

  priv->file_name_suffix = NULL;
  priv->file_mime_type = NULL;
  priv->pattern_to_ignore = NULL;
  priv->delete_if_succeed = TRUE;
}

AutoarPref*
autoar_pref_new (void)
{
  return g_object_new (AUTOAR_TYPE_PREF, NULL);
}

AutoarPref*
autoar_pref_new_with_gsettings (GSettings *settings)
{
  AutoarPref *arpref;
  arpref = autoar_pref_new ();
  autoar_pref_read_gsettings (arpref, settings);
  return arpref;
}

void
autoar_pref_read_gsettings (AutoarPref *arpref,
                            GSettings *settings)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (settings != NULL);

  arpref->priv->default_format = g_settings_get_enum (settings, KEY_DEFAULT_FORMAT);
  arpref->priv->default_filter = g_settings_get_enum (settings, KEY_DEFAULT_FILTER);

  g_strfreev (arpref->priv->file_name_suffix);
  arpref->priv->file_name_suffix = g_settings_get_strv (settings, KEY_FILE_NAME_SUFFIX);
  g_strfreev (arpref->priv->file_mime_type);
  arpref->priv->file_mime_type = g_settings_get_strv (settings, KEY_FILE_MIME_TYPE);
  g_strfreev (arpref->priv->pattern_to_ignore);
  arpref->priv->pattern_to_ignore = g_settings_get_strv (settings, KEY_PATTERN_TO_IGNORE);

  arpref->priv->delete_if_succeed = g_settings_get_boolean (settings, KEY_DELETE_IF_SUCCEED);

  arpref->priv->modification_enabled = TRUE;
  arpref->priv->modification_flags = MODIFIED_NONE;
}

void
autoar_pref_write_gsettings (AutoarPref *arpref,
                             GSettings *settings)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (settings != NULL);

  if (arpref->priv->modification_enabled) {
    if (arpref->priv->modification_flags & MODIFIED_DEFAULT_FORMAT) {
      if (g_settings_set_enum (settings, KEY_DEFAULT_FORMAT, arpref->priv->default_format))
        arpref->priv->modification_flags ^= MODIFIED_DEFAULT_FORMAT;
    }
    if (arpref->priv->modification_flags & MODIFIED_DEFAULT_FILTER) {
      if (g_settings_set_enum (settings, KEY_DEFAULT_FILTER, arpref->priv->default_filter))
        arpref->priv->modification_flags ^= MODIFIED_DEFAULT_FILTER;
    }
    if (arpref->priv->modification_flags & MODIFIED_FILE_NAME_SUFFIX) {
      if (g_settings_set_strv (settings, KEY_FILE_NAME_SUFFIX, (const char* const*)(arpref->priv->file_name_suffix)))
        arpref->priv->modification_flags ^= MODIFIED_FILE_NAME_SUFFIX;
    }
    if (arpref->priv->modification_flags & MODIFIED_FILE_MIME_TYPE) {
      if (g_settings_set_strv (settings, KEY_FILE_MIME_TYPE, (const char* const*)(arpref->priv->file_mime_type)))
        arpref->priv->modification_flags ^= MODIFIED_FILE_MIME_TYPE;
    }
    if (arpref->priv->modification_flags & MODIFIED_PATTERN_TO_IGNORE) {
      if (g_settings_set_strv (settings, KEY_PATTERN_TO_IGNORE, (const char* const*)(arpref->priv->pattern_to_ignore)))
        arpref->priv->modification_flags ^= MODIFIED_PATTERN_TO_IGNORE;
    }
    if (arpref->priv->modification_flags & MODIFIED_DELETE_IF_SUCCEED) {
      if (g_settings_set_boolean (settings, KEY_DELETE_IF_SUCCEED, arpref->priv->delete_if_succeed))
        arpref->priv->modification_flags ^= MODIFIED_DELETE_IF_SUCCEED;
    }
  } else {
    return autoar_pref_write_gsettings_force (arpref, settings);
  }
}

void
autoar_pref_write_gsettings_force (AutoarPref *arpref,
                                   GSettings *settings)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (settings != NULL);

  g_settings_set_enum (settings, KEY_DEFAULT_FORMAT, arpref->priv->default_format);
  g_settings_set_enum (settings, KEY_DEFAULT_FILTER, arpref->priv->default_filter);
  g_settings_set_strv (settings, KEY_FILE_NAME_SUFFIX, (const char* const*)(arpref->priv->file_name_suffix));
  g_settings_set_strv (settings, KEY_FILE_MIME_TYPE, (const char* const*)(arpref->priv->file_mime_type));
  g_settings_set_strv (settings, KEY_PATTERN_TO_IGNORE, (const char* const*)(arpref->priv->pattern_to_ignore));
  g_settings_set_boolean (settings, KEY_DELETE_IF_SUCCEED, arpref->priv->delete_if_succeed);
}

gboolean
autoar_pref_has_changes (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), FALSE);
  return (arpref->priv->modification_enabled && arpref->priv->modification_flags);
}

void
autoar_pref_forget_changes (AutoarPref *arpref)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  arpref->priv->modification_flags = MODIFIED_NONE;
}

gboolean
autoar_pref_check_file_name (AutoarPref *arpref,
                             const char *filepath)
{
  char *dot_location;
  int i;

  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), FALSE);
  g_return_val_if_fail (arpref->priv->file_name_suffix != NULL, FALSE);

  dot_location = strrchr (filepath, '.');
  if (dot_location == NULL)
    return FALSE;

  for (i = 0; arpref->priv->file_name_suffix[i] != NULL; i++) {
    if (strcmp (dot_location + 1, arpref->priv->file_name_suffix[i]) == 0)
      return TRUE;
  }

  return FALSE;
}

gboolean
autoar_pref_check_mime_type (AutoarPref *arpref,
                             const char *filepath)
{
  int i;
  GFile *file;
  GFileInfo *fileinfo;
  const char *content_type;
  const char *mime_type;

  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), FALSE);
  g_return_val_if_fail (arpref->priv->file_mime_type != NULL, FALSE);

  file = g_file_new_for_commandline_arg (filepath);
  fileinfo = g_file_query_info (file,
                                G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                NULL,
                                NULL);
  g_object_unref (file);

  if (fileinfo == NULL)
    return FALSE;

  content_type = g_file_info_get_content_type (fileinfo);
  g_debug ("Content Type: %s\n", content_type);
  mime_type = g_content_type_get_mime_type (content_type);
  g_debug ("MIME Type: %s\n", mime_type);

  for (i = 0; arpref->priv->file_mime_type[i] != NULL; i++) {
    if (strcmp (mime_type, arpref->priv->file_mime_type[i]) == 0) {
      g_object_unref (fileinfo);
      return TRUE;
    }
  }

  g_object_unref (fileinfo);
  return FALSE;
}

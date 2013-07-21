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

#include <glib.h>
#include <gio/gio.h>


G_DEFINE_TYPE (AutoarPref, autoar_pref, G_TYPE_OBJECT)

#define AUTOAR_PREF_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_PREF, AutoarPrefPrivate))

struct _AutoarPrefPrivate
{
  /* Archive creating preferences */
  AutoarPrefFormat   default_format;
  AutoarPrefFilter   default_filter;

  /* Archive extracting preferences */
  GPtrArray *file_name_suffix;
  GPtrArray *file_mime_type;
  GPtrArray *pattern_to_ignore;
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
  gssize len;

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
      strv = (const char* const*)(priv->file_name_suffix->pdata);
      len = (gssize)(priv->file_name_suffix->len - 1);
      variant = g_variant_new_strv (strv, len);
      g_value_take_variant (value, variant);
      break;
    case PROP_FILE_MIME_TYPE:
      strv = (const char* const*)(priv->file_mime_type->pdata);
      len = (gssize)(priv->file_mime_type->len - 1);
      variant = g_variant_new_strv (strv, len);
      g_value_take_variant (value, variant);
      break;
    case PROP_PATTERN_TO_IGNORE:
      strv = (const char* const*)(priv->pattern_to_ignore->pdata);
      len = (gssize)(priv->pattern_to_ignore->len - 1);
      variant = g_variant_new_strv (strv, len);
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
  AutoarPrefPrivate *priv;
  const char **strv;
  gsize len;

  arpref = AUTOAR_PREF (object);
  priv = arpref->priv;

  switch (property_id) {
    case PROP_DEFAULT_FORMAT:
      autoar_pref_set_default_format (arpref, g_value_get_enum (value));
      break;
    case PROP_DEFAULT_FILTER:
      autoar_pref_set_default_filter (arpref, g_value_get_enum (value));
      break;
    case PROP_FILE_NAME_SUFFIX:
      strv = g_variant_get_strv (g_value_get_variant (value), &len);
      autoar_pref_set_file_name_suffix (arpref, strv, len);
      break;
    case PROP_FILE_MIME_TYPE:
      strv = g_variant_get_strv (g_value_get_variant (value), NULL);
      autoar_pref_set_file_mime_type (arpref, strv, len);
      break;
    case PROP_PATTERN_TO_IGNORE:
      strv = g_variant_get_strv (g_value_get_variant (value), &len);
      autoar_pref_set_pattern_to_ignore (arpref, strv, len);
      break;
    case PROP_DELETE_IF_SUCCEED:
      priv->delete_if_succeed = g_value_get_boolean (value);
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
  return (const char**)(arpref->priv->file_name_suffix->pdata);
}

const char**
autoar_pref_get_file_mime_type (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), NULL);
  return (const char**)(arpref->priv->file_mime_type->pdata);
}

const char**
autoar_pref_get_pattern_to_ignore (AutoarPref *arpref)
{
  g_return_val_if_fail (AUTOAR_IS_PREF (arpref), NULL);
  return (const char**)(arpref->priv->pattern_to_ignore->pdata);
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
  arpref->priv->default_format = format;
}

void
autoar_pref_set_default_filter (AutoarPref *arpref,
                                AutoarPrefFilter filter)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (filter > 0 && filter < AUTOAR_PREF_FILTER_LAST);
  arpref->priv->default_filter = filter;
}


static void
autoar_pref_set_strv (AutoarPref *arpref,
                      GPtrArray **ptr,
                      const char **strv,
                      size_t len)
{
  int i;

  g_strfreev ((char**)g_ptr_array_free (*ptr, FALSE));

  if (len > 0)
    *ptr = g_ptr_array_sized_new (len + 1);
  else
    *ptr = g_ptr_array_new ();

  for (i = 0; strv[i] != NULL; i++)
    g_ptr_array_add (*ptr, g_strdup (strv[i]));

  g_ptr_array_add (*ptr, NULL);
}

void
autoar_pref_set_file_name_suffix (AutoarPref *arpref,
                                  const char **strv,
                                  size_t len)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  autoar_pref_set_strv (arpref, &(arpref->priv->file_name_suffix), strv, len);
}

void
autoar_pref_set_file_mime_type (AutoarPref *arpref,
                                const char **strv,
                                size_t len)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  autoar_pref_set_strv (arpref, &(arpref->priv->file_mime_type), strv, len);
}

void
autoar_pref_set_pattern_to_ignore (AutoarPref *arpref,
                                   const char **strv,
                                   size_t len)
{
  g_return_if_fail (AUTOAR_IS_PREF (arpref));
  g_return_if_fail (strv != NULL);
  autoar_pref_set_strv (arpref, &(arpref->priv->pattern_to_ignore), strv, len);
}

static void
autoar_pref_finalize (GObject *object)
{
  AutoarPref *arpref;
  AutoarPrefPrivate *priv;

  arpref = AUTOAR_PREF (object);
  priv = arpref->priv;

  g_strfreev ((char**)g_ptr_array_free (priv->file_name_suffix, FALSE));
  g_strfreev ((char**)g_ptr_array_free (priv->file_mime_type, FALSE));
  g_strfreev ((char**)g_ptr_array_free (priv->pattern_to_ignore, FALSE));

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
                                   g_param_spec_enum ("default-format",
                                                      "Default format",
                                                      "Default format to create archives",
                                                      AUTOAR_TYPE_PREF_FORMAT,
                                                      AUTOAR_PREF_FORMAT_ZIP,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_DEFAULT_FILTER,
                                   g_param_spec_enum ("default-filter",
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
                                   g_param_spec_variant ("file-name-suffix",
                                                         "File name suffix",
                                                         "File name suffix whitelist for automatic extraction",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_FILE_MIME_TYPE,
                                   g_param_spec_variant ("file-mime-type",
                                                         "File MIME type",
                                                         "File MIME type whitelist for automatic extraction",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_PATTERN_TO_IGNORE,
                                   g_param_spec_variant ("pattern-to-ignore",
                                                         "Pattern to ignore",
                                                         "Pattern of file name to skip when extracting files",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_DELETE_IF_SUCCEED,
                                   g_param_spec_boolean ("delete-if-succeed",
                                                         "Delete if succeed",
                                                         "Delete the archive file if extraction is succeeded",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));
}

static void
autoar_pref_init (AutoarPref *arpref)
{
  AutoarPrefPrivate *priv;

  priv = AUTOAR_PREF_GET_PRIVATE (arpref);
  arpref->priv = priv;

  priv->default_format = AUTOAR_PREF_FORMAT_ZIP;
  priv->default_filter = AUTOAR_PREF_FILTER_NONE;

  priv->file_name_suffix = g_ptr_array_new ();
  priv->file_mime_type = g_ptr_array_new ();
  priv->pattern_to_ignore = g_ptr_array_new ();
  priv->delete_if_succeed = TRUE;
}

AutoarPref*
autoar_pref_new (void)
{
  return g_object_new (AUTOAR_TYPE_PREF, NULL);
}

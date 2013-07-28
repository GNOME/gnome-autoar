/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-create.c
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

#include "config.h"

#include "autoar-create.h"
#include "autoar-pref.h"

#include <gio/gio.h>
#include <glib.h>

G_DEFINE_TYPE (AutoarCreate, autoar_create, G_TYPE_OBJECT)

#define AUTOAR_CREATE_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), AUTOAR_TYPE_CREATE, AutoarCreatePrivate))

#define BUFFER_SIZE (64 * 1024)

struct _AutoarCreatePrivate
{
  char **source;
  char  *output;

  guint64 completed_size;

  guint files;
  guint completed_files;

  AutoarPref *arpref;

  GOutputStream *ostream;
  void          *buffer;
  gssize         buffer_size;
  GError        *error;
};

enum
{
  DECIDE_DEST,
  PROGRESS,
  COMPLETED,
  ERROR,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SOURCE,
  PROP_OUTPUT,
  PROP_COMPLETED_SIZE,
  PROP_FILES,
  PROP_COMPLETED_FILES
};

static guint autoar_create_signals[LAST_SIGNAL] = { 0 };
static GQuark autoar_create_quark;

static void
autoar_create_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  AutoarCreate *arcreate;
  AutoarCreatePrivate *priv;

  GVariant *variant;
  const char* const* strv;

  arcreate = AUTOAR_CREATE (object);
  priv = arcreate->priv;

  switch (property_id) {
    case PROP_SOURCE:
      strv = (const char* const*)(priv->source);
      variant = g_variant_new_strv (strv, -1);
      g_value_take_variant (value, variant);
      break;
    case PROP_OUTPUT:
      g_value_set_string (value, priv->output);
      break;
    case PROP_COMPLETED_SIZE:
      g_value_set_uint64 (value, priv->completed_size);
      break;
    case PROP_FILES:
      g_value_set_uint (value, priv->files);
      break;
    case PROP_COMPLETED_FILES:
      g_value_set_uint (value, priv->completed_files);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
autoar_create_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  AutoarCreate *arcreate;
  AutoarCreatePrivate *priv;

  const char **strv;

  arcreate = AUTOAR_CREATE (object);
  priv = arcreate->priv;

  switch (property_id) {
    case PROP_COMPLETED_SIZE:
      autoar_create_set_completed_size (arcreate, g_value_get_uint64 (value));
      break;
    case PROP_FILES:
      autoar_create_set_files (arcreate, g_value_get_uint (value));
      break;
    case PROP_COMPLETED_FILES:
      autoar_create_set_completed_files (arcreate, g_value_get_uint (value));
      break;
    case PROP_SOURCE:
      strv = g_variant_get_strv (g_value_get_variant (value), NULL);
      g_strfreev (arcreate->priv->source);
      arcreate->priv->source = g_strdupv ((char**)strv);
      break;
    case PROP_OUTPUT:
      g_free (priv->output);
      priv->output = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

char**
autoar_create_get_source (AutoarCreate *arcreate)
{
  g_return_val_if_fail (AUTOAR_IS_CREATE (arcreate), NULL);
  return arcreate->priv->source;
}

char*
autoar_create_get_output (AutoarCreate *arcreate)
{
  g_return_val_if_fail (AUTOAR_IS_CREATE (arcreate), NULL);
  return arcreate->priv->output;
}

guint64
autoar_create_get_completed_size (AutoarCreate *arcreate)
{
  g_return_val_if_fail (AUTOAR_IS_CREATE (arcreate), 0);
  return arcreate->priv->completed_size;
}

guint
autoar_create_get_files (AutoarCreate *arcreate)
{
  g_return_val_if_fail (AUTOAR_IS_CREATE (arcreate), 0);
  return arcreate->priv->files;
}

guint
autoar_create_get_completed_files (AutoarCreate *arcreate)
{
  g_return_val_if_fail (AUTOAR_IS_CREATE (arcreate), 0);
  return arcreate->priv->completed_files;
}

void
autoar_create_set_completed_size (AutoarCreate *arcreate,
                                  guint64 completed_size)
{
  g_return_if_fail (AUTOAR_IS_CREATE (arcreate));
  g_return_if_fail (completed_size <= arcreate->priv->completed_size);
  arcreate->priv->completed_size = completed_size;
}

void
autoar_create_set_files (AutoarCreate *arcreate,
                         guint files)
{
  g_return_if_fail (AUTOAR_IS_CREATE (arcreate));
  arcreate->priv->files = files;
}

void
autoar_create_set_completed_files (AutoarCreate *arcreate,
                                   guint completed_files)
{
  g_return_if_fail (AUTOAR_IS_CREATE (arcreate));
  g_return_if_fail (completed_files <= arcreate->priv->completed_files);
  arcreate->priv->completed_files = completed_files;
}

static void
autoar_create_dispose (GObject *object)
{
  AutoarCreate *arcreate;
  arcreate = AUTOAR_CREATE (object);

  g_debug ("AutoarCreate: dispose");

  g_clear_object (&(arcreate->priv->arpref));

  G_OBJECT_CLASS (autoar_create_parent_class)->dispose (object);
}

static void
autoar_create_finalize (GObject *object)
{
  AutoarCreate *arcreate;
  AutoarCreatePrivate *priv;

  arcreate = AUTOAR_CREATE (object);
  priv = arcreate->priv;

  g_debug ("AutoarCreate: finalize");

  g_strfreev (priv->source);
  priv->source = NULL;

  g_free (priv->output);
  priv->output = NULL;

  if (priv->ostream != NULL) {
    if (!g_output_stream_is_closed (priv->ostream)) {
      g_output_stream_close (priv->ostream, NULL, NULL);
    }
    g_object_unref (priv->ostream);
  }

  g_free (priv->buffer);
  priv->buffer = NULL;

  if (priv->error != NULL) {
    g_error_free (priv->error);
    priv->error = NULL;
  }

  G_OBJECT_CLASS (autoar_create_parent_class)->finalize (object);
}

static void
autoar_create_class_init (AutoarCreateClass *klass)
{
  GObjectClass *object_class;
  GType type;
  GPtrArray *tmparr;

  object_class = G_OBJECT_CLASS (klass);
  type = G_TYPE_FROM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (AutoarCreatePrivate));

  autoar_create_quark = g_quark_from_static_string ("autoar-create");

  object_class->get_property = autoar_create_get_property;
  object_class->set_property = autoar_create_set_property;
  object_class->dispose = autoar_create_dispose;
  object_class->finalize = autoar_create_finalize;

  tmparr = g_ptr_array_new ();
  g_ptr_array_add (tmparr, NULL);

  g_object_class_install_property (object_class, PROP_SOURCE,
                                   g_param_spec_variant ("source",
                                                         "Source archive",
                                                         "The source files and directories to be compressed",
                                                         G_VARIANT_TYPE_STRING_ARRAY,
                                                         g_variant_new_strv ((const char* const*)tmparr->pdata, -1),
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_OUTPUT,
                                   g_param_spec_string ("output",
                                                        "Output directory",
                                                        "Output directory of created archive",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_COMPLETED_SIZE,
                                   g_param_spec_uint64 ("completed-size",
                                                        "Read file size",
                                                        "Bytes written to the archive",
                                                        0, G_MAXUINT64, 0,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_FILES,
                                   g_param_spec_uint ("files",
                                                      "Files",
                                                      "Number of files to be compressed",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_COMPLETED_FILES,
                                   g_param_spec_uint ("completed-files",
                                                      "Read files",
                                                      "Number of files has been read",
                                                      0, G_MAXUINT32, 0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_NICK |
                                                      G_PARAM_STATIC_BLURB));

  autoar_create_signals[DECIDE_DEST] =
    g_signal_new ("decide-dest",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarCreateClass, decide_dest),
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_FILE);

  autoar_create_signals[PROGRESS] =
    g_signal_new ("progress",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarCreateClass, progress),
                  NULL, NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_UINT64,
                  G_TYPE_UINT);

  autoar_create_signals[COMPLETED] =
    g_signal_new ("completed",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarCreateClass, completed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  autoar_create_signals[ERROR] =
    g_signal_new ("error",
                  type,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (AutoarCreateClass, error),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_POINTER);

  g_ptr_array_unref (tmparr);
}

static void
autoar_create_init (AutoarCreate *arcreate)
{
  AutoarCreatePrivate *priv;

  priv = AUTOAR_CREATE_GET_PRIVATE (arcreate);
  arcreate->priv = priv;

  priv->source = NULL;
  priv->output = NULL;

  priv->completed_size = 0;

  priv->files = 0;
  priv->completed_files = 0;

  priv->arpref = NULL;

  priv->ostream = NULL;
  priv->buffer_size = BUFFER_SIZE;
  priv->buffer = g_new (char, priv->buffer_size);
  priv->error = NULL;
}

AutoarCreate*
autoar_create_new (const char **source,
                   const char  *output,
                   AutoarPref  *arpref)
{
  AutoarCreate* arcreate;

  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (output != NULL, NULL);

  arcreate = g_object_new (AUTOAR_TYPE_CREATE,
                           "source", source,
                           "output", output,
                           NULL);
  arcreate->priv->arpref = g_object_ref (arpref);

  return arcreate;
}

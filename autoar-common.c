/* vim: set sw=2 ts=2 sts=2 et: */
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * autoar-common.c
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

#include "config.h"

#include "autoar-common.h"

#include <glib.h>
#include <gobject/gvaluecollector.h>
#include <string.h>

typedef struct _AutoarCommonSignalData AutoarCommonSignalData;

struct _AutoarCommonSignalData
{
  GValue instance_and_params[3]; /* Maximum number of parameters + 1 */
  gssize used_values; /* Number of GValues to be unset */
  guint signal_id;
  GQuark detail;
};

char*
autoar_common_get_filename_extension (const char *filename)
{
  char *dot_location;

  dot_location = strrchr (filename, '.');
  if (dot_location == NULL || dot_location == filename) {
    return (char*)filename;
  }

  if (dot_location - 4 > filename && strncmp (dot_location - 4, ".tar", 4) == 0)
    dot_location -= 4;
  else if (dot_location - 5 > filename && strncmp (dot_location - 5, ".cpio", 5) == 0)
    dot_location -= 5;

  return dot_location;
}

char*
autoar_common_get_basename_remove_extension (const char *filename)
{
  char *dot_location;
  char *basename;

  if (filename == NULL) {
    return NULL;
  }

  /* filename must not be directory, so we do not get a bad basename. */
  basename = g_path_get_basename (filename);

  dot_location = autoar_common_get_filename_extension (basename);
  *dot_location = '\0';

  g_debug ("_g_filename_basename_remove_extension: %s => %s",
           filename,
           basename);
  return basename;
}

static void
autoar_common_signal_data_free (AutoarCommonSignalData *signal_data)
{
  int i;

  for (i = 0; i < signal_data->used_values; i++)
    g_value_unset (signal_data->instance_and_params + i);

  g_free (signal_data);
}

static gboolean
autoar_common_g_signal_emit_main_context (void *data)
{
  AutoarCommonSignalData *signal_data = data;
  g_signal_emitv (signal_data->instance_and_params,
                  signal_data->signal_id,
                  signal_data->detail,
                  NULL);
  autoar_common_signal_data_free (signal_data);
  return FALSE;
}

void
autoar_common_g_signal_emit (gboolean in_thread,
                             gpointer instance,
                             guint signal_id,
                             GQuark detail,
                             ...)
{
  va_list ap;

  va_start (ap, detail);
  if (in_thread) {
    int i;
    gchar *error;
    GSignalQuery query;
    AutoarCommonSignalData *data;

    error = NULL;
    data = g_new0 (AutoarCommonSignalData, 1);
    data->signal_id = signal_id;
    data->detail = detail;
    data->used_values = 1;
    g_value_init (data->instance_and_params, G_TYPE_FROM_INSTANCE (instance));
    g_value_set_instance (data->instance_and_params, instance);

    g_signal_query (signal_id, &query);
    if (query.signal_id == 0) {
      autoar_common_signal_data_free (data);
      va_end (ap);
      return;
    }

    for (i = 0; i < query.n_params; i++) {
      G_VALUE_COLLECT_INIT (data->instance_and_params + i + 1,
                            query.param_types[i],
                            ap,
                            0,
                            &error);
      if (error != NULL)
        break;
      data->used_values++;
    }

    if (error == NULL) {
      g_main_context_invoke (NULL, autoar_common_g_signal_emit_main_context, data);
    } else {
      autoar_common_signal_data_free (data);
      g_debug ("G_VALUE_COLLECT_INIT: Error: %s", error);
      g_free (error);
      va_end (ap);
      return;
    }
  } else {
    g_signal_emit_valist (instance, signal_id, detail, ap);
  }
  va_end (ap);
}

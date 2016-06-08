/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/autoar.h>
#include <gio/gio.h>
#include <locale.h>
#include <stdlib.h>

static void
my_handler_scanned (AutoarExtract *arextract,
                    guint files,
                    gpointer data)
{
  g_print ("Scanning OK, %d files to be extracted.\n", files);
}

static GFile*
my_handler_decide_dest (AutoarExtract *arextract,
                        GFile *dest,
                        GList *files,
                        gpointer data)
{
  char *path, *uri;
  GList *l;

  path = g_file_get_path (dest);
  uri = g_file_get_uri (dest);
  g_print ("Destination Path: %s\n", path);
  g_print ("Destination URI: %s\n", uri);
  g_free (path);
  g_free (uri);


  for (l = files; l != NULL; l = l->next) {
    char *pathname;

    pathname = g_file_get_path (l->data);
    g_print ("File: %s\n", pathname);

    g_free (pathname);
  }

  return g_object_ref (dest);
}

static void
my_handler_progress (AutoarExtract *arextract,
                     guint64 completed_size,
                     guint completed_files,
                     gpointer data)
{
  g_print ("\rProgress: Archive Size %.2lf %%, Files %.2lf %%",
           ((double)(completed_size)) * 100 / autoar_extract_get_size (arextract),
           ((double)(completed_files)) * 100 / autoar_extract_get_files (arextract));
}

static AutoarConflictAction
my_handler_conflict (AutoarExtract *arextract,
                     GFile *file,
                     GFile **new_file,
                     gpointer data)
{
  g_autofree char *path;

  path = g_file_get_path (file);

  g_print ("Conflict on: %s\n", path);

  return AUTOAR_CONFLICT_OVERWRITE;
}

static void
my_handler_error (AutoarExtract *arextract,
                  GError *error,
                  gpointer data)
{
  g_printerr ("\nError %d: %s\n", error->code, error->message);
}

static void
my_handler_completed (AutoarExtract *arextract,
                      gpointer data)
{
  g_print ("\nCompleted!\n");
}

int
main (int argc,
      char *argv[])
{
  AutoarExtract *arextract;
  AutoarPref *arpref;
  GSettings *settings;
  char *content;

  if (argc < 3) {
    g_printerr ("Usage: %s archive_file output_dir pattern_to_ignore ...\n",
                argv[0]);
    return 255;
  }

  setlocale (LC_ALL, "");

  content = NULL;
  settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);

  arpref = autoar_pref_new_with_gsettings (settings);
  autoar_pref_set_delete_if_succeed (arpref, FALSE);
  autoar_pref_set_pattern_to_ignore (arpref, (const char**)argv + 3);

  autoar_pref_forget_changes (arpref);
  autoar_pref_write_gsettings (arpref, settings);

  if (g_str_has_suffix (argv[0], "test-extract-memory")) {
    gsize length;
    GFile *file;
    GError *error;

    g_print ("Loading whole file into memory ... ");

    error = NULL;
    file = g_file_new_for_commandline_arg (argv[1]);
    if (!g_file_load_contents (file, NULL, &content, &length, NULL, &error)) {
      g_printerr ("\ntest-extract-memory: Error %d: %s\n", error->code, error->message);
      g_object_unref (file);
      g_error_free (error);
      return 1;
    }

    g_print ("OK\n");
    g_object_unref (file);
    arextract = autoar_extract_new_memory (content, length, argv[1], argv[2], arpref);
  } else {
    arextract = autoar_extract_new (argv[1], argv[2], arpref);
  }

  g_signal_connect (arextract, "scanned", G_CALLBACK (my_handler_scanned), NULL);
  g_signal_connect (arextract, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (arextract, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (arextract, "conflict", G_CALLBACK (my_handler_conflict), NULL);
  g_signal_connect (arextract, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (arextract, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_extract_start (arextract, NULL);

  g_object_unref (arextract);
  g_object_unref (arpref);
  g_object_unref (settings);
  g_free (content);

  return 0;
}

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

static void
my_handler_decide_dest (AutoarExtract *arextract,
                        GFile *dest)
{
  char *path, *uri;
  path = g_file_get_path (dest);
  uri = g_file_get_uri (dest);
  g_print ("Destination Path: %s\n", path);
  g_print ("Destination URI: %s\n", uri);
  g_free (path);
  g_free (uri);
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
  g_autoptr (GFile) source = NULL;
  g_autoptr (GFile) output = NULL;

  if (argc < 3) {
    g_printerr ("Usage: %s archive_file output_dir\n",
                argv[0]);
    return 255;
  }

  setlocale (LC_ALL, "");

  content = NULL;
  settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);

  arpref = autoar_pref_new_with_gsettings (settings);
  autoar_pref_set_delete_if_succeed (arpref, FALSE);

  source = g_file_new_for_commandline_arg (argv[1]);
  output = g_file_new_for_commandline_arg (argv[2]);

  arextract = autoar_extract_new (source, output, arpref);

  g_signal_connect (arextract, "scanned", G_CALLBACK (my_handler_scanned), NULL);
  g_signal_connect (arextract, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (arextract, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (arextract, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (arextract, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_extract_start (arextract, NULL);

  g_object_unref (arextract);
  g_object_unref (arpref);
  g_object_unref (settings);
  g_free (content);

  return 0;
}

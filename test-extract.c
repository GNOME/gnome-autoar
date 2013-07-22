/* vim: set sw=2 ts=2 sts=2 et: */

#include <autoarchive/autoarchive.h>
#include <gio/gio.h>
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
                     gdouble fraction_size,
                     gdouble fraction_files,
                     gpointer data)
{
  g_print ("\rProgress: Archive Size %.2lf %%, Files %.2lf %%",
           fraction_size * 100,
           fraction_files * 100);
}

static void
my_handler_error (AutoarExtract *arextract,
                  GError *error,
                  gpointer data)
{
  g_printerr ("\nError: %s\n", error->message);
  g_error_free (error);
  exit (1);
}

static void
my_handler_completed (AutoarExtract *arextract,
                      gpointer data)
{
  g_print ("\nCompleted!\n");
  exit (0);
}

int
main (int argc,
      char *argv[])
{
  AutoarExtract *arextract;
  AutoarPref *arpref;
  GSettings *settings;
  const char *pattern[] = {
    "__MACOSX",
    ".DS_Store",
    "._.*",
    "*.in",
    NULL
  };

  if (argc < 3) {
    g_printerr ("Usage: %s archive_file output_dir\n", argv[0]);
    return 255;
  }

  settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);

  arpref = autoar_pref_new_with_gsettings (settings);
  autoar_pref_set_delete_if_succeed (arpref, FALSE);
  autoar_pref_set_pattern_to_ignore (arpref, pattern);

  autoar_pref_forget_changes (arpref);
  autoar_pref_write_gsettings (arpref, settings);

  arextract = autoar_extract_new (argv[1], argv[2]);
  g_signal_connect (arextract, "scanned", G_CALLBACK (my_handler_scanned), NULL);
  g_signal_connect (arextract, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (arextract, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (arextract, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (arextract, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_extract_start (arextract, arpref);

  g_object_unref (arextract);
  g_object_unref (arpref);
  g_object_unref (settings);

  return 0;
}

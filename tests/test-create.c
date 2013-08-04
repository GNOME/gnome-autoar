/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/autoar.h>
#include <glib.h>
#include <stdlib.h>

static void
my_handler_decide_dest (AutoarCreate *arcreate,
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
my_handler_progress (AutoarCreate *arcreate,
                     guint64 completed_size,
                     guint completed_files,
                     gpointer data)
{
  g_print ("\rProgress: %"G_GUINT64_FORMAT" bytes, %u files read",
           completed_size,
           completed_files);
}

static void
my_handler_error (AutoarCreate *arcreate,
                  GError *error,
                  gpointer data)
{
  g_printerr ("\nError %d: %s\n", error->code, error->message);
}

static void
my_handler_completed (AutoarCreate *arcreate,
                      gpointer data)
{
  g_print ("\nCompleted!\n");
}

int
main (int argc,
      char* argv[])
{
  AutoarCreate *arcreate;
  AutoarPref *arpref;

  if (argc < 5) {
    g_printerr ("Usage: %s format filter output_dir source ...", argv[0]);
    return 255;
  }

  arpref = autoar_pref_new ();
  autoar_pref_set_default_format (arpref, atoi (argv[1]));
  autoar_pref_set_default_filter (arpref, atoi (argv[2]));

  arcreate = autoar_create_newv (arpref, argv[3], (const char**)argv + 4);
  g_signal_connect (arcreate, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (arcreate, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (arcreate, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (arcreate, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_create_start (arcreate, NULL);

  g_object_unref (arpref);
  g_object_unref (arcreate);

  return 0;
}

/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/autoar.h>
#include <glib.h>
#include <locale.h>
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
  GList *source_files = NULL;
  g_autoptr (GFile) output_file = NULL;
  int i;

  if (argc < 5) {
    g_printerr ("Usage: %s format filter output_dir source ...\n", argv[0]);
    return 255;
  }

  setlocale (LC_ALL, "");

  output_file = g_file_new_for_commandline_arg (argv[3]);

  for (i = 4; i < argc; ++i) {
    source_files = g_list_prepend (source_files,
                                   g_file_new_for_commandline_arg (argv[i]));
  }

  source_files = g_list_reverse (source_files);

  arcreate = autoar_create_new (source_files,
                                output_file,
                                atoi (argv[1]),
                                atoi (argv[2]),
                                TRUE);
  g_signal_connect (arcreate, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (arcreate, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (arcreate, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (arcreate, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_create_start (arcreate, NULL);

  g_list_free_full (source_files, g_object_unref);
  g_object_unref (arcreate);

  return 0;
}

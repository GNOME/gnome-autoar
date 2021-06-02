/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/gnome-autoar.h>
#include <glib.h>
#include <locale.h>
#include <stdlib.h>

static void
my_handler_decide_dest (AutoarCompressor *compressor,
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
my_handler_progress (AutoarCompressor *compressor,
                     guint64 completed_size,
                     guint completed_files,
                     gpointer data)
{
  g_print ("\rProgress: %"G_GUINT64_FORMAT" bytes, %u files read",
           completed_size,
           completed_files);
}

static void
my_handler_error (AutoarCompressor *compressor,
                  GError *error,
                  gpointer data)
{
  g_printerr ("\nError %d: %s\n", error->code, error->message);
}

static void
my_handler_completed (AutoarCompressor *compressor,
                      gpointer data)
{
  g_print ("\nCompleted!\n");
}

int
main (int argc,
      char* argv[])
{
  AutoarCompressor *compressor;
  GList *source_files = NULL;
  g_autoptr (GFile) output_file = NULL;
  int i;

  if (argc < 6) {
    g_printerr ("Usage: %s format filter passphrase output_dir source ...\n", argv[0]);
    return 255;
  }

  setlocale (LC_ALL, "");

  output_file = g_file_new_for_commandline_arg (argv[4]);

  for (i = 5; i < argc; ++i) {
    source_files = g_list_prepend (source_files,
                                   g_file_new_for_commandline_arg (argv[i]));
  }

  source_files = g_list_reverse (source_files);

  compressor = autoar_compressor_new (source_files,
                                      output_file,
                                      atoi (argv[1]),
                                      atoi (argv[2]),
                                      TRUE);
  if (argv[3][0] != '\0')
    autoar_compressor_set_passphrase (compressor, argv[3]);

  g_signal_connect (compressor, "decide-dest", G_CALLBACK (my_handler_decide_dest), NULL);
  g_signal_connect (compressor, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (compressor, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (compressor, "completed", G_CALLBACK (my_handler_completed), NULL);

  autoar_compressor_start (compressor, NULL);

  g_list_free_full (source_files, g_object_unref);
  g_object_unref (compressor);

  return 0;
}

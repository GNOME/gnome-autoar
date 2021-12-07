/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/gnome-autoar.h>
#include <gio/gio.h>
#include <locale.h>
#include <stdlib.h>

static void
my_handler_scanned (AutoarExtractor *extractor,
                    guint files,
                    gpointer data)
{
  g_print ("Scanning OK, %d files to be extracted.\n", files);
}

static GFile*
my_handler_decide_destination (AutoarExtractor *extractor,
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
my_handler_progress (AutoarExtractor *extractor,
                     guint64 completed_size,
                     guint completed_files,
                     gpointer data)
{
  g_print ("\rProgress: Archive Size %.2lf %%, Files %.2lf %%",
           ((double)(completed_size)) * 100 / autoar_extractor_get_total_size (extractor),
           ((double)(completed_files)) * 100 / autoar_extractor_get_total_files (extractor));
}

static AutoarConflictAction
my_handler_conflict (AutoarExtractor *extractor,
                     GFile *file,
                     GFile **new_file,
                     gpointer data)
{
  g_autofree char *path = NULL;

  path = g_file_get_path (file);

  g_print ("Conflict on: %s\n", path);

  return AUTOAR_CONFLICT_UNHANDLED;
}

static void
my_handler_error (AutoarExtractor *extractor,
                  GError *error,
                  gpointer data)
{
  g_printerr ("\nError %d: %s\n", error->code, error->message);
}

static void
my_handler_completed (AutoarExtractor *extractor,
                      gpointer data)
{
  g_print ("\nCompleted!\n");
}

static gchar *
my_handler_request_passphrase (AutoarExtractor *extractor,
                               gpointer data)
{
  const gchar *passphrase = data;

  g_print ("Passphrase requested!\n");

  return g_strdup (passphrase);
}

int
main (int argc,
      char *argv[])
{
  AutoarExtractor *extractor;
  char *content;
  g_autoptr (GFile) source = NULL;
  g_autoptr (GFile) output = NULL;
  g_autofree gchar *passphrase = NULL;

  if (argc < 3 || argc > 4) {
    g_printerr ("Usage: %s archive_file output_dir passphrase\n",
                argv[0]);
    return 255;
  }

  setlocale (LC_ALL, "");

  content = NULL;

  source = g_file_new_for_commandline_arg (argv[1]);
  output = g_file_new_for_commandline_arg (argv[2]);
  if (argc == 4 && argv[3][0] != '\0')
    passphrase = g_strdup (argv[3]);

  extractor = autoar_extractor_new (source, output);

  autoar_extractor_set_delete_after_extraction (extractor, FALSE);

  g_signal_connect (extractor, "scanned", G_CALLBACK (my_handler_scanned), NULL);
  g_signal_connect (extractor, "decide-destination", G_CALLBACK (my_handler_decide_destination), NULL);
  g_signal_connect (extractor, "progress", G_CALLBACK (my_handler_progress), NULL);
  g_signal_connect (extractor, "conflict", G_CALLBACK (my_handler_conflict), NULL);
  g_signal_connect (extractor, "error", G_CALLBACK (my_handler_error), NULL);
  g_signal_connect (extractor, "completed", G_CALLBACK (my_handler_completed), NULL);
  g_signal_connect (extractor, "request-passphrase", G_CALLBACK (my_handler_request_passphrase), passphrase);

  autoar_extractor_start (extractor, NULL);

  g_object_unref (extractor);
  g_free (content);

  return 0;
}

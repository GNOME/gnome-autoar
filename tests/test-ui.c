/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/gnome-autoar.h>
#include <gnome-autoar/autoar-gtk.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

static gboolean
before_deleted (GtkWindow *window,
                GdkEvent *event,
                GtkWidget *simple)
{
  int format;
  int filter;
  char *str;

  autoar_gtk_chooser_simple_get (simple, &format, &filter);
  if (!format || !filter) {
    GtkWidget *warning;
    warning = gtk_message_dialog_new (window,
                                      GTK_DIALOG_MODAL,
                                      GTK_MESSAGE_WARNING,
                                      GTK_BUTTONS_OK,
                                      "Your choice is not valid!");
    gtk_dialog_run (GTK_DIALOG (warning));
    gtk_widget_destroy (warning);

    return TRUE;
  }

  puts (str = autoar_format_filter_get_description (format, filter));
  free (str);
  puts (str = autoar_format_filter_get_extension (format, filter));
  free (str);

  gtk_main_quit();
  return FALSE;
}

int
main (int argc,
      char *argv[])
{
  GtkWidget *window;
  GtkWidget *simple;
  int format, filter;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), argv[0]);

  format = (argc >= 2) ? atoi (argv[1]) : 0;
  filter = (argc >= 3) ? atoi (argv[2]) : 0;

  simple = autoar_gtk_chooser_simple_new (format, filter);
  gtk_container_add (GTK_CONTAINER (window), simple);
  g_signal_connect (window, "delete-event", G_CALLBACK (before_deleted), simple);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}

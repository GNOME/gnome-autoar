/* vim: set sw=2 ts=2 sts=2 et: */

#include <gnome-autoar/autoar.h>

int
main (int argc,
      char *argv[])
{
  AutoarPref *arpref;
  GSettings *settings;
  GFile *file;

  if (argc < 2) {
    g_printerr ("Usage: %s archive_file\n", argv[0]);
    return 255;
  }

  settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);
  arpref = autoar_pref_new_with_gsettings (settings);
  file = g_file_new_for_commandline_arg (argv[1]);

  g_print ("file-name-suffix check: %d, %d\n",
           autoar_pref_check_file_name (arpref, argv[1]),
           autoar_pref_check_file_name_file (arpref, file));
  g_print ("file-mime-type check: %d, %d\n",
           autoar_pref_check_mime_type (arpref, argv[1]),
           autoar_pref_check_mime_type_file (arpref, file));

  g_object_unref (settings);
  g_object_unref (arpref);
  g_object_unref (file);

  return 0;
}

/* vim: set sw=2 ts=2 sts=2 et: */

#define G_LOG_DOMAIN "gnome-autoar-test"

#include <gnome-autoar/autoar.h>

int
main (int argc,
      char *argv[])
{
  if (argc < 2) {
    g_printerr ("Usage: %s archive_file\n", argv[0]);
    return 255;
  }

  g_autoptr (GSettings) settings = g_settings_new (AUTOAR_PREF_DEFAULT_GSCHEMA_ID);
  g_autoptr (AutoarPref) arpref = autoar_pref_new_with_gsettings (settings);
  g_autoptr (GFile) file file = g_file_new_for_commandline_arg (argv[1]);

  g_print ("file-name-suffix check: %d, %d\n",
           autoar_pref_check_file_name (arpref, argv[1]),
           autoar_pref_check_file_name_file (arpref, file));
  g_print ("file-mime-type check: %d, %d\n",
           autoar_pref_check_mime_type (arpref, argv[1]),
           autoar_pref_check_mime_type_file (arpref, file));

  return 0;
}

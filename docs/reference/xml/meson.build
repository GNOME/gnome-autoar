# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

ent_conf = {
  'PACKAGE': gnome_autoar_name,
  'PACKAGE_BUGREPORT': 'https://gitlab.gnome.org/GNOME/@0@'.format(gnome_autoar_name),
  'PACKAGE_NAME': gnome_autoar_name,
  'PACKAGE_STRING': '@0@ @1@'.format(gnome_autoar_name, gnome_autoar_version),
  'PACKAGE_TARNAME': gnome_autoar_name,
  'PACKAGE_URL': '',
  'PACKAGE_VERSION': gnome_autoar_version,
}

gtkdocentities_ent = configure_file(
  input: 'gtkdocentities.ent.in',
  output: '@BASENAME@',
  configuration: ent_conf,
)

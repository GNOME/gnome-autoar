# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

subdir('xml')

private_headers = [
  'autoar-gtk.h',
  'autoar-private.h',
  'gnome-autoar.h',
]

gnome.gtkdoc(
  gnome_autoar_name,
  main_xml: f'@gnome_autoar_name@-docs.xml',
  src_dir: src_inc,
  dependencies: libgnome_autoar_dep,
  ignore_headers: private_headers,
  fixxref_args: '--html-dir=' + (gnome_autoar_prefix / gnome.gtkdoc_html_dir(gnome_autoar_name)),
  install: true,
)

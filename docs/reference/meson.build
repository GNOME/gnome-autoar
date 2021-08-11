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
  main_xml: '@0@-docs.xml'.format(gnome_autoar_name),
  src_dir: src_inc,
  dependencies: libgnome_autoar_dep,
  scan_args: '--rebuild-types',
  gobject_typesfile: '@0@.types'.format(gnome_autoar_name),
  ignore_headers: private_headers,
  fixxref_args: '--html-dir=' + (gnome_autoar_prefix / gnome.gtkdoc_html_dir(gnome_autoar_name)),
  install: true,
)

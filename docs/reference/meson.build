# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021, 2022 Iñigo Martinez <inigomartinez@gmail.com>

enable_gtk_doc = get_option('gtk_doc')
gi_docgen = find_program('gi-docgen', required: enable_gtk_doc)

if enable_gtk_doc and gi_docgen.found()
  toml_conf = {
    'license': ','.join(meson.project_license()),
    'version': gnome_autoar_version,
  }

  toml = configure_file(
    input: 'gnome-autoar.toml.in',
    output: '@BASENAME@',
    configuration: toml_conf,
  )

  custom_target(
    'gnome-autoar-doc',
    input: [
      toml,
      libgnome_autoar_gir,
    ],
    output: 'gnome-autoar',
    command: [
      gi_docgen,
      'generate',
      '--quiet',
      '--no-namespace-dir',
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    build_by_default: true,
    install: true,
    install_dir: gnome_autoar_docdir,
  )
endif
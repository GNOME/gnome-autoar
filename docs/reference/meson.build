toml_data = configuration_data()
toml_data.set('VERSION', meson.project_version())

gnome_autoar_toml = configure_file(
  input: 'gnome-autoar.toml.in',
  output: 'gnome-autoar.toml',
  configuration: toml_data,
)

custom_target(
  'gnome-autoar-doc',
  input: [
    libgnome_autoar_gir[0],
    gnome_autoar_toml,
  ],
  output: 'gnome-autoar',
  command: [
    gi_docgen,
    'generate',
    '--quiet',
    '--add-include-path=@0@'.format(join_paths(meson.project_source_root(), 'gnome-autoar')),
    '--config=@INPUT1@',
    '--output-dir=@OUTPUT@',
    '--no-namespace-dir',
    '--content-dir=@0@'.format(meson.current_source_dir()),
    '@INPUT0@',
  ],
  build_by_default: true,
  install: true,
  install_dir: gnome_autoar_docdir,
)

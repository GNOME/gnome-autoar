# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

src_inc = include_directories('.')

libname = f'@gnome_autoar_name@-@gnome_autoar_api_version@'

headers = files(
  'autoar-compressor.h',
  'autoar-extractor.h',
  'autoar-format-filter.h',
  'autoar-mime-types.h',
  'autoar-misc.h',
)

install_headers(
  headers + files('gnome-autoar.h'),
  install_dir: gnome_autoar_includedir / libname / gnome_autoar_name,
)

sources = files(
  'autoar-compressor.c',
  'autoar-extractor.c',
  'autoar-format-filter.c',
  'autoar-mime-types.c',
  'autoar-misc.c',
)

enum_types = 'autoar-enum-types'

enum_sources = gnome.mkenums(
  enum_types,
  sources: headers,
  h_template: f'@enum_types@.h.template',
  c_template: f'@enum_types@.c.template',
  install_header: true,
  install_dir: gnome_autoar_includedir / libname / gnome_autoar_name,
)

deps = [
  gio_dep,
  glib_dep,
  libarchive_dep,
]

config_h = configuration_data()

# functions
check_functions = [
  'getgrnam',
  'getpwnam',
  'link',
  'mkfifo',
  'mknod',
  'stat',
]

foreach func: check_functions
  config_h.set('HAVE_' + func.to_upper(), cc.has_function(func))
endforeach

configure_file(
  output: 'config.h',
  configuration: config_h,
)

libgnome_autoar = shared_library(
  libname,
  version: gnome_autoar_libversion,
  sources: sources + enum_sources + files('autoar-private.c'),
  include_directories: top_inc,
  dependencies: deps,
  install: true,
)

libgnome_autoar_dep = declare_dependency(
  sources: enum_sources[1],
  include_directories: src_inc,
  dependencies: deps,
  link_with: libgnome_autoar,
)

pkg.generate(
  libraries: libgnome_autoar,
  name: gnome_autoar_name,
  description: 'Archives integration support for GNOME',
  filebase: libname,
  subdirs: libname,
  requires: deps,
  variables: 'exec_prefix=${prefix}',
)

if enable_introspection
  api_ns = 'GnomeAutoar'

  incs = [
    'Gio-2.0',
    'GLib-2.0',
    'GObject-2.0',
  ]

  libgnome_autoar_gir = gnome.generate_gir(
    libgnome_autoar,
    sources: sources + headers,
    includes: incs,
    header: 'gnome-autoar/gnome-autoar.h',
    namespace: api_ns,
    nsversion: gnome_autoar_api_ns_version,
    identifier_prefix: gnome_autoar_api_prefix,
    export_packages: f'@api_ns@-@gnome_autoar_api_ns_version@',
    install: true,
  )

  if enable_vapi
    libgnome_autoar_vapi = gnome.generate_vapi(
      libname,
      sources: libgnome_autoar_gir[0],
      packages: 'gio-2.0',
      install: true,
    )
  endif
endif

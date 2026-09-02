# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

project(
  'gnome-autoar', 'c',
  version: '0.5.2',
  license: 'LGPL2.1+',
  default_options: ['buildtype=debugoptimized'],
  meson_version: '>= 1.8.0',
)

gnome_autoar_name = meson.project_name()

gnome_autoar_version = meson.project_version()
version_array = gnome_autoar_version.split('.')
gnome_autoar_major_version = version_array[0].to_int()
gnome_autoar_minor_version = version_array[1].to_int()
gnome_autoar_micro_version = version_array[2].to_int()

# Before making a release, the libversion string should be modified.
#
#  * Bump the first component if binary compatibility has been broken; or
#  * Bump the second component if new APIs are added; or
#  * Bump the third component otherwise.
#
# When bumping the first component version, set the second and third components
# to 0. When bumping the second version, set the third one to zero.
gnome_autoar_libversion = '0.2.1'

gnome_autoar_api_version = 0

gnome_autoar_api_prefix = 'Autoar'
gnome_autoar_api_ns_version = '0.1'

gnome_autoar_prefix = get_option('prefix')
gnome_autoar_datadir = get_option('datadir')
gnome_autoar_includedir = get_option('includedir')
gnome_autoar_docdir = gnome_autoar_datadir / 'doc'

gnome = import('gnome')
pkg = import('pkgconfig')

source_root = meson.current_source_dir()

top_inc = include_directories('.')

cc = meson.get_compiler('c')

compiler_flags = []
if get_option('buildtype').contains('debug')
  compiler_flags += cc.get_supported_arguments([
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-prototypes',
    '-Werror=missing-include-dirs',
    '-Werror=pointer-arith',
    '-Werror=return-type',
    '-Wnested-externs',
    '-Wstrict-prototypes',
  ])
endif

add_project_arguments(compiler_flags, language: 'c')

glib_req_version = '>= 2.35.6'
gio_dep = dependency('gio-2.0', version: glib_req_version)
glib_dep = dependency('glib-2.0', version: glib_req_version)
gobject_dep = dependency('gobject-2.0', version: glib_req_version)

libarchive_dep = dependency('libarchive', version: '>= 3.4.0')
if not libarchive_dep.found()
  libarchive_dep = cc.find_library('archive')
  cc.has_function('archive_entry_is_encrypted', dependencies: libarchive_dep)
endif

enable_introspection = dependency('gobject-introspection-1.0', version: '>= 1.30.0', required: get_option('introspection')).found()

enable_vapi = get_option('vapi')
assert(not enable_vapi or enable_introspection, 'GObject introspection support must be enabled to build VALA bindings')

subdir('gnome-autoar')

enable_tests = get_option('tests')
if enable_tests
  subdir('tests')
endif

subdir('docs/reference')

summary(
  {
    'Source code location': source_root,
    'Prefix': gnome_autoar_prefix,
    'Compiler': cc.get_id(),
    'CFLAGS': compiler_flags,
  },
  section: 'Configuration',
)

summary(
  {
    'Build API documentation': enable_docs,
    'Introspection': enable_introspection,
    'VALA bindings': enable_vapi,
    'Tests': enable_tests,
  },
  section: 'Optional features',
  bool_yn: true,
)

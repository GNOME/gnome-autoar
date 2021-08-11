# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2021 Iñigo Martinez <inigomartinez@gmail.com>

test_units = [
  ['test-extract', libgnome_autoar_dep, false],
  ['test-extract-unit', libgnome_autoar_dep, true],
  ['test-create', libgnome_autoar_dep, false],
]

if enable_gtk
  test_units += [['test-ui', libgnome_autoar_gtk_dep, false]]
endif

foreach test_unit: test_units
  exe = executable(
    test_unit[0],
    test_unit[0] + '.c',
    include_directories: top_inc,
    dependencies: test_unit[1],
  )

  if test_unit[2]
    test(
      test_unit[0],
      exe,
      env : ['G_TEST_SRCDIR=@0@'.format(source_root)]
    )
  endif
endforeach

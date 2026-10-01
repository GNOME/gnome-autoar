/*
 * SPDX-FileCopyrightText: 2014  Ting-Wei Lan
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef AUTOAR_MISC_H
#define AUTOAR_MISC_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * AUTOAR_LIBARCHIVE_ERROR:
 *
 * Error domain for libarchive. Error returned by functions in libarchive uses
 * this domain. Error code and messages are got using archive_errno() and
 * archive_error_string() functions provided by libarchive.
 **/

#define AUTOAR_LIBARCHIVE_ERROR autoar_libarchive_quark()

GQuark    autoar_libarchive_quark                      (void);

G_END_DECLS

#endif /* AUTOAR_COMMON_H */

/*
 * SPDX-FileCopyrightText: 2014  Ting-Wei Lan
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "gnome-autoar"

#include "config.h"
#include "autoar-misc.h"

#include <glib.h>

/**
 * SECTION:autoar-misc
 * @Short_description: Miscellaneous functions and shared data types used
 *  by gnome-autoar
 * @Title: autoar-misc
 * @Include: gnome-autoar/autoar.h
 *
 * Public utility functions and data types used by gnome-autoar;
 **/

/**
 * autoar_libarchive_quark:
 *
 * Gets the libarchive Error Quark.
 *
 * Returns: a #GQuark.
 **/
G_DEFINE_QUARK (libarchive-quark, autoar_libarchive)

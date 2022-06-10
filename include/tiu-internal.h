/*  Copyright (C) 2021  Thorsten Kukuk <kukuk@suse.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <glib.h>
#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG "/var/log/tiu/"

/* Default maximum downloadable bundle size (800 MiB) */
#define DEFAULT_MAX_DOWNLOAD_SIZE 800*1024*1024

extern gboolean verbose_flag;
extern gboolean debug_flag;

extern gboolean workdir_setup (const gchar *contentpath, const gchar **workdir, GError **error);
extern gboolean workdir_destroy (const gchar *workdir, GError **error);
extern gboolean rm_rf (GFile *file, GCancellable *cancellable, GError **error);
extern gboolean rmdir_rf (const gchar *dir, GCancellable *cancellable, GError **error);

#ifdef __cplusplus
}
#endif

/*  Copyright (C) 2022  Thorsten Kukuk <kukuk@suse.com>

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

#ifdef __cplusplus
extern "C" {
#endif

extern gboolean btrfs_set_readonly (const gchar *path, gboolean ro, GError **error);
extern gboolean btrfs_get_subvolume_id (const gchar *snapshot_dir, const gchar *mountpoint, gchar **output, GError **error);
extern gboolean btrfs_set_default (const gchar *btrfs_id, const gchar *path, GError **error);

#ifdef __cplusplus
}
#endif

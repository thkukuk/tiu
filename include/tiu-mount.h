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

#define TIU_ROOT_DIR "/var/lib/tiu/root"

extern gboolean bind_mount (const gchar *source, const gchar *target, const gchar *dir, GError **error);
extern gboolean setup_chroot (const gchar *target, GError **error);
extern gboolean umount_chroot (const gchar *target, gboolean force, GError **error);

#ifdef __cplusplus
}
#endif

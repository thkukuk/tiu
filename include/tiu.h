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

extern gboolean extract_image(const gchar *archive, const gchar *outputdir, GError **error);
extern gboolean install_system (const gchar *archive, const gchar *device,
		                const gchar *disk_layout, GError **error);
extern gboolean update_system (const gchar *archive, GError **error);
extern gboolean download_archive (const gchar *archive, const gchar *archive_md5sum,
				  gchar **location, GError **error);

#ifdef __cplusplus
}
#endif

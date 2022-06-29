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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <glib.h>
#include <glib/gfileutils.h>

#include "tiu-internal.h"

gboolean
create_etc_hwrevision (const gchar *sysroot, GError **error)
{
  GError *ierror = NULL;
  struct utsname uname_data;
  gchar *content = NULL;
  gboolean retval = TRUE;
  gchar *filename = NULL;

  /* XXX solve this better, error checking, freeing memory, ... */
  if (sysroot == NULL)
    filename = "/etc/hwrevision";
  else
    {
      if (asprintf (&filename, "%s/etc/hwrevision", sysroot) < 0)
	{
	  /* g_set_error for out of memory? */
	  retval = FALSE;
	  goto cleanup;
	}
    }

  uname(&uname_data); /* XXX error checking */

  if (asprintf (&content, "arch %s", uname_data.machine) < 0)
    {
      /* g_set_error for out of memory? */
      retval = FALSE;
      goto cleanup;
    }

  if (!g_file_set_contents (filename, content, -1, &ierror))
    {
      g_propagate_error (error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  cleanup:
  if (content)
    free (content);

  return retval;
}

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

#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>

#include "tiu-internal.h"
#include "tiu-mount.h"

static char *devices[] = {"/dev", "/proc", "/sys"};

gboolean
bind_mount (const gchar *source, const gchar *target, const gchar *dir,
	    GError **error)
{
  gboolean retval = TRUE;
  gchar *target_path = NULL;

  if (dir != NULL)
    target_path = g_strjoin("/", target, dir, NULL);
  else
    target_path = g_strjoin("", target, source, NULL);

  if (mount(source, target_path, "bind", MS_BIND|MS_REC, NULL) < 0)
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                  "failed to bind mount %s on %s: %s",
		  source, target_path, g_strerror(err));
      retval = FALSE;
    }
  else if (mount(source, target_path, "bind", MS_REC|MS_SLAVE, NULL) < 0)
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                  "failed to make mount %s a slave: %s",
		  target_path, g_strerror(err));
      retval = FALSE;
    }

  free (target_path);

  return retval;
}

gboolean
umount_chroot (const gchar *target, gboolean force, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  gboolean retval = TRUE;

  if (debug_flag)
    printf ("Umount %s...\n", target);

  GPtrArray *args = g_ptr_array_new_full(4, NULL);
  g_ptr_array_add(args, "umount");
  g_ptr_array_add(args, "-R");
  g_ptr_array_add(args, strdup(target));
  g_ptr_array_add(args, NULL);
  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      if (!force)
	{
	  g_propagate_prefixed_error(error, ierror,
				     "Failed to umount chroot environment: ");
	  retval = FALSE;
	}
      goto cleanup;
    }
  else if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      if (!force)
	{
	  g_propagate_prefixed_error(error, ierror,
				     "Failed to umount chroot environment: ");
	  retval = FALSE;
	}
      goto cleanup;
    }

 cleanup:
  g_ptr_array_free(args, TRUE);
  return retval;
}

gboolean
setup_chroot (const gchar *target, GError **error)
{
  GError *ierror = NULL;

  for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++)
    {
      if (debug_flag)
	printf ("Mount %s into %s...\n", devices[i], target);

      if (!bind_mount(devices[i], target, NULL, &ierror))
	{
	  g_propagate_error(error, ierror);
	  umount_chroot (target, TRUE, &ierror);
	  return FALSE;
	}
    }

  /* bind mount the whole stuff below /var/lib/tiu to make
     grub2-mkconfig working */
  if (!g_file_test(TIU_ROOT_DIR, G_FILE_TEST_IS_DIR))
    {
      gint ret;
      ret = g_mkdir_with_parents(TIU_ROOT_DIR, 0700);

      if (ret != 0)
        {
          g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating mount path '%s'", TIU_ROOT_DIR);
          return FALSE;
        }
    }

  if (!bind_mount(target, TIU_ROOT_DIR, "", &ierror))
    {
      g_propagate_error(error, ierror);
      umount_chroot (target, TRUE, &ierror);
      return FALSE;
    }

  return TRUE;
}

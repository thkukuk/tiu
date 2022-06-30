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
#include <fcntl.h>
#include <mntent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <glib/gprintf.h>
#include <libeconf.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "tiu-swupdate.h"
#include "tiu-mount.h"

static gboolean
update_kernel (gchar *chroot, gchar *partition, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating kernel in %s/boot/%s...\n", chroot?chroot:"", partition);


  if (chroot)
    {
      g_ptr_array_add(args, "chroot");
      g_ptr_array_add(args, chroot);
    }
  g_ptr_array_add(args, "/usr/libexec/tiu/update-kernel");
  g_ptr_array_add(args, &partition[4]);
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to start update-kernel sub-process: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute update-kernel sub-process: ");
      retval = FALSE;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);

  return retval;
}

#if 0
static gboolean
update_bootloader (gchar *chroot, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating bootloader in %s...\n", chroot?chroot:"/");

  if (chroot)
    {
      g_ptr_array_add(args, "chroot");
      g_ptr_array_add(args, chroot);
    }
  g_ptr_array_add(args, "/usr/sbin/update-bootloader");
  g_ptr_array_add(args, "--reinit");
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to start sub-process: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute sub-process: ");
      retval = FALSE;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);

  return retval;
}
#endif

static gboolean
set_default_partition (gint menuentry_id, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;
  gchar *entry = NULL;

  if (debug_flag)
    g_printf("Updating default boot entry to %i...\n", menuentry_id);

  if (asprintf (&entry, "saved_entry=%i", menuentry_id) < 0)
    {
      /* XXX set usefull out of memory error message */
      return FALSE;
    }

  g_ptr_array_add(args, "/usr/bin/grub2-editenv");
  g_ptr_array_add(args, "-");
  g_ptr_array_add(args, "set");
  g_ptr_array_add(args, entry);
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to start grub2-editenv: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute grub2-editenv: ");
      retval = FALSE;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);

  return retval;
}

/* XXX find a more robust way for this */
static gchar *
map_dev_to_partlabel (gchar *device, GError **error)
{
  gchar *retval = NULL;
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);

  if (debug_flag)
    g_printf("Get partition label for '/dev%s'...\n", device);

  gchar *sharg = g_strjoin (NULL, "/bin/ls -l /dev/disk/by-partlabel/ |grep ",
			    device, "$ |sed -e 's|.*\\(USR_[A-Z]\\).*|\\1|g'", NULL);

  g_ptr_array_add(args, "/bin/sh");
  g_ptr_array_add(args, "-c");
  g_ptr_array_add(args, sharg);
  g_ptr_array_add(args, NULL);

    sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start getting partition label: ");
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute getting partition label: ");
      goto cleanup;
    }

  gchar *stdout;
  gchar *stderr;
  if (!g_subprocess_communicate_utf8 (sproc, NULL, NULL, &stdout, &stderr, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to read stdout to get partition label: ");
      goto cleanup;

    }

  if (stdout)
    {
      /* Remove trailing "\n" from output */
      int len = strlen(stdout);
      if (stdout[len - 1] == '\n')
        stdout[len - 1] = '\0';
      retval = g_strdup(stdout);
    }
  else
    {
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(EPIPE),
                  "Failed to read stdout to get partition label:");
      retval = FALSE;
      goto cleanup;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);
  if (sharg)
    free (sharg);

  return retval;
}

static gchar *
get_next_partition (GError **error)
{
  struct mntent *ent;
  FILE *f;
  GError *ierror = NULL;
  char *curr_dev = NULL;

  f = setmntent ("/proc/mounts", "r");
  if (f == NULL)
    {
      int err = errno;
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno(err),
		   "Failed to open /proc/mounts: %s", g_strerror(err));
      return FALSE;
    }

  while (NULL != (ent = getmntent (f)))
    {
      if (strcmp (ent->mnt_dir, "/usr") == 0)
	{
	  curr_dev = map_dev_to_partlabel (&(ent->mnt_fsname)[4], &ierror);
	  if (curr_dev == NULL)
	    {
	      g_propagate_error(error, ierror);
	      return NULL;
	    }
	  break;
	}
    }

  endmntent(f);

  if (curr_dev == NULL)
    {
      /* XXX set error that we haven't found the device */
      return NULL;
    }

  if (debug_flag)
    printf ("Found current partition label for /usr: %s\n", curr_dev);


  gchar *next_dev = g_strdup (curr_dev);

  /* USR_A -> USR_B, ... */
  next_dev[strlen(next_dev)-1]++;

  gchar *test_dev = g_strjoin("/", "/dev/disk/by-partlabel", next_dev, NULL);

  if (g_file_test(test_dev, G_FILE_TEST_EXISTS))
    {
      free (test_dev);
    }
  else
    {
      /* so next partition does not exist, so start by USR_A again */
      next_dev[strlen(next_dev)-1] = 'A';

      if (strcmp (curr_dev, next_dev) == 0)
	{
	  g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		      "No free partition found!");
	  free (curr_dev);
	  free (next_dev);
	  return NULL;
	}
    }

  free (curr_dev);
  return next_dev;
}

static gchar *
internal_update_system_pre (GError **error)
{
  GError *ierror = NULL;
  gchar *next_partlabel = NULL;
  gchar *retval = NULL;

  if ((next_partlabel = get_next_partition(&ierror)) == NULL)
    {
      if (ierror != NULL)
	g_propagate_error(error, ierror);
      return NULL;
    }

  /* we have at minimum two partitions A/B to switch between.
     /dev/update-image-usr should be a symlink to the next free partition. */
  remove("/dev/update-image-usr");
  gchar *device = g_strjoin("/", "/dev/disk/by-partlabel", next_partlabel, NULL);
  if (symlink (device, "/dev/update-image-usr"))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                  "failed to create symlink: %s", g_strerror(err));

      retval = NULL;
    }
  else
    retval = g_strdup (next_partlabel);

  free (device);
  if (next_partlabel)
    free (next_partlabel);

  return retval;
}

static gboolean
internal_update_system_post (gchar *next_partlabel, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;

  const gchar *mountpoint = "/var/lib/tiu/mount";
  if (!g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
    {
      gint ret;
      ret = g_mkdir_with_parents(mountpoint, 0700);

      if (ret != 0)
        {
          g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating mount path '%s'", mountpoint);
	  retval = FALSE;
	  goto cleanup;
        }
    }

  if (!update_kernel (NULL, next_partlabel, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

#if 0
  if (!update_bootloader (NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }
#endif

  gint menuentry_id = next_partlabel[4] - 'A';
  if (!set_default_partition (menuentry_id, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

 cleanup:
  remove("/dev/update-image-usr");

  if (umount2 (mountpoint, UMOUNT_NOFOLLOW))
    /* XXX print error in cleanup really helpful? Will it overwrite original error? */
#if 0
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to umount %s: %s", mountpoint, g_strerror(err));
    }
#else
    {}
#endif

  return retval;
}

gboolean
update_system_pre (GError **error)
{
  GError *ierror = NULL;

  if (internal_update_system_pre (&ierror) == NULL)
    {
      g_propagate_error (error, ierror);
      return FALSE;
    }

  return TRUE;
}

gboolean
update_system_post (GError **error)
{
  GError *ierror = NULL;
  gchar *next_partlabel = NULL;

  if ((next_partlabel = get_next_partition(&ierror)) == NULL)
    {
      if (ierror != NULL)
	g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!internal_update_system_post (next_partlabel, &ierror))
    {
      g_propagate_error (error, ierror);
      return FALSE;
    }

  return TRUE;
}

gboolean
update_system (const gchar *archive, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;
  gchar *next_partlabel = NULL;

  if (verbose_flag)
    g_printf("Update /usr...\n");

  if ((next_partlabel = internal_update_system_pre (&ierror)) == NULL)
    {
      g_propagate_error (error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (verbose_flag)
    g_printf ("Write new image to partition '%s'\n", next_partlabel);

  if (debug_flag)
    g_printf("Calling swupdate...\n");

  if (!swupdate_deploy (archive, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!internal_update_system_post (next_partlabel, &ierror))
    {
      g_propagate_error (error, ierror);
      return FALSE;
    }

 cleanup:
  if (next_partlabel)
    free (next_partlabel);

  return retval;
}

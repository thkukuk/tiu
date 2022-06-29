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

#if 0 /* XXX */

#include "tiu-btrfs.h"

static gboolean
snapper_create (const gchar *config, gchar **output, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (debug_flag)
    g_printf("Running snapper create for '%s'...\n",
	     config?config:"root");

  g_ptr_array_add(args, "snapper");
  if (config)
    {
      g_ptr_array_add(args, "-c");
      g_ptr_array_add(args, g_strdup(config));
    }
  g_ptr_array_add(args, "create");
  g_ptr_array_add(args, "--print-number");
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start snapper: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute snapper: ");
      return FALSE;
    }

  gchar *stdout;
  gchar *stderr;
  if (!g_subprocess_communicate_utf8 (sproc, NULL, NULL, &stdout, &stderr, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to read stdout: ");
      return FALSE;
    }

  if (stdout)
    {
      /* Remove trailing "\n" from output */
      int len = strlen(stdout);
      if (stdout[len - 1] == '\n')
	stdout[len - 1] = '\0';
      *output = g_strdup(stdout);
    }
  else
    {
      *output = NULL;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(EPIPE),
		  "Failed to read stdout");
      return FALSE;
    }

  return TRUE;
}

static gboolean
adjust_etc_fstab_usr_btrfs (const gchar *path, const gchar *snapshot_usr, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  gchar *fstab = g_strjoin("/", path, "etc/fstab", NULL);

  if (debug_flag)
    g_printf("Adjusting '%s'...\n", fstab);

  gchar *sedarg = g_strjoin(NULL, "s|subvol=/.snapshots/.*/snapshot|subvol=/.snapshots/",
			    snapshot_usr, "/snapshot|g", NULL);

  g_ptr_array_add(args, "sed");
  g_ptr_array_add(args, "-i");
  g_ptr_array_add(args, "-e");
  g_ptr_array_add(args, sedarg);
  g_ptr_array_add(args, fstab);
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start sed on /etc/fstab: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute sed on /etc/fstab: ");
      retval = FALSE;
      goto cleanup;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);
  g_free (sedarg);
  g_free (fstab);

  return retval;
}

static gboolean
mount_boot_efi (gchar *target, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);

  if (debug_flag)
    g_printf("Mount /boot/efi on %s/boot/efi...\n", target);

  g_ptr_array_add(args, "chroot");
  g_ptr_array_add(args, target);
  g_ptr_array_add(args, "mount");
  g_ptr_array_add(args, "/boot/efi");
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to run mount /boot/efi: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute mount /boot/efi: ");
      return FALSE;
    }

  return TRUE;
}

#endif

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
  g_ptr_array_add(args, partition);
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

  /* XXXX create next partition */

  return curr_dev;
}

gboolean
update_system (const gchar *archive, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;
  gchar *device = NULL;

  if (verbose_flag)
    g_printf("Update /usr...\n");

  if ((device = get_next_partition(&ierror)) == NULL)
    {
      if (ierror != NULL)
	g_propagate_error(error, ierror);
      return FALSE;
    }

  /* we have at minimum two partitions A/B to switch between.
     /dev/update-image-usr should be a symlink to the next free partition. */
  remove("/dev/update-image-usr");
  /* XXXX find correct partlabel */
  if (symlink("/dev/disk/by-partlabel/USR_B", "/dev/update-image-usr"))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
                  "failed to create symlink: %s", g_strerror(err));
      retval = FALSE;
      goto cleanup;
    }

  if (debug_flag)
    g_printf("Calling swupdate...\n");

  if (!swupdate_deploy (archive, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  remove("/dev/update-image-usr");

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

  /* touch /usr for systemd */
  utimensat(AT_FDCWD, mountpoint, NULL, 0);

  if (!update_kernel (NULL, "B", &ierror))
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

 cleanup:
  if (device)
    free (device);

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

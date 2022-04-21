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
#include "tiu-mount.h"
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

  gchar *sedarg = g_strjoin(NULL, "s|^/sysroot/os/.snapshots/.*/snapshot|/sysroot/os/.snapshots/",
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

static gboolean
update_kernel (gchar *chroot, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating kernel in %s/boot...\n", chroot?chroot:"");


  if (chroot)
    {
      g_ptr_array_add(args, "chroot");
      g_ptr_array_add(args, chroot);
    }
  g_ptr_array_add(args, "/usr/libexec/tiu/update-kernel");
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

static gboolean
update_bootloader (gchar *chroot, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating bootloader in %s...\n", chroot?chroot:"");

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

static gboolean
update_system_usr_btrfs (TIUBundle *bundle, const gchar *store, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;
  gchar *snapshot_root = NULL;
  gchar *snapshot_usr = NULL;
  gchar *subvol_id = NULL;

  if (verbose_flag)
    g_printf("Update /usr with btrfs snapshots...\n");

  /* Create at first the root snapshot, so that we can mount /usr
     later on it to update the kernel */
  if (!snapper_create (NULL, &snapshot_root, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  gchar *snapshot_root_path = g_strjoin("/", "/.snapshots",
					snapshot_root, "snapshot", NULL);
  if (!btrfs_set_readonly (snapshot_root_path, false, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }


  if (!snapper_create ("usr", &snapshot_usr, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  gchar *usr_snapshot_path = g_strjoin("/", "/os/.snapshots",
				       snapshot_usr, "snapshot", NULL);
  if (!btrfs_set_readonly (usr_snapshot_path, false, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  gchar *usr_path = g_strjoin("/", usr_snapshot_path, "usr", NULL);
  if (!extract_image(bundle, usr_path, store, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  /* touch /usr for systemd */
  utimensat(AT_FDCWD, usr_path, NULL, 0);

  /* make /usr snapshot readonly again */
  if (!btrfs_set_readonly (usr_snapshot_path, true, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!adjust_etc_fstab_usr_btrfs (snapshot_root_path, snapshot_usr, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!setup_chroot (snapshot_root_path, TIU_ROOT_DIR, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!bind_mount(usr_path, TIU_ROOT_DIR, "usr", &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!mount_boot_efi (TIU_ROOT_DIR, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  /* XXX Mount /var inside snapshot, so that we can write the log files.
     Needs a better way to handle the log files */
  if (!bind_mount ("/var", TIU_ROOT_DIR, NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!update_kernel (TIU_ROOT_DIR, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!update_bootloader (TIU_ROOT_DIR, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!btrfs_get_subvolume_id (snapshot_root_path, "/.snapshots",
			       &subvol_id, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!btrfs_set_default (subvol_id, snapshot_root_path, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

 cleanup:
  if (ierror != NULL)
    umount_chroot(TIU_ROOT_DIR, TRUE, NULL);
  else
    umount_chroot(TIU_ROOT_DIR, FALSE, &ierror);

  if (snapshot_usr)
    free (snapshot_usr);
  if (snapshot_root)
    free (snapshot_root);
  if (subvol_id)
    free (subvol_id);

  return retval;
}

static gchar *
get_next_partition (GError **error)
{
  struct mntent *ent;
  FILE *f;
  char *new_dev = NULL;

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
	  printf("%s %s\n", ent->mnt_fsname, ent->mnt_dir);

	  new_dev = malloc (strlen (ent->mnt_fsname) + 2); /* +2: one for '\0', one if we go from 9->10 */
	  /* XXX check for failed malloc */
	  strcpy (new_dev, ent->mnt_fsname);
	  break;
	}
    }

  endmntent(f);

  if (new_dev == NULL)
    {
      /* XXX set error that we haven't found the device */
      return NULL;
    }

  size_t len = strlen (new_dev);
  char *cp = &new_dev[len];
  for (size_t i = 1; i < len; i++)
    {
      if (new_dev[len - i] >= '0' && new_dev[len - i] <= '9')
	cp--;
      else
	break;
    }
  /* XXX use strtoul */
  int part_nr = atoi (cp);

  if (part_nr < 1)
    /* XXX error message */
    return NULL;

  ++part_nr;
  /* XXX make min and max partition number configureable */
  if (part_nr == 5)
    part_nr = 3;

  *cp = '\0';
  sprintf (new_dev, "%s%i", new_dev, part_nr);

  return new_dev;
}

static gboolean
update_system_usr_AB (TIUBundle *bundle __attribute__((unused)),
		      const gchar *store, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;
  gchar *snapshot_root = NULL;
  gchar *subvol_id = NULL;
  gchar *device = NULL;

  if (verbose_flag)
    g_printf("Update /usr with A/B partitions...\n");

  if (!snapper_create (NULL, &snapshot_root, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  device = get_next_partition(&ierror); /* XXX make sure device get free'd */
  if (device == NULL)
    {
      if (ierror != NULL)
	g_propagate_error(error, ierror);
      return FALSE;
    }

  const gchar *mountpoint = "/var/lib/tiu/mount";
  if (!g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
    {
      gint ret;
      ret = g_mkdir_with_parents(mountpoint, 0700);

      if (ret != 0)
        {
          g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating mount path '%s'", mountpoint);
          return FALSE;
        }
    }

  if (debug_flag)
    g_printf("Mounting partition '%s' to '%s'\n", device, mountpoint);

  /* XXX we should have an entry in the config defining the used filesystem type.
     So that we could e.g. use ext4 instead of xfs */
  if (mount(device, mountpoint, "xfs", 0, NULL))
    {
      int err = errno;
      g_set_error(&ierror, G_FILE_ERROR, g_file_error_from_errno(err),
                  "failed to mount partition %s: %s", device, g_strerror(err));
      retval = FALSE;
      goto cleanup;
    }

  if (!extract_image(bundle, mountpoint, store, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }



  /* touch /usr for systemd */
  utimensat(AT_FDCWD, mountpoint, NULL, 0);

  gchar *snapshot_root_path = g_strjoin("/", "/.snapshots",
					snapshot_root, "snapshot", NULL);
  if (!btrfs_set_readonly (snapshot_root_path, false, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

#if 0
  if (!adjust_etc_fstab_usr_AB (snapshot_root_path, snapshot_usr, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }
#endif

  if (!update_kernel (NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!update_bootloader (NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!btrfs_get_subvolume_id (snapshot_root_path, "/.snapshots",
			       &subvol_id, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!btrfs_set_default (subvol_id, snapshot_root_path, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

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
#endif

  if (snapshot_root)
    free (snapshot_root);
  if (subvol_id)
    free (subvol_id);

  return retval;
}

gboolean
update_system (TIUBundle *bundle, const gchar *store, GError **error)
{
  if (access ("/os", F_OK) == 0)
    return update_system_usr_btrfs (bundle, store, error);
  else
    return update_system_usr_AB (bundle, store, error);
}

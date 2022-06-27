/*  Copyright (C) 2021,2022 Thorsten Kukuk <kukuk@suse.com>

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
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <libeconf.h>
#include <sys/mount.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "tiu-btrfs.h"
#include "tiu-mount.h"
#include "tiu-swupdate.h"
#include "tiu-errors.h"

#define LIBEXEC_TIU "/usr/libexec/tiu/"

static gboolean
exec_script (const gchar *script, const gchar *device, GError **error,
	     const gchar *disk_layout, const gchar *logfile)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (verbose_flag)
    g_printf("Running script '%s' for device '%s'...\n",
	     script, device);
  if (debug_flag)
    {
      if (disk_layout)
	g_printf("Disk layout stored in: %s\n",	disk_layout);
      if (logfile)
	g_printf("Output will be written to: %s\n", logfile);
    }

  g_ptr_array_add(args, g_strdup(script));
  g_ptr_array_add(args, "-d");
  g_ptr_array_add(args, g_strdup(device));
  if (logfile)
    {
       g_ptr_array_add(args, "-o");
       g_ptr_array_add(args, g_strdup(logfile));
    }
  if (disk_layout)
    {
       g_ptr_array_add(args, "-l");
       g_ptr_array_add(args, g_strdup(disk_layout));
    }
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start sub-process (%s): ", script);
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute sub-process (%s): ", script);
      return FALSE;
    }

  return TRUE;
}

static gboolean
wipefs (const gchar *device,  const gchar *logfile __attribute__((unused)), GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (verbose_flag)
    g_printf("Running wipefs on device '%s'...\n", device);

  /* XXX implement writing into logfile */
  if (debug_flag)
    {
      if (logfile)
	g_printf("Output will be written to: %s\n", logfile);
    }

  /* XXX strdup all or nothing... */
  g_ptr_array_add(args, "wipefs");
  g_ptr_array_add(args, "-f");
  g_ptr_array_add(args, "-a");
  g_ptr_array_add(args, g_strdup(device));
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start sub-process (wipefs): ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute sub-process (wipefs): ");
      return FALSE;
    }

  return TRUE;
}


/* XXX make this available in mount.c as rec_umount and merge with
   umount_chroot() */
static void
rec_umount(const gchar *mountpoint)
{
   g_autoptr (GSubprocess) sproc = NULL;
   GError *ierror = NULL;

   if (mountpoint == NULL) return;
   if (verbose_flag)
     g_printf("  * unmount %s...\n", mountpoint);

   GPtrArray *args = g_ptr_array_new_full(4, g_free);
   g_ptr_array_add(args, "umount");
   g_ptr_array_add(args, "-R");
   g_ptr_array_add(args,  g_strdup(mountpoint));
   g_ptr_array_add(args, NULL);
   sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			     G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
   if (sproc != NULL)
     {
       g_subprocess_wait_check(sproc, NULL, &ierror);
     }
   g_ptr_array_free(args, FALSE);
   g_clear_error(&ierror);
}

#if 0 /* keep as backup */
static gboolean
call_swupdate(const gchar *archive, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  if (archive == NULL)
    return FALSE;

  if (verbose_flag)
    g_printf("  * Call swupdate with %s...\n", archive);

  GPtrArray *args = g_ptr_array_new_full(6, g_free);
  g_ptr_array_add(args, "swupdate");
  g_ptr_array_add(args, "-k");
  g_ptr_array_add(args, "/usr/share/swupdate/certs/public.pem");
  g_ptr_array_add(args, "-i");
  g_ptr_array_add(args,  g_strdup(archive));
  g_ptr_array_add(args, NULL);
  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);


  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start swupdate: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute swupdate: ");
      return FALSE;
    }

  return TRUE;
}
#endif

static gboolean
set_usr_device_name (const gchar *path, const gchar *device, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  gchar *fstab = g_strjoin("/", path, "etc/fstab", NULL);

  if (debug_flag)
    g_printf("Adjusting '%s'...\n", fstab);

  gchar *sedarg = g_strjoin(NULL, "s|.*[[:space:]]/usr[[:space:]].*|",
                            device, "  /usr  ext4 ro,x-initrd.mount  0  0|g", NULL);

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

/* This function is called after installation to cleanup leftovers.
   Goal should be that you can call the tiu installer as often as you wish. */
static void
cleanup_install (void)
{
   g_autoptr (GSubprocess) sproc = NULL;
   GError *ierror = NULL;

   if (verbose_flag)
     g_printf("Cleanup system:\n");

   /* if /usr is mounted, umount that first, could shadow /usr/local */
   if (is_mounted ("/mnt/usr", &ierror))
     rec_umount("/mnt/usr");
   rec_umount("/mnt");
}

gboolean
install_system (const gchar *archive, const gchar *device,
		const gchar *disk_layout, GError **error)
{
  GError *ierror = NULL;
  gboolean retval = FALSE;

  if (archive == NULL)
    {
      g_set_error_literal (error,
			   T_ARCHIVE_ERROR,
			   T_ARCHIVE_ERROR_NO_DATA,
			   "No valid archive available.");
      return FALSE;
    }

  if (device == NULL)
    {
      /* XXX Error message */
      return FALSE;
    }

  if (disk_layout == NULL)
    {
      /* XXX Error message */
      return FALSE;
    }

  if (!wipefs (device, LOG"wipefs.log", &ierror))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"setup-disk", device, &ierror,
		    disk_layout, LOG"setup-disk.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  /* XXX we need a way to identify the real /dev/... device name! */
  /* XXX better would we if we could create /etc/fstab directly with
     device names... */
  if (!set_usr_device_name ("/mnt", "/dev/vda3", &ierror))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"setup-root", device,
		    &ierror, NULL, LOG"setup-root.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  /* we have at minimum two partitions A/B to switch between.
     /dev/update-image-usr should be a symlink to the next free partition. */
  remove("/dev/update-image-usr");
  /* XXX replace hard coded device, add error check */
  if (symlink("/dev/vda3", "/dev/update-image-usr"))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to create symlink: %s", g_strerror(err));
      goto cleanup;
    }

  /* Make sure usr/local is not mounted, else casync will fail on mount point */
  if (umount2 ("/mnt/usr/local", UMOUNT_NOFOLLOW))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to umount usr/local: %s", g_strerror(err));
      goto cleanup;
    }

  /* umunt /mnt/usr, we cannot write to the partition if it is mounted */
  if (umount2 ("/mnt/usr", UMOUNT_NOFOLLOW))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to umount usr: %s", g_strerror(err));
      goto cleanup;
    }

#if 1
  if (!swupdate_deploy (archive, &ierror))
#else
  if (!call_swupdate (archive, &ierror))
#endif
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  /* mount /usr so that we can setup the rest of the system */
  /* XXX replace hard coded device and filesystem values */
  if (mount("/dev/vda3", "/mnt/usr", "ext4", 0, NULL))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to re-mount /usr read-write: %s", g_strerror(err));
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"/populate-etc", device,
		    &ierror, NULL, LOG"populate-etc.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!setup_chroot ("/mnt", TIU_ROOT_DIR, &ierror))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"setup-bootloader", device,
		    &ierror, NULL, LOG"setup-bootloader.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"finish", device,
		    &ierror, NULL, LOG"finish.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  retval = TRUE;

 cleanup:
  /* Umount /mnt/usr first, since it could hide /mnt/usr/local */
  umount2 ("/mnt/usr", UMOUNT_NOFOLLOW);
  umount_chroot(TIU_ROOT_DIR, TRUE, NULL);
  cleanup_install ();

  return retval;
}

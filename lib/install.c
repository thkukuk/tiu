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
#include "tiu-mount.h"
#include "tiu-errors.h"

#define LIBEXEC_TIU "/usr/libexec/tiu/"

static gboolean
exec_script (const gchar *script, const gchar *device, GError **error,
	     const gchar *disk_layout, const gchar *logfile)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (debug_flag)
    {
      g_printf("Running script '%s' for device '%s'...\n",
	       script, device);
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

/* This function is called after installation to cleanup leftovers.
   Goal should be that you can call the tiu installer as often as you wish. */
static void
cleanup_install (TIUBundle *bundle)
{
   g_autoptr (GSubprocess) sproc = NULL;
   GError *ierror = NULL;

   if (verbose_flag)
     g_printf("Cleanup system:\n");

   /* Remove snapper config for "usr" snapshots */
   if (access ("/etc/snapper/configs/usr", F_OK) == 0)
     {
       if (verbose_flag)
	 g_printf("  * Delete \"usr\" snapper configuration...\n");

       g_unlink ("/etc/snapper/configs/usr");

       GPtrArray *args = g_ptr_array_new_full(6, g_free);
       g_ptr_array_add(args, "sed");
       g_ptr_array_add(args, "-i");
       g_ptr_array_add(args, "-e");
       g_ptr_array_add(args, "s|SNAPPER_CONFIGS=.*|SNAPPER_CONFIGS=\"\"|g");
       g_ptr_array_add(args, "/etc/sysconfig/snapper");
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

   /* if /usr is mounted, umount that first, could shadow /usr/local */
   if (is_mounted ("/mnt/usr", &ierror))
     { /* XXX 2x same code, move in a function */
       if (verbose_flag)
	 g_printf("  * unmount /mnt/usr...\n");

       GPtrArray *args = g_ptr_array_new_full(4, g_free);
       g_ptr_array_add(args, "umount");
       g_ptr_array_add(args, "-R");
       g_ptr_array_add(args, "/mnt/usr");
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
   {
     if (verbose_flag)
       g_printf("  * unmount /mnt\n");

     GPtrArray *args = g_ptr_array_new_full(4, g_free);
     g_ptr_array_add(args, "umount");
     g_ptr_array_add(args, "-R");
     g_ptr_array_add(args, "/mnt");
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

   if (bundle->mount_point != NULL)
     {
       if (verbose_flag)
	 g_printf("  * unmount /var/lib/tiu/mount\n");

       umount_tiu_archive (bundle, &ierror);
     }
}

gboolean
install_system (TIUBundle *bundle, const gchar *device,
		TIUPartSchema schema,
		const gchar *disk_layout,
		const gchar *store,
		GError **error)
{
  GError *ierror = NULL;
  gboolean retval = FALSE;

  if (bundle == NULL)
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

  if (!exec_script (LIBEXEC_TIU"setup-disk", device, &ierror,
		    disk_layout, LOG"tiu-setup-disk.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"setup-root", device,
		    &ierror, NULL, LOG"tiu-setup-root.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (schema == TIU_USR_BTRFS)
    {
      /* snapshots for /usr is only required if /usr is btrfs and the only /usr partition */
      if (!exec_script (LIBEXEC_TIU"setup-usr-snapper", device,
			&ierror, NULL, LOG"tiu-setup-usr-snapper.log"))
	{
	  g_propagate_error(error, ierror);
	  goto cleanup;
	}
    }
  else
    {
      /* we have at minimum two partitions A/B to switch between. Re-mount /usr read-write */
      /* XXX replace hard coded device and filesystem values */
      if (mount("/dev/vda3", "/mnt/usr", "xfs", MS_REMOUNT, NULL))
	{
	  int err = errno;
	  g_set_error(&ierror, G_FILE_ERROR, g_file_error_from_errno(err),
		      "failed to re-mount /usr read-write: %s", g_strerror(err));
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
    }

  if (!extract_image(bundle, "/mnt/usr", store, &ierror))
    {
      if (ierror)
	g_propagate_error(error, ierror);
      else
	g_fprintf (stderr, "ERROR: installing the archive failed!\n");
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"/populate-etc", device,
		    &ierror, NULL, LOG"tiu-populate-etc.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!setup_chroot ("/mnt", &ierror))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"setup-bootloader", device,
		    &ierror, NULL, LOG"tiu-setup-bootloader.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  if (!exec_script (LIBEXEC_TIU"finish", device,
		    &ierror, NULL, LOG"tiu-finish.log"))
    {
      g_propagate_error(error, ierror);
      goto cleanup;
    }

  retval = TRUE;

 cleanup:
  /* Umount /mnt/usr first, since it could hide /mnt/usr/local */
  umount2 ("/mnt/usr", UMOUNT_NOFOLLOW);
  umount_chroot(TIU_ROOT_DIR, TRUE, NULL);
  cleanup_install (bundle);

  return retval;
}

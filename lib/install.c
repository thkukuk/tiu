/*  Copyright (C) 2021  Thorsten Kukuk <kukuk@suse.com>

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
#include <libeconf.h>
#include <sys/mount.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "tiu-errors.h"

#define LIBEXEC_TIU "/usr/libexec/tiu/"
#define LOG "/var/log/"

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
      g_propagate_prefixed_error(error, ierror, "Failed to start sub-process: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute sub-process: ");
      return FALSE;
    }

  return TRUE;
}

/* This function will be called only if something has gone wrong while  */
/* the installation. It tries to reset settings in order to ensure that */
/* the next run of tiu will not fail due corrupted settings.            */
void cleanup_install (TIUBundle *bundle)
{
   g_autoptr (GSubprocess) sproc = NULL;
   GError *ierror = NULL;
   GPtrArray *args = g_ptr_array_new_full(5, g_free);

   g_printf("Try to reset system changes which already have been done:\n");

   g_printf("  Delete \"usr\" snapper configuration...\n");
   g_ptr_array_add(args, "snapper");
   g_ptr_array_add(args, "-c");
   g_ptr_array_add(args, "usr");
   g_ptr_array_add(args, "delete-config");
   g_ptr_array_add(args, NULL);
   sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			     G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
   if (sproc != NULL)
     {
	g_subprocess_wait_check(sproc, NULL, &ierror);
     }
   g_ptr_array_free(args, FALSE);
   g_clear_error(&ierror);

   g_printf("  unmount /mnt/usr...\n");
   args = g_ptr_array_new_full(4, g_free);
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

   g_printf("  unmount /mnt\n");
   args = g_ptr_array_new_full(4, g_free);
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

   g_printf("  unmount /var/lib/tiu/mount\n");
   if (bundle->path != NULL) umount_tiu_archive (bundle, &ierror);
}

gboolean
install_system (TIUBundle *bundle, const gchar *device,
		TIUPartSchema schema,
		const gchar *disk_layout, GError **error)
{
  GError *ierror = NULL;

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
      return FALSE;
    }

  if (!exec_script (LIBEXEC_TIU"setup-root", device,
		    &ierror, NULL, LOG"tiu-setup-root.log"))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (schema == TIU_USR_BTRFS)
    {
      /* snapshots for /usr is only required if /usr is btrfs and the only /usr partition */
      if (!exec_script (LIBEXEC_TIU"setup-usr-snapper", device,
			&ierror, NULL, LOG"tiu-setup-usr-snapper.log"))	    
	{
	  g_propagate_error(error, ierror);
	  return FALSE;
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
	  return FALSE;
	}

      /* Make sure usr/local is not mounted, else casync will fail on mount point */
      if (umount2 ("/mnt/usr/local", UMOUNT_NOFOLLOW))
	{
	  int err = errno;
	  g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		      "failed to umount usr/local: %s", g_strerror(err));
	  return FALSE;
	}
    }

  if (!extract_image(bundle, "/mnt/usr", &ierror))
    {
      if (ierror)
	g_propagate_error(error, ierror);
      else
	g_fprintf (stderr, "ERROR: installing the archive failed!\n");
      return FALSE;
    }

  if (!exec_script (LIBEXEC_TIU"/populate-etc", device,
		    &ierror, NULL, LOG"tiu-populate-etc.log"))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!exec_script (LIBEXEC_TIU"setup-bootloader", device,
		    &ierror, NULL, LOG"tiu-setup-bootloader.log"))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!exec_script (LIBEXEC_TIU"finish", device,
		    &ierror, NULL, LOG"tiu-finish.log"))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  return TRUE;
}

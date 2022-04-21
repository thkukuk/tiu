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

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include "tiu-internal.h"
#include "tiu-btrfs.h"

gboolean
btrfs_set_readonly (const gchar *path, gboolean ro, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (debug_flag)
    g_printf("Setting subvolume '%s' to '%s'...\n",
             path, ro?"read-only":"read-write");

  g_ptr_array_add(args, "btrfs");
  g_ptr_array_add(args, "property");
  g_ptr_array_add(args, "set");
  g_ptr_array_add(args, g_strdup(path));
  g_ptr_array_add(args, "ro");
  g_ptr_array_add(args, ro?"true":"false");
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start btrfs set property: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute btrfs set property: ");
      return FALSE;
    }

  return TRUE;
}

gboolean
btrfs_get_subvolume_id (const gchar *snapshot_dir, const gchar *mountpoint, gchar **output, GError **error)
{
  gboolean retval = TRUE;
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);

  if (debug_flag)
    g_printf("Get subvolume ID for '%s'...\n", snapshot_dir);

  gchar *sharg = g_strjoin(NULL, "btrfs subvolume list -o ", mountpoint, " | grep ",
                           snapshot_dir, " | awk '{print $2}'", NULL);

  g_ptr_array_add(args, "/bin/sh");
  g_ptr_array_add(args, "-c");
  g_ptr_array_add(args, sharg);
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start getting subvolume ID: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute getting subvolume ID: ");
      retval = FALSE;
      goto cleanup;
    }

  gchar *stdout;
  gchar *stderr;
  if (!g_subprocess_communicate_utf8 (sproc, NULL, NULL, &stdout, &stderr, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to read stdout to get subvolume ID: ");
      retval = FALSE;
      goto cleanup;

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
                  "Failed to read stdout to get subvolume ID:");
      retval = FALSE;
      goto cleanup;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);
  if (sharg)
    free (sharg);

  return retval;
}

gboolean
btrfs_set_default (const gchar *btrfs_id, const gchar *path, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Set btrfs subvolume %s as default...\n", btrfs_id);

  g_ptr_array_add(args, "btrfs");
  g_ptr_array_add(args, "subvolume");
  g_ptr_array_add(args, "set-default");
  g_ptr_array_add(args, g_strdup(btrfs_id));
  g_ptr_array_add(args, g_strdup(path));
  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
                            G_SUBPROCESS_FLAGS_STDOUT_PIPE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start setting default subvolume: ");
      retval = FALSE;
      goto cleanup;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to execute setting default subvolume: ");
      retval = FALSE;
    }

 cleanup:
  g_ptr_array_free (args, TRUE);

  return retval;
}

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <glib/gprintf.h>
#include <libeconf.h>

#include "tiu.h"

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

static gboolean
adjust_etc_fstab (const gchar *path, const gchar *snapshot_usr, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  gchar *fstab = g_strjoin("/", path, "etc/fstab", NULL);

  if (debug_flag)
    g_printf("Adjusting '%s'...\n", fstab);

  gchar *sedarg = g_strjoin(NULL, "s|subvol=/@/usr/.snapshots/.*/snapshot|subvol=/@/usr/.snapshots/",
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
    }

 cleanup:
  g_ptr_array_free (args, TRUE);
  g_free (sedarg);
  g_free (fstab);

  return retval;
}

static gboolean
btrfs_get_subvolume_id (const gchar *snapshot_dir, gchar **output, GError **error)
{
  gboolean retval = TRUE;
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);

  if (debug_flag)
    g_printf("Get subvolume ID for '%s'...\n", snapshot_dir);

  gchar *sharg = g_strjoin(NULL, "btrfs subvolume list -o /.snapshots | grep ",
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

static gboolean
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

static gboolean
update_kernel (GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating kernel in /boot...\n");

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
update_bootloader (GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, NULL);
  gboolean retval = TRUE;

  if (debug_flag)
    g_printf("Updating bootloader...\n");

  g_ptr_array_add(args, "/usr/sbin/update-bootlaoder");
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

gboolean
update_system (const gchar *tiuname, GError **error)
{
  gboolean retval = TRUE;
  GError *ierror = NULL;
  gchar *snapshot_root = NULL;
  gchar *snapshot_usr = NULL;
  gchar *subvol_id = NULL;

  if (!snapper_create ("usr", &snapshot_usr, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!snapper_create (NULL, &snapshot_root, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  gchar *usr_path = g_strjoin("/", "/usr/.snapshots",
			      snapshot_usr, "snapshot", NULL);
  if (!btrfs_set_readonly (usr_path, false, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!extract_tiu_image(tiuname, usr_path, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  /* touch /usr for systemd */
  utimensat(AT_FDCWD, usr_path, NULL, 0);

  if (!btrfs_set_readonly (usr_path, true, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  gchar *root_path = g_strjoin("/", "/.snapshots",
			      snapshot_root, "snapshot", NULL);
  if (!btrfs_set_readonly (root_path, false, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!adjust_etc_fstab (root_path, snapshot_usr, &ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!update_kernel (&ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  if (!update_bootloader (&ierror))
    {
      g_propagate_error(error, ierror);
      retval = FALSE;
      goto cleanup;
    }

  btrfs_get_subvolume_id (root_path, &subvol_id, &ierror);
  btrfs_set_default (subvol_id, root_path, &ierror);

 cleanup:
  if (snapshot_usr)
    free (snapshot_usr);
  if (snapshot_root)
    free (snapshot_root);
  if (subvol_id)
    free (subvol_id);

  return retval;
}

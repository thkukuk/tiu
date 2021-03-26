
#include <glib.h>
#include <gio/gio.h>

#include "tiu.h"

static gboolean
create_btrfs_subvolume (const gchar *subvolume, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  g_ptr_array_add(args, g_strdup("btrfs"));
  g_ptr_array_add(args, g_strdup("subvolume"));
  g_ptr_array_add(args, g_strdup("create"));
  g_ptr_array_add(args, g_strdup(subvolume));

  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start btrfs: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to create btrfs subvolume: ");
      return FALSE;
    }

  return TRUE;
}

static gboolean
delete_btrfs_subvolume (const gchar *subvolume, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  g_ptr_array_add(args, g_strdup("btrfs"));
  g_ptr_array_add(args, g_strdup("subvolume"));
  g_ptr_array_add(args, g_strdup("delete"));
  g_ptr_array_add(args, g_strdup(subvolume));

  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start btrfs: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to create btrfs subvolume: ");
      return FALSE;
    }

  return TRUE;
}

gboolean
workdir_destroy (const gchar *workdir, GError **error)
{
  GFile *file;
  gboolean res;

  if (workdir == NULL)
    return TRUE;

  const gchar *btrfsdir = NULL;
  btrfsdir = g_strjoin("/", workdir, "btrfs", NULL);

  if (!delete_btrfs_subvolume (btrfsdir, error))
    return FALSE;

  file = g_file_new_for_path (workdir);
  res = rm_rf (file, NULL, error);
  g_object_unref(file);

  return res;
}

gboolean
workdir_setup (const gchar *contentpath, const gchar **workdir, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(15, g_free);
  const gchar *tmpdir = NULL;
  const gchar *btrfsdir = NULL;

  tmpdir = g_dir_make_tmp("tiu-workdir-XXXXXXXXXX", &ierror);
  if (tmpdir == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to create work directory: ");
      return FALSE;
    }

  btrfsdir = g_strjoin("/", tmpdir, "btrfs", NULL);

  if (!create_btrfs_subvolume(btrfsdir, error))
    return FALSE;

  g_ptr_array_add(args, g_strdup("tar"));
  g_ptr_array_add(args, g_strdup("xf"));
  g_ptr_array_add(args, g_strdup(contentpath));
  g_ptr_array_add(args, g_strdup("-C"));
  g_ptr_array_add(args, g_strdup(btrfsdir));

  g_ptr_array_add(args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start tar: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror, "Failed to extract tar archive: ");
      return FALSE;
    }

  if (workdir)
    *workdir = tmpdir;

  return TRUE;
}

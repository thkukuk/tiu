
#include <glib.h>
#include <gio/gio.h>

#include "tiu-internal.h"

gboolean
workdir_destroy (const gchar *workdir, GError **error)
{
  GFile *file;
  gboolean res;

  if (workdir == NULL)
    return TRUE;

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
  GPtrArray *tar_args = g_ptr_array_new_full(15, g_free);
  GPtrArray *mv_args = g_ptr_array_new_full(5, g_free);
  const gchar *tmpdir = NULL;
  const gchar *mv_cmd = NULL;

  tmpdir = g_dir_make_tmp("tiu-workdir-XXXXXXXXXX", &ierror);
  if (tmpdir == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to create work directory: ");
      return FALSE;
    }

  g_ptr_array_add(tar_args, g_strdup("tar"));
  g_ptr_array_add(tar_args, g_strdup("xf"));
  g_ptr_array_add(tar_args, g_strdup(contentpath));
  g_ptr_array_add(tar_args, g_strdup("-C"));
  g_ptr_array_add(tar_args, g_strdup(tmpdir));
  g_ptr_array_add(tar_args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)tar_args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start tar: ");
      return FALSE; /* XXX cleanup */
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror, "Failed to extract tar archive: ");
      return FALSE; /* XXX cleanup */
    }
  g_ptr_array_free(tar_args, TRUE);

  /* Move /etc to /usr/share/factory/etc */
  mv_args = g_ptr_array_new_full(5, g_free);
  mv_cmd = g_strjoin(NULL, "mv ", tmpdir, "/etc ", tmpdir, "/usr/share/factory/etc", NULL);

  g_ptr_array_add(mv_args, g_strdup("/bin/sh"));
  g_ptr_array_add(mv_args, g_strdup("-c"));
  g_ptr_array_add(mv_args, g_strdup(mv_cmd));
  g_ptr_array_add(mv_args, NULL);

  sproc = g_subprocess_newv((const gchar * const *)mv_args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror, "Failed to start mv: ");
      return FALSE; /* XXX cleanup */
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror, "Failed to move etc directory: ");
      return FALSE; /* XXX cleanup */
    }

  g_ptr_array_free(mv_args, TRUE);

  if (workdir)
    *workdir = tmpdir;

  return TRUE;
}

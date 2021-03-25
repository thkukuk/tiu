#include <glib.h>
#include <gio/gio.h>

#include "tiu.h"

gboolean
casync_extract(const gchar *input, const gchar *dest, const gchar *store,
	       const gchar *seed, GError **error)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  g_autoptr(GPtrArray) args = g_ptr_array_new_full(15, g_free);

  g_ptr_array_add(args, g_strdup("casync"));
  g_ptr_array_add(args, g_strdup("extract"));
  if (seed)
    {
      g_ptr_array_add(args, g_strdup("--seed"));
      g_ptr_array_add(args, g_strdup(seed));
    }
  if (store)
    {
      g_ptr_array_add(args, g_strdup("--store"));
      g_ptr_array_add(args, g_strdup(store));
    }
  /* https://github.com/systemd/casync/issues/240 */
  g_ptr_array_add(args, g_strdup("--seed-output=no"));
  g_ptr_array_add(args, g_strdup(input));
  g_ptr_array_add(args, g_strdup(dest));
  g_ptr_array_add(args, NULL);

  launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
#if 0 /* XXX */
  if (tmpdir)
    g_subprocess_launcher_setenv(launcher, "TMPDIR", tmpdir, TRUE);
#endif

  sproc = g_subprocess_launcher_spawnv(launcher,
				       (const gchar * const *)args->pdata,
				       &ierror);
  if (sproc == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to start casync extract: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to run casync extract: ");
      return FALSE;
    }

  return TRUE;
}

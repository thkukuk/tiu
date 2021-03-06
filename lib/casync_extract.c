#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include "tiu.h"

gboolean
casync_extract(const gchar *input, const gchar *dest, const gchar *store,
	       const gchar *seed, GError **error)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  g_autoptr(GPtrArray) args = g_ptr_array_new_full(15, g_free);

  if (debug_flag)
    g_printf("Extract '%s' to '%s' with casync...\n", input, dest);

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
  /* XXX https://github.com/systemd/casync/issues/240 */
  g_ptr_array_add(args, g_strdup("--seed-output=no"));
  g_ptr_array_add(args, g_strdup(input));
  g_ptr_array_add(args, g_strdup(dest));
  g_ptr_array_add(args, NULL);

#if 0 /* XXX */
  launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
  if (tmpdir)
    g_subprocess_launcher_setenv(launcher, "TMPDIR", tmpdir, TRUE);

  sproc = g_subprocess_launcher_spawnv(launcher,
				       (const gchar * const *)args->pdata,
				       &ierror);
#else
  sproc = g_subprocess_newv((const gchar * const *)args->pdata,
			    G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
#endif
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

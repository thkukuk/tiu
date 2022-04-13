#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include "tiu-internal.h"

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

  if (debug_flag)
     g_ptr_array_add(args, g_strdup("--verbose"));

  if (seed)
    {
      g_ptr_array_add(args, g_strdup("--seed"));
      g_ptr_array_add(args, g_strdup(seed));
      if (debug_flag) {
         g_printf("  --seed: %s\n", seed);
	 g_printf("  --seed-output=no");
      }
    }
  if (store)
    {
      g_ptr_array_add(args, g_strdup("--store"));
      g_ptr_array_add(args, g_strdup(store));
      if (debug_flag)
         g_printf("  --store: %s\n", store);
    }
  /* XXX https://github.com/systemd/casync/issues/240 */
  g_ptr_array_add(args, g_strdup("--seed-output=no"));
  g_ptr_array_add(args, g_strdup(input));
  g_ptr_array_add(args, g_strdup(dest));
  g_ptr_array_add(args, NULL);

  if (debug_flag) {
     launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_MERGE);
     g_subprocess_launcher_set_stdout_file_path(launcher,
		  LOG"tiu-cascyn-extract.log");
     g_printf("Output will be written to %s\n", LOG"tiu-cascyn-extract.log");
     sproc = g_subprocess_launcher_spawnv(launcher,
		  (const gchar * const *)args->pdata,
		  &ierror);
  } else {
     sproc = g_subprocess_newv((const gchar * const *)args->pdata,
		  G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ierror);
  }

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

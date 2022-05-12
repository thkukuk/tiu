#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include "tiu-internal.h"

gboolean
desync_untar(const gchar *input, const gchar *dest, const gchar *store,
	     GError **error)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  g_autoptr(GPtrArray) args = g_ptr_array_new_full(15, g_free);

  if (debug_flag)
    g_printf("Extract '%s' to '%s' with desync...\n", input, dest);

  g_ptr_array_add(args, g_strdup("desync"));
  g_ptr_array_add(args, g_strdup("untar"));

  if (debug_flag)
     g_ptr_array_add(args, g_strdup("--verbose"));

  if (store)
    {
      g_ptr_array_add(args, g_strdup("--store"));
      g_ptr_array_add(args, g_strdup(store));
      if (debug_flag)
         g_printf("  --store: %s\n", store);
    }

  g_ptr_array_add(args, g_strdup(input));
  g_ptr_array_add(args, g_strdup(dest));
  g_ptr_array_add(args, NULL);

  if (debug_flag) {
     launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_MERGE);
     g_subprocess_launcher_set_stdout_file_path(launcher,
		  LOG"tiu-desync-extract.log");
     g_printf("Output will be written to %s\n", LOG"tiu-desync-extract.log");
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
				 "failed to start desync untar: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to run desync untar: ");
      return FALSE;
    }

  return TRUE;
}

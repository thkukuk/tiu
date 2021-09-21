#include <errno.h>
#include <glib/gprintf.h>
#include <libeconf.h>

#include "tiu.h"

static gboolean
exec_script (const gchar *script, const gchar *device, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(8, g_free);

  if (debug_flag)
    g_printf("Running script '%s' for device '%s'...\n",
	     script, device);

  g_ptr_array_add(args, g_strdup(script));
  g_ptr_array_add(args, "-d");
  g_ptr_array_add(args, g_strdup(device));
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

gboolean
install_system (const gchar *tiuname, const gchar *device,
		GError **error)
{
  GError *ierror = NULL;

  if (!exec_script ("/usr/libexec/tiu/setup-disk", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }
  if (!exec_script ("/usr/libexec/tiu/setup-root", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }
  if (!exec_script ("/usr/libexec/tiu/setup-usr-snapper", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!extract_tiu_image(tiuname, "/mnt/usr", &ierror))
    {
      if (ierror)
	g_propagate_error(error, ierror);
      else
	g_fprintf (stderr, "ERROR: installing the archive failed!\n");
      return FALSE;
    }

  if (!exec_script ("/usr/libexec/tiu/populate-etc", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }
  if (!exec_script ("/usr/libexec/tiu/setup-bootloader", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }
  if (!exec_script ("/usr/libexec/tiu/finish", device, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  return TRUE;
}

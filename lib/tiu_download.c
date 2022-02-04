
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "network.h"

gboolean
download_tiu_archive (const gchar *tiuname, struct TIUBundle **bundle, GError **error)
{
  const gchar *cachedir = "/var/cache/tiu";
  GError *ierror = NULL;
  gchar *tiuscheme = g_uri_parse_scheme(tiuname);

  if (bundle == NULL)
    {
      /* XXX use error */
      g_fprintf(stderr, "TIUBundle is NULL pointer!\n");
      return FALSE;
    }

  *bundle = calloc(1, sizeof(TIUBundle));
  if (*bundle == NULL)
    {
      g_fprintf(stderr, "ERROR: Out of memory!\n");
      return FALSE;
    }

  if (tiuscheme == NULL)
    {
      g_autofree gchar *cwd = g_get_current_dir();

      (*bundle)->origpath = g_strdup(tiuname);
      (*bundle)->path = g_build_filename(cwd, tiuname, NULL);

      if (!g_file_test((*bundle)->path, G_FILE_TEST_EXISTS))
	{
	  /* XXX */
	  g_fprintf(stderr, "No such file: %s\n", (*bundle)->path);
	  return FALSE;
	}
    }
  else if (is_remote_scheme(tiuscheme))
    {
      (*bundle)->origpath = g_strdup(tiuname);
      gchar *tiu_basename = g_path_get_basename(tiuname);
      (*bundle)->path = g_build_filename(cachedir, tiu_basename, NULL);
      free (tiu_basename);

      if (g_mkdir_with_parents(cachedir, 0700) != 0)
        {
          g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating cache directory '%s'", cachedir);
          return FALSE;
        }

      if (verbose_flag)
        g_printf("Remote URI detected, downloading tiu archive to '%s'...\n",
		 (*bundle)->path);

      if (!network_init(&ierror))
        {
          g_propagate_error(error, ierror);
          return FALSE;
        }

      /* Make sure the file does not exist in the cache */
      g_remove ((*bundle)->path);
      if (!download_file((*bundle)->path, tiuname,
                         DEFAULT_MAX_DOWNLOAD_SIZE, &ierror))
        {
          g_propagate_prefixed_error(error, ierror,
				     "Failed to download tiu archive %s: ",
				     tiuname);
          return FALSE;
        }
      if (verbose_flag)
        g_printf("Downloaded tiu archive to '%s'\n", (*bundle)->path);
    }
  else
    {
      (*bundle)->path = g_strdup(tiuname);
    }

  return TRUE;
}

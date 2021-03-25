#include <errno.h>
#include <glib/gprintf.h>

#include "tiu.h"

static gchar *
resolve_tiu_path(const char *path)
{
  g_autofree gchar *scheme = NULL;
  g_autofree gchar *location = NULL;
  GError *error = NULL;

  scheme = g_uri_parse_scheme(path);
  if (scheme == NULL && !g_path_is_absolute(path))
    {
      g_autofree gchar *cwd = g_get_current_dir();
      location = g_build_filename(cwd, path, NULL);
    }
  else
    {
      g_autofree gchar *hostname = NULL;

      if (g_strcmp0(scheme, "file") == 0)
	{
	  location = g_filename_from_uri(path, &hostname, &error);
	  if (!location)
	    {
	      g_fprintf(stderr, "Conversion error: %s\n", error->message);
	      g_clear_error(&error);
	      return NULL;
	    }

	  if (hostname != NULL)
	    {
	      g_fprintf(stderr, "file URI with hostname detected. Did you forget to add a leading / ?\n");
	      return NULL;
	    }

	  /* Clear scheme to trigger local path handling */
	  g_clear_pointer(&scheme, g_free);
	}
      else
	{
	  location = g_strdup(path);
	}
    }

  /* If the URI parser returns NULL, assume install with local path */
  if (scheme == NULL)
    {
      if (!g_file_test(location, G_FILE_TEST_EXISTS))
	{
	  g_fprintf(stderr, "No such file: %s\n", location);
	  return NULL;
	}
    }

  return g_steal_pointer(&location);
}

gboolean
apply_tiu_image(const gchar *tiuname, const gchar *outputdir,
		GError **error)
{
  GError *ierror = NULL;
  TIUBundle *bundle = NULL;
  g_autofree gchar *tiulocation = NULL;

  tiulocation = resolve_tiu_path(tiuname);

  if (tiulocation == NULL)
    return FALSE;

  if (debug_flag)
    g_printf("input tiu archive: %s\n", tiulocation);

  if (!check_tiu_archive(tiulocation, &bundle, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (!mount_tiu_archive(bundle, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  gchar *inputfile = "/var/lib/tiu/mount/openSUSE-MicroOS-20210320.catar"; /* XXX */
  gchar *store = NULL;
  if (!casync_extract(inputfile, outputdir, store, NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      umount_tiu_archive(bundle, &ierror);
      /* XXX warn if umount fails */
      return FALSE;
    }

  if (!umount_tiu_archive(bundle, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  return TRUE;
}

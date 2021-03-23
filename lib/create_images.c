
#include <stdio.h>
#include <libeconf.h>
#include <glib/gprintf.h>

#include "tiu.h"

static void
cleanup(const gchar *tmpdir)
{
  workdir_destroy (tmpdir, NULL);
}

static gboolean
mksquashfs (const gchar *manifest, const gchar *input1,
	    const char *input2, const char *output, GError **error)
{
  g_autoptr (GSubprocess) sproc = NULL;
  GError *ierror = NULL;
  GPtrArray *args = g_ptr_array_new_full(15, g_free);

  g_ptr_array_add(args, g_strdup("mksquashfs"));
  g_ptr_array_add(args, g_strdup(manifest));
  g_ptr_array_add(args, g_strdup(input1));
  if (input2 != NULL)
    g_ptr_array_add(args, g_strdup(input2));
  g_ptr_array_add(args, g_strdup(output));
  g_ptr_array_add(args, g_strdup("-comp"));
  g_ptr_array_add(args, g_strdup("xz"));
  g_ptr_array_add(args, g_strdup("-all-root"));
  g_ptr_array_add(args, g_strdup("-no-xattrs"));
  g_ptr_array_add(args, g_strdup("-noappend"));
  g_ptr_array_add(args, g_strdup("-no-progress"));

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
create_images (const gchar *input, GError **gerror)
{
  const gchar *tmpdir = NULL;
  econf_file *os_release = NULL;
  econf_err error;

  if (!workdir_setup (input, &tmpdir, NULL))
    {
      cleanup (tmpdir);
      return FALSE;
    }

  char *libosrelease = g_strjoin ("/", tmpdir, "btrfs", "usr/lib/os-release", NULL);
  // char *etcosrelease = g_strjoin ("/", tmpdir, "btrfs", "etc/os-release", NULL);

  /* XXX if ((error = econf_readDirs (&os_release, libosrelease, etcosrelease,
     "os-release", "", "=", "#"))) */
  if ((error = econf_readFile (&os_release, libosrelease, "=", "#")))
    {
      fprintf (stderr, "ERROR: couldn't read os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }

  char *version_id = NULL;
  if ((error = econf_getStringValue (os_release, "", "VERSION_ID",
				     &version_id)))
    {
      fprintf (stderr,
	       "ERROR: couldn't read \"VERSION_ID\" from os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }
  char *product_name = NULL;
  if ((error = econf_getStringValue (os_release, "", "NAME",
				     &product_name)))
    {
      fprintf (stderr,
	       "ERROR: couldn't read \"NAME\" from os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }
  for (size_t i = 0; i < strlen(product_name); i++)
    {
      if (product_name[i] == ' ')
	product_name[i] = '-';
    }
  g_printf("Update version: \"%s-%s\"\n", product_name, version_id);

  char *product_id = NULL;
  if ((error = econf_getStringValue (os_release, "", "ID",
				     &product_id)))
    {
      fprintf (stderr,
	       "ERROR: couldn't read \"ID\" from os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }


  /* XXX Cleanup subvolumes! */


  gchar *pvers = g_strjoin("-", product_name, version_id, NULL);
  gchar *pvers_idx = g_strjoin(".", pvers, "caidx", NULL);
  gchar *pvers_str = g_strjoin(".", pvers, "castr", NULL);
  gchar *pvers_tar = g_strjoin(".", pvers, "catar", NULL);

  if (!casync_make (tmpdir, pvers_idx, pvers_str, gerror))
    {
      fprintf (stderr, "ERROR: no caidx archive created, aborting...\n");
      cleanup (tmpdir);
      return FALSE;
    }

  if (!casync_make (tmpdir, pvers_tar, NULL, gerror))
    {
      fprintf (stderr, "ERROR: no catar archive created, aborting...\n");
      cleanup (tmpdir);
      return FALSE;
    }

  econf_file *manifest;
  if ((error = econf_newKeyFile(&manifest, '=', '#')))
    {
      fprintf (stderr, "ERROR: couldn't create manifest file: %s\n",
               econf_errString(error));
      return FALSE;
    }

  econf_setStringValue(manifest, "global", "ID", product_id);
  econf_setStringValue(manifest, "global", "ARCH", "x86_64");
  econf_setStringValue(manifest, "global", "MIN_VERSION", "20210320");
  econf_setStringValue(manifest, "update", "VERSION", version_id);

  econf_writeFile(manifest, tmpdir, "manifest.tiu");

  gchar *manifest_file = g_strjoin("/", tmpdir, "manifest.tiu", NULL);
  gchar *output_tiutar = g_strjoin(".", pvers, "tiutar", NULL);

  if (!mksquashfs (manifest_file, pvers_tar, NULL, output_tiutar, gerror))
    {
      cleanup (tmpdir);
      return FALSE;
    }

  if (!workdir_destroy (tmpdir, NULL))
    return FALSE;

  return TRUE;
}

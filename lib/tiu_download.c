
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <openssl/sha.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "network.h"

/*
  Checking SHA256SUM of the archive.
*/
static gboolean
check_sha256sum(const gchar *filename, const gchar *sha256sum)
{
  unsigned char c[SHA256_DIGEST_LENGTH];
  int i;
  SHA256_CTX shaContext;
  int bytes;
  unsigned char data[1024];
  char *filesha256 = (char*) malloc(33 *sizeof(char));

  FILE *inFile = fopen (filename, "rb");
  if (inFile == NULL)
    return FALSE;

  SHA256_Init (&shaContext);

  while ((bytes = fread (data, 1, 1024, inFile)) != 0)

  SHA256_Update (&shaContext, data, bytes);

  SHA256_Final (c,&shaContext);

  for(i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    sprintf(&filesha256[i*2], "%02x", (unsigned int)c[i]);
  }
  fclose (inFile);
  return (strcmp(filesha256, sha256sum) == 0);
}

gboolean
download_archive (const gchar *archive, const gchar *archive_sha256sum,
		  gchar **location, GError **error)
{
  const gchar *cachedir = "/var/cache/tiu";
  GError *ierror = NULL;
  gchar *tiuscheme = g_uri_parse_scheme(archive);

  if (location == NULL)
    {
      /* XXX use error */
      g_fprintf(stderr, "location is NULL pointer!\n");
      return FALSE;
    }

  if (tiuscheme == NULL)
    {
      g_autofree gchar *cwd = g_get_current_dir();

      *location = g_build_filename(cwd, archive, NULL);

      if (!g_file_test(*location, G_FILE_TEST_EXISTS))
	{
	  /* XXX */
	  g_fprintf(stderr, "No such file: %s\n", *location);
	  return FALSE;
	}
    }
  else if (is_remote_scheme(tiuscheme))
    {
      gchar *tiu_basename = g_path_get_basename(archive);
      *location = g_build_filename(cachedir, tiu_basename, NULL);
      free (tiu_basename);

      if (archive_sha256sum)
	{
	  /*Checking if file has already been downloaded */
	  if (check_sha256sum(*location, archive_sha256sum))
          {
            g_printf("swu archive '%s' has already been downloaded...\n",
		     archive);
	    return TRUE;
	  }
	}

      if (g_mkdir_with_parents(cachedir, 0700) != 0)
        {
          g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating cache directory '%s'", cachedir);
          return FALSE;
        }

      if (!quiet_flag)
        g_printf("Remote URI detected, downloading tiu archive to '%s'...\n",
		 *location);

      if (!network_init(&ierror))
        {
          g_propagate_error(error, ierror);
          return FALSE;
        }

      /* Make sure the file does not exist in the cache */
      g_remove (*location);
      if (!download_file(*location, archive,
                         DEFAULT_MAX_DOWNLOAD_SIZE, &ierror))
        {
          g_propagate_prefixed_error(error, ierror,
				     "Failed to download tiu archive %s: ",
				     archive);
          return FALSE;
        }
      if (!quiet_flag)
        g_printf("Downloaded tiu archive to '%s'\n", *location);
    }
  else
    {
      *location = g_strdup(archive);
    }

  return TRUE;
}

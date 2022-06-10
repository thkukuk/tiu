#include <errno.h>
#include <glib/gprintf.h>
#include <libeconf.h>

#include "tiu.h"
#include "tiu-internal.h"

#if 0 /* XXX */

gboolean
extract_image(TIUBundle *bundle, const gchar *outputdir, const gchar *store,
	      GError **error)
{
  GError *ierror = NULL;

  if (bundle == NULL)
    {
      /* XXX */
      return FALSE;
    }

  if (bundle->path == NULL)
    {
      /* XXX */
      return FALSE;
    }

  if (debug_flag)
    g_printf("Input tiu archive: %s\n", bundle->path);

  econf_err ec_err;
  econf_file *tiumf = NULL;
  gchar *tiumf_name = g_strjoin("/", bundle->mount_point, MANIFEST_TIU, NULL);

  if ((ec_err = econf_readFile (&tiumf, tiumf_name, "=", "#")))
    {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		  "Couldn't read manifest.tiu: %s",
		  econf_errString(ec_err));
      umount_tiu_archive(bundle, &ierror);
      return FALSE;
    }

  gchar *archive_name = NULL;
  if ((ec_err = econf_getStringValue (tiumf, "global", "ARCHIVE",
				     &archive_name)))
    {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		  "Couldn't read 'ARCHIVE' entry: %s",
		  econf_errString(ec_err));
      umount_tiu_archive(bundle, &ierror);
      return FALSE;
    }

  gchar *inputfile = g_strjoin("/", bundle->mount_point, archive_name, NULL);
  if (!desync_untar(inputfile, outputdir, store, &ierror))
    {
      g_propagate_error(error, ierror);
      g_clear_error(&ierror);
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
#endif

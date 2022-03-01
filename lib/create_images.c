
#include <stdio.h>
#include <errno.h>
#include <libeconf.h>
#include <glib/gprintf.h>
#include <gio/gfiledescriptorbased.h>
#include <glib/gstdio.h>
#include <openssl/rand.h>

#include "tiu.h"
#include "tiu-internal.h"
#include "verity_hash.h"
#include "network.h"

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

  if (debug_flag)
    g_printf("Run mksquashfs to generate '%s'...\n", output);

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
      g_propagate_prefixed_error(error, ierror, "Failed to start mksquashfs: ");
      return FALSE;
    }

  if (!g_subprocess_wait_check(sproc, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
                                 "Failed to create squashfs image: ");
      return FALSE;
    }

  return TRUE;
}

static gboolean
rm_dir_content (const gchar *dir, const gchar *tmpdir, GError **error)
{
  g_autoptr(GFileEnumerator) enumerator = NULL;

  GFile *file = g_file_new_for_path (g_strjoin("/", tmpdir,
					       "usr", dir, NULL));
  enumerator = g_file_enumerate_children (file,
					  G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, NULL);

  while (enumerator != NULL)
    {
      GFile *child;

      if (!g_file_enumerator_iterate (enumerator, NULL, &child, NULL, error))
        return FALSE;
      if (child == NULL)
        break;

      /* Don't follow symlinks! */
      if (g_file_query_file_type (child, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  NULL) == G_FILE_TYPE_SYMBOLIC_LINK)
	{
	  g_file_delete (child, NULL, error);
	  continue;
	}

      if (!rm_rf (child, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static gchar *
r_hex_encode(const guint8 *raw, size_t len)
{
  const char hex_chars[] = "0123456789abcdef";
  gchar *hex = NULL;

  g_assert(raw != NULL);
  g_assert(len > 0);

  len *= 2;
  hex = g_malloc0(len+1);
  for (size_t i = 0; i < len; i += 2) {
    hex[i] = hex_chars[(raw[i/2] >> 4)];
    hex[i+1] = hex_chars[(raw[i/2] & 0xf)];
  }

  return hex;
}

static gboolean
output_stream_write_bytes_all(GOutputStream *stream, GBytes *bytes,
			      GCancellable *cancellable, GError **error)
{
  const void *buffer;
  gsize count, bytes_written;

  buffer = g_bytes_get_data(bytes, &count);
  return g_output_stream_write_all(stream, buffer, count, &bytes_written,
				   cancellable, error);
}

static gboolean
output_stream_write_uint64_all(GOutputStream *stream, guint64 data,
			       GCancellable *cancellable, GError **error)
{
  gsize bytes_written;
  gboolean res;

  data = GUINT64_TO_BE(data);
  res = g_output_stream_write_all(stream, &data, sizeof(data), &bytes_written,
				  cancellable, error);
  g_assert(bytes_written == sizeof(data));
  return res;
}

static GKeyFile *
prepare_manifest(const tiu_manifest *mf)
{
  g_autoptr(GKeyFile) key_file = NULL;

  key_file = g_key_file_new();
  g_key_file_set_string(key_file, "tiu", "format", mf->format);
  if (mf->verity_hash)
    g_key_file_set_string(key_file, "tiu", "verity-hash", mf->verity_hash);
  if (mf->verity_salt)
    g_key_file_set_string(key_file, "tiu", "verity-salt", mf->verity_salt);
  if (mf->verity_size)
    g_key_file_set_uint64(key_file, "tiu", "verity-size", mf->verity_size);

  return g_steal_pointer(&key_file);
}

static gboolean
convert_manifest_to_mem(GBytes **mem, const tiu_manifest *mf)
{
  g_autoptr(GKeyFile) key_file = NULL;
  guint8 *data = NULL;
  gsize length = 0;

  g_return_val_if_fail(mem != NULL && *mem == NULL, FALSE);
  g_return_val_if_fail(mf != NULL, FALSE);

  key_file = prepare_manifest(mf);

  /* according to the docs, this never fails */
  data = (guint8*)g_key_file_to_data(key_file, &length, NULL);
  g_assert(data != NULL);
  g_assert(length > 0);

  *mem = g_bytes_new(data, length);

  return TRUE;
}


static gboolean
calc_verity (const gchar *tiufile, GError **error)
{
  GError *ierror = NULL;
  g_autoptr(GBytes) mem_manifest = NULL;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileIOStream) stream = NULL;
  GOutputStream *outstream = NULL; /* owned by the bundle stream */
  guint64 offset;
  tiu_manifest manifest;

  memset (&manifest, 0, sizeof(tiu_manifest));

  g_return_val_if_fail(tiufile, FALSE);

  file = g_file_new_for_path (tiufile);
  stream = g_file_open_readwrite (file, NULL, &ierror);
  if (stream == NULL) {
    g_propagate_prefixed_error(error, ierror,
			       "failed to open tiu archive for verity signature: ");
    return FALSE;
  }
  outstream = g_io_stream_get_output_stream(G_IO_STREAM(stream));

  if (!g_seekable_seek(G_SEEKABLE(stream),
		       0, G_SEEK_END, NULL, &ierror)) {
    g_propagate_prefixed_error(error, ierror,
			       "failed to seek to end of tiu archive: ");
    return FALSE;
  }

  offset = g_seekable_tell(G_SEEKABLE(stream));
  if (debug_flag)
    g_printf("Payload size: %" G_GUINT64_FORMAT " bytes.\n", offset);

  int bundlefd = g_file_descriptor_based_get_fd(G_FILE_DESCRIPTOR_BASED(outstream));
  guint8 salt[32] = {0};
  guint8 hash[32] = {0};
  off_t combined_size = 0;
  guint64 verity_size = 0;

  if (debug_flag)
    g_print("Create %s in 'verity' format...\n", tiufile);

  /* check we have a clean manifest */
  g_assert(manifest.verity_salt == NULL);
  g_assert(manifest.verity_hash == NULL);
  g_assert(manifest.verity_size == 0);

  /* dm-verity hash table generation */
  if (RAND_bytes((unsigned char *)&salt, sizeof(salt)) != 1)
    {
      /* XXX
      g_set_error(error,
		  R_SIGNATURE_ERROR,
		  R_SIGNATURE_ERROR_CREATE_SIG,
		  "failed to generate verity salt");
      */
      return FALSE;
    }
  if (offset % 4096 != 0)
    {
      /* XXX
      g_set_error(error,
		  R_SIGNATURE_ERROR,
		  R_SIGNATURE_ERROR_CREATE_SIG,
		  "squashfs size (%"G_GUINT64_FORMAT ") is not a multiple of 4096 bytes", offset);
      */
      return FALSE;
    }
  if (verity_create_or_verify_hash(0, bundlefd, offset/4096,
				   &combined_size, hash, salt) != 0)
    {
      /*
	g_set_error(error,
	R_SIGNATURE_ERROR,
	R_SIGNATURE_ERROR_CREATE_SIG,
	"failed to generate verity hash tree");
      */
      return FALSE;
    }
  /* for a squashfs <= 4096 bytes, we don't have a hash table */
  g_assert(combined_size*4096 >= (off_t)offset);
  verity_size = combined_size*4096 - offset;
  g_assert(verity_size % 4096 == 0);

  manifest.format = g_strdup("verity");
  manifest.verity_salt = r_hex_encode(salt, sizeof(salt));
  manifest.verity_hash = r_hex_encode(hash, sizeof(hash));
  manifest.verity_size = verity_size;

  convert_manifest_to_mem(&mem_manifest, &manifest);

  if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to seek to end of tiu file: ");
      return FALSE;
    }

  offset = g_seekable_tell(G_SEEKABLE(stream));
  if (debug_flag)
    g_printf ("Manifest offset: %" G_GUINT64_FORMAT " bytes.\n", offset);
  if (!output_stream_write_bytes_all(outstream, mem_manifest, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to append manifest to archive: ");
      return FALSE;
    }

  offset = g_seekable_tell(G_SEEKABLE(stream)) - offset;
  if (!output_stream_write_uint64_all(outstream, offset, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "failed to append manifest size to archive: ");
      return FALSE;
    }

  offset = g_seekable_tell(G_SEEKABLE(stream));

  if (debug_flag)
    g_printf ("tiu file size: %" G_GUINT64_FORMAT " bytes.\n", offset);

  return TRUE;
}

gboolean
create_image (const gchar *input, GError **gerror)
{
  const gchar *cachedir = "/var/cache/tiu";
  const gchar *tmpdir = NULL;
  econf_file *os_release = NULL;
  econf_err error;
  g_autofree gchar *lf = NULL;
  GError *ierror = NULL;

  if (debug_flag)
    g_printf("Start creating tiu update images from '%s'...\n",
	     input);

  gchar *scheme = g_uri_parse_scheme(input);

  if (is_remote_scheme(scheme))
    {
      lf = g_build_filename(cachedir, "input", NULL);

      if (g_mkdir_with_parents(cachedir, 0700) != 0)
        {
          g_set_error(gerror, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                      "Failed creating cache directory '%s'", cachedir);
          return FALSE;
        }

      if (debug_flag)
        g_printf("Remote URI detected, downloading input archive to '%s'...\n", lf);

      if (!network_init(&ierror))
        {
          g_propagate_error(gerror, ierror);
          return FALSE;
        }

      /* Make sure the file does not exist in the cache */
      g_remove (lf);
      if (!download_file(lf, input, DEFAULT_MAX_DOWNLOAD_SIZE, &ierror))
        {
          g_propagate_prefixed_error(gerror, ierror,
				     "Failed to download input archive %s: ",
				     input);
          return FALSE;
        }
      if (debug_flag)
        g_printf("Downloaded input archive to '%s'\n", lf);
    }
  else
    {
      lf = g_strdup(input);
    }

  if (!workdir_setup (lf, &tmpdir, &ierror))
    {
      g_propagate_error(gerror, ierror);
      cleanup (tmpdir);
      return FALSE;
    }

  char *libosrelease = g_strjoin ("/", tmpdir, "usr", "lib/os-release", NULL);
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
      /* XXX g_set_error */
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
      /* XXX g_set_error */
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

  char *pretty_name = NULL;
  if ((error = econf_getStringValue (os_release, "", "PRETTY_NAME",
				     &pretty_name)))
    {
      /* XXX g_set_error */
      fprintf (stderr,
	       "ERROR: couldn't read \"PRETTY_NAME\" from os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }

  if (debug_flag)
    g_printf("Product: \"%s-%s\" (%s)\n", product_name, version_id, pretty_name);

  char *product_id = NULL;
  if ((error = econf_getStringValue (os_release, "", "ID",
				     &product_id)))
    {
      /* XXX g_set_error */
      fprintf (stderr,
	       "ERROR: couldn't read \"ID\" from os-release: %s\n",
	       econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }

  /* Create required directories */
  gchar *dir = g_strjoin("/", tmpdir, "usr", ".snapshots", NULL);
  if (g_mkdir(dir, 0750) != 0)
    {
      g_set_error(gerror, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                  "Failed creating directory '%s' (%s)", dir, strerror(errno));
      return FALSE;
    }

  /* Cleanup subvolumes */
  if (debug_flag)
    g_print("Cleanup of subvolumes...\n");
  rm_dir_content("boot/writable", tmpdir, gerror);
  rm_dir_content("usr/local", tmpdir, gerror);

  gchar *pvers = g_strjoin("-", product_name, version_id, NULL);
  gchar *pvers_idx = g_strjoin(".", pvers, "caidx", NULL);
  gchar *pvers_str = g_strjoin(".", pvers, "castr", NULL);
  gchar *pvers_tar = g_strjoin(".", pvers, "catar", NULL);

  if (!casync_make (tmpdir, pvers_idx, pvers_str, gerror))
    {
      if (debug_flag)
	fprintf (stderr, "ERROR: no caidx archive created, aborting...\n");
      cleanup (tmpdir);
      return FALSE;
    }

  if (!casync_make (tmpdir, pvers_tar, NULL, gerror))
    {
      if (debug_flag)
	fprintf (stderr, "ERROR: no catar archive created, aborting...\n");
      cleanup (tmpdir);
      return FALSE;
    }

  econf_file *manifest;
  if ((error = econf_newKeyFile(&manifest, '=', '#')))
    {
      if (debug_flag)
	fprintf (stderr, "ERROR: couldn't create manifest file: %s\n",
		 econf_errString(error));
      cleanup (tmpdir);
      return FALSE;
    }

  econf_setStringValue(manifest, "global", "ID", product_id);
  econf_setStringValue(manifest, "global", "ARCH", "x86_64"); /* XXX */
  econf_setStringValue(manifest, "global", "FULL_NAME", pretty_name);
  econf_setStringValue(manifest, "global", "NAME", product_name);
  econf_setStringValue(manifest, "global", "VERSION", version_id);
  econf_setStringValue(manifest, "update", "MIN_VERSION", "20220101");
  econf_setStringValue(manifest, "update", "FORMAT", "catar");
  econf_setStringValue(manifest, "update", "ARCHIVE", pvers_tar);
  econf_writeFile(manifest, tmpdir, "manifest.tiu");

  gchar *manifest_file = g_strjoin("/", tmpdir, "manifest.tiu", NULL);
  gchar *output_tiutar = g_strjoin(".", pvers, "tiutar", NULL);
  gchar *output_tiuidx = g_strjoin(".", pvers, "tiuidx", NULL);

  if (!mksquashfs (manifest_file, pvers_tar, NULL, output_tiutar, gerror))
    {
      cleanup (tmpdir);
      return FALSE;
    }

  /* overwrite some values and create a tiuidx file */
  econf_setStringValue(manifest, "update", "FORMAT", "caidx");
  econf_setStringValue(manifest, "update", "ARCHIVE", pvers_idx);
  econf_writeFile(manifest, tmpdir, "manifest.tiu");

  if (!mksquashfs (manifest_file, pvers_idx, NULL, output_tiuidx, gerror))
    {
      cleanup (tmpdir);
      return FALSE;
    }

  /* XXX error handling */
  if (!calc_verity(output_tiutar, gerror))
    {
      cleanup (tmpdir);
      return FALSE;
    }
  if (!calc_verity(output_tiuidx, gerror))
    {
      cleanup (tmpdir);
      return FALSE;
    }

  if (!workdir_destroy (tmpdir, NULL))
    {
      /* XXX g_set_error */
      return FALSE;
    }

  return TRUE;
}

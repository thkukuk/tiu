#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <glib/gprintf.h>
#include <gio/gfiledescriptorbased.h>
#include <openssl/cms.h>
#include <openssl/x509.h>

#include "tiu.h"
#include "verity_hash.h"

#ifndef SQUASHFS_MAGIC
#define SQUASHFS_MAGIC 0x73717368
#endif

static gboolean
is_remote_scheme (const gchar *scheme)
{
  return (g_strcmp0(scheme, "http") == 0) ||
    (g_strcmp0(scheme, "https") == 0) ||
    (g_strcmp0(scheme, "sftp") == 0) ||
    (g_strcmp0(scheme, "ftp") == 0);
}

/*
  Attempts to read and verify the squashfs magic to verify having
  a valid tiu archive.
*/
static gboolean
input_stream_check_tiu_identifier(GInputStream *stream, GError **error)
{
  GError *ierror = NULL;
  guint32 squashfs_id;
  gsize bytes_read;

  g_return_val_if_fail(stream, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  if (!g_input_stream_read_all(stream, &squashfs_id, sizeof(squashfs_id),
			       &bytes_read, NULL, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  if (bytes_read != sizeof(squashfs_id))
    {
      g_set_error(error,G_IO_ERROR,G_IO_ERROR_PARTIAL_INPUT,
		  "Only %"G_GSIZE_FORMAT " of %zu bytes read",
		  bytes_read, sizeof(squashfs_id));
      return FALSE;
    }

  if (squashfs_id != GUINT32_TO_LE(SQUASHFS_MAGIC))
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_IDENTIFIER, "Invalid identifier. Did you pass a valid RAUC bundle?");
      */
      return FALSE;
    }

  return TRUE;
}

static gboolean
input_stream_read_uint64_all(GInputStream *stream, guint64 *data,
			     GCancellable *cancellable,
			     GError **error)
{
  guint64 tmp;
  gsize bytes_read;
  gboolean res;

  res = g_input_stream_read_all(stream, &tmp, sizeof(tmp), &bytes_read,
				cancellable, error);
  g_assert(bytes_read == sizeof(tmp));
  *data = GUINT64_FROM_BE(tmp);
  return res;
}

static gboolean
input_stream_read_bytes_all(GInputStream *stream, GBytes **bytes,
			    gsize count, GCancellable *cancellable,
			    GError **error)
{
  g_autofree void *buffer = NULL;
  gsize bytes_read;

  g_assert_cmpint(count, !=, 0);

  buffer = g_malloc0(count);

  if (!g_input_stream_read_all(stream, buffer, count, &bytes_read,
			       cancellable, error))
    return FALSE;

  g_assert(bytes_read == count);
  *bytes = g_bytes_new_take(g_steal_pointer(&buffer), count);
  return TRUE;
}

/*
 * This checks if another user could get or already has write access
 * the tiu archive.
 *
 * Prohibited are:
 * - ownership or permissions that allow other users to open it for writing
 * // - storage on unsafe filesystems such as FUSE or NFS, where the data
 * //   is supplied by an untrusted source (the rootfs is explicitly
 * //    trusted, though)
 * // - storage on an filesystem mounted from a block device with a non-root owner
 * - existing open file descriptors (via F_SETLEASE)
 *
 * XXX move to own file
 */
static gboolean
check_access(int fd, GError **error)
{
  struct stat archive_stat;
  struct statfs archive_statfs;
  mode_t perm = 0;

  if (debug_flag)
    g_printf("Check access rights of tiu archive...\n");

  if (fstat(fd, &archive_stat))
    {
      int err = errno;
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "failed to fstat archive: %s", g_strerror(err));
      return FALSE;
    }
  perm = archive_stat.st_mode & 07777;

  if (fstatfs(fd, &archive_statfs))
    {
      int err = errno;
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "failed to fstatfs archive: %s", g_strerror(err));
      return FALSE;
    }

  /* unexpected file type */
  if (!S_ISREG(archive_stat.st_mode))
    {
      /* XXX g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNSAFE, "unsafe bundle (not a regular file)"); */
      return FALSE;
    }

  /* owned by other user (except root) */
  if ((archive_stat.st_uid != 0) && (archive_stat.st_uid != geteuid()))
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNSAFE, "unsafe bundle uid %ju", (uintmax_t)archive_stat.st_uid);
      */
      return FALSE;
    }

  /* unsafe permissions (not a subset of 0755) */
  if (perm & ~(0755))
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNSAFE, "unsafe bundle permissions 0%jo", (uintmax_t)perm);
      */
      return FALSE;
    }

  /* XXX check if the filesystem is safe and the underlying device acceptable */

  /* check for other open file descriptors via leases (see fcntl(2)) */
  if (fcntl(fd, F_SETLEASE, F_RDLCK))
    {
      const gchar *message = NULL;
      int err = errno;
      if (err == EAGAIN)
	{
	  message = "EAGAIN: existing open file descriptor";
	}
      else if (err == EACCES)
	{
	  message = "EACCES: missing capability CAP_LEASE?";
	}
      else
	{
	  message = g_strerror(err);
	}
      g_fprintf(stderr, "could not ensure exclusive tiu archive access (F_SETLEASE): %s", message);
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNSAFE,
	 "could not ensure exclusive tiu archive access (F_SETLEASE): %s", message);
      */
      return FALSE;
    }
  if (fcntl(fd, F_GETLEASE) != F_RDLCK)
    {
      /* XXX
	 int err = errno;

	 g_set_error(error,
	 R_BUNDLE_ERROR,
	 R_BUNDLE_ERROR_UNSAFE,
	 "could not ensure exclusive bundle access (F_GETLEASE): %s", g_strerror(err));
      */
      return FALSE;
  }
  if (fcntl(fd, F_SETLEASE, F_UNLCK))
    {
      int err = errno;
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "failed to remove file lease on tiu archive: %s", g_strerror(err));
      return FALSE;
    }

  return TRUE;
}

static gboolean
convert_manifest_from_mem(GBytes *mem, tiu_manifest **manifest, GError **error)
{
  GError *ierror = NULL;
  g_autoptr(GKeyFile) key_file = NULL;
  const gchar *data;
  gsize length;

  data = g_bytes_get_data(mem, &length);
  if (data == NULL)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_ERROR_NO_DATA, "No data available");
      */
      return FALSE;
    }

  key_file = g_key_file_new();

  if (!g_key_file_load_from_data(key_file, data, length, G_KEY_FILE_NONE, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  *manifest = g_new0(tiu_manifest, 1);

  (*manifest)->format = g_key_file_get_string(key_file, "tiu", "format", NULL);
  (*manifest)->verity_hash = g_key_file_get_string(key_file, "tiu", "verity-hash", NULL);
  (*manifest)->verity_salt = g_key_file_get_string(key_file, "tiu", "verity-salt", NULL);
  (*manifest)->verity_size = g_key_file_get_uint64(key_file, "tiu", "verity-size", NULL);

  return TRUE;
}

static guint8 *
r_hex_decode(const gchar *hex, size_t len)
{
  g_autofree guint8 *raw = NULL;
  size_t input_len = 0;

  g_assert(hex != NULL);

  input_len = strlen(hex);
  if (input_len != (len * 2))
    return NULL;

  raw = g_malloc0(len);
  for (size_t i = 0; i < len; i++)
    {
      gint upper = g_ascii_xdigit_value(hex[i*2]);
      gint lower = g_ascii_xdigit_value(hex[i*2+1]);

      if ((upper < 0) || (lower < 0))
	return NULL;

      raw[i] = upper << 4 | lower;
    }

  return g_steal_pointer(&raw);
}

static gboolean
check_manifest(const tiu_manifest *mf, GError **error __attribute__((unused)))
{
  guint8 *tmp;

  if (debug_flag)
    g_printf ("Verify manifest...\n");

  if ((mf->format == NULL) ||
      (strcmp (mf->format, "verity") != 0))
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR,
	 "Unsupported format for external manifest");
      */
      return FALSE;
    }

  if (!mf->verity_hash)
    {
      /* XXX
      g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Missing hash for verity bundle");
      */
      return FALSE;
    }
  tmp = r_hex_decode(mf->verity_hash, 32);
  if (!tmp)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Invalid hash for verity bundle");
      */
      return FALSE;
    }
  g_free(tmp);

  if (!mf->verity_salt)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Missing salt for verity bundle");
      */
      return FALSE;
    }
  tmp = r_hex_decode(mf->verity_salt, 32);
  if (!tmp)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Invalid salt for verity bundle");
      */
      return FALSE;
    }
  g_free(tmp);

  if (!mf->verity_size)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Missing size for verity bundle");
      */
      return FALSE;
    }

  if (mf->verity_size % 4096)
    {
      /* XXX
	 g_set_error(error, R_MANIFEST_ERROR, R_MANIFEST_CHECK_ERROR, "Unaligned size for verity bundle");
      */
      return FALSE;
    }

  return TRUE;
}

gboolean
check_tiu_archive(const gchar *tiuname, TIUBundle **bundle, GError **error)
{
  GError *ierror = NULL;
  g_autoptr(GFile) tiufile = NULL;
  g_autoptr(GFileInfo) tiuinfo = NULL;
  g_autoptr(TIUBundle) ibundle = g_new0(TIUBundle, 1);
  g_autoptr(GBytes) manifest_bytes = NULL;
  guint64 sigsize;
  goffset offset;

  gchar *tiuscheme = g_uri_parse_scheme(tiuname);

  if (is_remote_scheme(tiuscheme))
    {
      ibundle->origpath = g_strdup(tiuname);
      ibundle->path = g_build_filename(g_get_tmp_dir(), "tiu", NULL);

      if (debug_flag)
	g_printf("Remote URI detected, downloading tiu archive to '%s'...\n", ibundle->path);

#if 0 /* XXX */
      if (!download_file(path, origpath, &ierror))
	{
	  g_propagate_prefixed_error(error, ierror, "Failed to download tiu archive %s: ", origpath);
	  return FALSE;
	}
#endif
      if (debug_flag)
	g_printf("Downloaded tiu archive to '%s'\n", ibundle->path);
    }
  else
    {
      ibundle->path = g_strdup(tiuname);
    }

  if (debug_flag)
    g_printf("Verifying tiu payload...\n");

  tiufile = g_file_new_for_path(tiuname);
  ibundle->stream = G_INPUT_STREAM(g_file_read(tiufile, NULL, &ierror));
  if (ibundle->stream == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
                                "Failed to open tiu image for reading: ");
      return FALSE;
    }

  int fd = g_file_descriptor_based_get_fd(G_FILE_DESCRIPTOR_BASED(ibundle->stream));

  if (!check_access(fd, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  tiuinfo = g_file_input_stream_query_info(G_FILE_INPUT_STREAM(ibundle->stream),
					   G_FILE_ATTRIBUTE_STANDARD_TYPE,
					   NULL, &ierror);
  if (tiuinfo == NULL)
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to query tiu archive info: ");
      return FALSE;
    }

  if (g_file_info_get_file_type(tiuinfo) != G_FILE_TYPE_REGULAR)
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_UNSAFE,
	 "Bundle is not a regular file");
      */
      return FALSE;
    }

  if (!input_stream_check_tiu_identifier(ibundle->stream, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to check tiu archive identifier: ");
      return FALSE;
    }

  offset = sizeof(sigsize);
  if (!g_seekable_seek(G_SEEKABLE(ibundle->stream),
		       -offset, G_SEEK_END, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to seek to end of tiu archive: ");
      return FALSE;
    }
  offset = g_seekable_tell(G_SEEKABLE(ibundle->stream));

  if (!input_stream_read_uint64_all(ibundle->stream, &sigsize, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to read signature size from tiu archive: ");
      return FALSE;
    }

  if (sigsize == 0)
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
	 "Signature size is 0");
      */
      return FALSE;
    }
  /* sanity check: signature should be smaller than tiu archive size */
  if (sigsize > (guint64)offset)
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
	 "Signature size (%"G_GUINT64_FORMAT ") exceeds bundle size", sigsize);
      */
      return FALSE;
    }
  /* sanity check: signature should be smaller than 64kiB */
  if (sigsize > 0x4000000)
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_SIGNATURE,
	 "Signature size (%"G_GUINT64_FORMAT ") exceeds 64KiB", sigsize);
      */
      return FALSE;
    }

  offset -= sigsize;
  ibundle->size = offset;

  if (!g_seekable_seek(G_SEEKABLE(ibundle->stream),
		       offset, G_SEEK_SET, NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to seek to start of tiu archive signature: ");
      return FALSE;
    }

  if (debug_flag)
    g_printf("Read signature data...\n");

  if (!input_stream_read_bytes_all(ibundle->stream, &ibundle->sigdata, sigsize,
				   NULL, &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to read signature from tiu archive: ");
      return FALSE;
    }

#if 0 /* XXX */
  CMS_ContentInfo *cms = NULL;
  X509_STORE *store = setup_x509_store(NULL, NULL, &ierror);
  if (!store)
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }
#endif

  if (debug_flag)
    g_printf("Verifying tiu archive signature...\n");

#if 0 /* XXX */
  if (!cms_verify_sig(sigdata, store, &cms, &manifest_bytes, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  res = cms_get_cert_chain(cms, store, &ibundle->verified_chain, &ierror);
  if (!res) {
    g_propagate_error(error, ierror);
    goto out;
  }

  X509_STORE_free(store);
  CMS_ContentInfo_free(cms);
#else
  manifest_bytes = ibundle->sigdata;
#endif

  if (manifest_bytes == NULL)
    {
      /* XXX set error */
      fprintf (stderr, "No manifest found\n");
      return FALSE;
    }

  if (!convert_manifest_from_mem(manifest_bytes, &ibundle->manifest,
				 &ierror))
    {
      g_propagate_prefixed_error(error, ierror,
				 "Failed to load manifest: ");
      return FALSE;
    }

  if (!check_manifest(ibundle->manifest, &ierror))
    {
      g_propagate_error(error, ierror);
      return FALSE;
    }

  guint8 *root_digest = r_hex_decode(ibundle->manifest->verity_hash, 32);
  guint8 *salt = r_hex_decode(ibundle->manifest->verity_salt, 32);
  off_t data_size = ibundle->size - ibundle->manifest->verity_size;
  g_assert(root_digest);
  g_assert(salt);
  g_assert(data_size % 4096 == 0);

  if (debug_flag)
    g_printf("Verifying tiu archive verity hash...\n");

  if (verity_create_or_verify_hash(1, fd, data_size/4096, NULL, root_digest, salt))
    {
      /* XXX
	 g_set_error(error, R_BUNDLE_ERROR, R_BUNDLE_ERROR_PAYLOAD,
	 "bundle payload is corrupted");
      */
      return FALSE;
    }

  *bundle = g_steal_pointer(&ibundle);

  return TRUE;
}

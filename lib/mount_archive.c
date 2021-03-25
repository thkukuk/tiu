#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <glib/gstdio.h>
#include <gio/gfiledescriptorbased.h>
#include <linux/loop.h>

#include "tiu.h"
#include "dm.h"

static gboolean
setup_loop(gint fd, gint *loopfd_out, gchar **loopname_out,
	   goffset size, GError **error)
{
  gboolean res = FALSE;
  gint controlfd = -1;
  g_autofree gchar *loopname = NULL;
  gint loopfd = -1, looprc;
  guint tries;
  struct loop_info64 loopinfo;

  g_return_val_if_fail(fd >= 0, FALSE);
  g_return_val_if_fail(loopfd_out != NULL, FALSE);
  g_return_val_if_fail(loopname_out != NULL && *loopname_out == NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  memset (&loopinfo, 0, sizeof (loopinfo));

  controlfd = open("/dev/loop-control", O_RDWR|O_CLOEXEC);
  if (controlfd < 0) {
    int err = errno;
    g_set_error(error,
		G_FILE_ERROR,
		g_file_error_from_errno(err),
		"Failed to open /dev/loop-control: %s", g_strerror(err));
    res = FALSE;
    goto out;
  }

  for (tries = 10; tries > 0; tries--) {
    gint loopidx;

    g_clear_pointer(&loopname, g_free);
    if (loopfd >= 0)
      g_close(loopfd, NULL);

    loopidx = ioctl(controlfd, LOOP_CTL_GET_FREE);
    if (loopidx < 0) {
      int err = errno;
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "Failed to get free loop device: %s", g_strerror(err));
      res = FALSE;
      goto out;
    }

    loopname = g_strdup_printf("/dev/loop%d", loopidx);

    loopfd = open(loopname, O_RDWR|O_CLOEXEC);
    if (loopfd < 0) {
      int err = errno;
      /* is this loop dev gone already? */
      if ((err == ENOENT) || (err == ENXIO))
	continue; /* retry */
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "Failed to open %s: %s", loopname, g_strerror(err));
      res = FALSE;
      goto out;
    }

    looprc = ioctl(loopfd, LOOP_SET_FD, fd);
    if (looprc < 0) {
      int err = errno;
      /* is this loop dev is already in use by someone else? */
      if (err == EBUSY)
	continue; /* retry */
      g_set_error(error,
		  G_FILE_ERROR,
		  g_file_error_from_errno(err),
		  "Failed to set loop device file descriptor: %s", g_strerror(err));
      res = FALSE;
      goto out;
    } else {
      break; /* claimed a loop dev */
    }
  }

  if (!tries) {
    g_set_error(error,
		G_FILE_ERROR,
		g_file_error_from_errno(EBUSY),
		"Failed to find free loop device");
    res = FALSE;
    goto out;
  }

  loopinfo.lo_sizelimit = size;
  loopinfo.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;

  do {
    looprc = ioctl(loopfd, LOOP_SET_STATUS64, &loopinfo);
  } while (looprc && errno == EAGAIN);
  if (looprc < 0) {
    int err = errno;
    g_set_error(error,
		G_FILE_ERROR,
		g_file_error_from_errno(err),
		"Failed to set loop device configuration: %s", g_strerror(err));
    ioctl(loopfd, LOOP_CLR_FD, 0);
    res = FALSE;
    goto out;
  }

  looprc = ioctl(loopfd, LOOP_SET_BLOCK_SIZE, 4096);
  if (looprc < 0) {
    g_debug("Failed to set loop device block size to 4096, continuing");
  }

  g_message("Configured loop device '%s' for %" G_GOFFSET_FORMAT " bytes", loopname, size);

  *loopfd_out = loopfd;
  loopfd = -1;
  *loopname_out = g_steal_pointer(&loopname);
  res = TRUE;

 out:
  if (loopfd >= 0)
    g_close(loopfd, NULL);
  if (controlfd >= 0)
    g_close(controlfd, NULL);
  return res;
}

gboolean
umount_tiu_archive (TIUBundle *bundle, GError **error)
{
  g_return_val_if_fail(bundle != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  g_assert_nonnull(bundle->mount_point);

  if (umount2 (bundle->mount_point, UMOUNT_NOFOLLOW))
    {
      int err = errno;
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to umount tiu archive: %s", g_strerror(err));
      return FALSE;
    }

  g_rmdir(bundle->mount_point);
  g_clear_pointer(&bundle->mount_point, g_free);

  return TRUE;
}

gboolean
mount_tiu_archive(TIUBundle *bundle, GError **error)
{
  const gchar *mountpoint = "/var/lib/tiu/mount";
  GError *ierror = NULL;
  g_autofree gchar *loopname = NULL;
  gint bundlefd = -1;
  gint loopfd = -1;
  gboolean res = FALSE;

  g_return_val_if_fail(bundle != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  g_assert_null(bundle->mount_point);

  if (!g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
    {
      gint ret;
      ret = g_mkdir_with_parents(mountpoint, 0700);

      if (ret != 0)
	{
	  g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
		      "Failed creating mount path '%s'", mountpoint);
	  return FALSE;
	}
    }

  if (debug_flag)
    g_printf("Mounting tiu archive '%s' to '%s'", bundle->path, mountpoint);

  bundlefd = g_file_descriptor_based_get_fd(G_FILE_DESCRIPTOR_BASED(bundle->stream));
  res = setup_loop(bundlefd, &loopfd, &loopname, bundle->size, &ierror);
  if (!res)
    {
      g_propagate_error(error, ierror);
      goto out;
    }

  g_autoptr(GError) ierror_dm = NULL;
  g_autoptr(RaucDMVerity) dm_verity = new_dm_verity();

  dm_verity->lower_dev = g_strdup(loopname);
  dm_verity->data_size = bundle->size - bundle->manifest->verity_size;
  dm_verity->root_digest = g_strdup(bundle->manifest->verity_hash);
  dm_verity->salt = g_strdup(bundle->manifest->verity_salt);

  res = setup_dm_verity(dm_verity, &ierror);
  if (!res)
    {
      g_propagate_error(error, ierror);
      goto out;
    }

  if (mount(dm_verity->upper_dev, mountpoint, "squashfs",
	    MS_NODEV | MS_NOSUID | MS_RDONLY, NULL))
    {
      int err = errno;
      g_set_error(&ierror, G_FILE_ERROR, g_file_error_from_errno(err),
		  "failed to mount bundle: %s", g_strerror(err));
      res = FALSE;
    }

  if (!remove_dm_verity(dm_verity, TRUE, &ierror_dm))
    {
      g_warning("failed to mark dm verity device for removal: %s", ierror_dm->message);
      g_clear_error(&ierror_dm);
    }

  if (!res)
    {
      g_propagate_error(error, ierror);
      goto out;
    }

  bundle->mount_point = g_strdup(mountpoint);
  res = TRUE;

 out:
  if (loopfd >= 0)
    g_close(loopfd, NULL);
  return res;
}

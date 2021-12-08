#include "tiu-internal.h"

gboolean debug_flag = FALSE;
gboolean verbose_flag = TRUE;

void
free_manifest(tiu_manifest *manifest)
{
  g_return_if_fail(manifest);

  g_free(manifest->format);
  g_free(manifest->verity_hash);
  g_free(manifest->verity_salt);
  g_free(manifest);
}

void
free_bundle(TIUBundle *bundle)
{
  g_return_if_fail(bundle);

#if 0 /* XXX */
  /* In case of a temporary download artifact, remove it. */
  if (bundle->origpath)
    if (g_remove(bundle->path) != 0)
      {
	g_warning("failed to remove download artifact %s: %s\n", bundle->path, g_strerror(errno));
      }
#endif

  g_free(bundle->path);
  if (bundle->stream)
    g_object_unref(bundle->stream);
  g_bytes_unref(bundle->sigdata);
  g_free(bundle->mount_point);
  if (bundle->manifest)
    free_manifest(bundle->manifest);
  g_free(bundle);
}

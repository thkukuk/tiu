

#include <glib.h>
#include <gio/gio.h>

#include "tiu-internal.h"

/*
   Recursively delete @file and its children. @file may be a file or a
   directory.
*/
gboolean
rm_rf (GFile *file, GCancellable *cancellable, GError **error)
{
  g_autoptr(GFileEnumerator) enumerator = NULL;

  enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          cancellable, NULL);

  while (enumerator != NULL)
    {
      GFile *child;

      if (!g_file_enumerator_iterate (enumerator, NULL, &child, cancellable, error))
        return FALSE;
      if (child == NULL)
        break;

      /* Don't follow symlinks! */
      if (g_file_query_file_type (child, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  cancellable) == G_FILE_TYPE_SYMBOLIC_LINK)
	{
	  g_file_delete (child, cancellable, error);
	  continue;
	}

      if (!rm_rf (child, cancellable, error))
        return FALSE;
    }

  return g_file_delete (file, cancellable, error);
}

/*
   Recursively delete all files and its children in the @dir directory.
   directory.
*/
gboolean
rmdir_rf (const gchar *dir, GCancellable *cancellable, GError **error)
{
  g_autoptr(GFileEnumerator) enumerator = NULL;

  GFile *file = g_file_new_for_path (dir);
  enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          cancellable, NULL);
  g_object_unref(file);

  while (enumerator != NULL)
    {
      GFile *child;

      if (!g_file_enumerator_iterate (enumerator, NULL, &child, cancellable, error))
        return FALSE;
      if (child == NULL)
        break;

      /* Don't follow symlinks! */
      if (g_file_query_file_type (child, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				  cancellable) == G_FILE_TYPE_SYMBOLIC_LINK)
	{
	  g_file_delete (child, cancellable, error);
	  continue;
	}

      if (!rm_rf (child, cancellable, error))
        return FALSE;
    }

  return TRUE;
}

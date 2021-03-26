#pragma once

#include <glib.h>

/**
 * Network initalization routine.
 *
 * Sets up libcurl.
 *
 * @param error return location for a GError, or NULL
 *
 * @return TRUE if succeeded, FALSE if failed
 */
gboolean network_init(GError **error)
G_GNUC_WARN_UNUSED_RESULT;

gboolean download_file(const gchar *target, const gchar *url, goffset limit, GError **error)
G_GNUC_WARN_UNUSED_RESULT;

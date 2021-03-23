
#include <glib.h>

/* External */
extern gboolean create_images (const gchar *input, GError **error);

/* Internal, move in separate header */
#include <gio/gio.h>
extern gboolean workdir_setup (const gchar *contentpath, const gchar **workdir, GError **error);
extern gboolean workdir_destroy (const gchar *workdir, GError **error);
extern gboolean casync_make (const gchar *inputdir, const gchar *outfile, const gchar *store, GError **error);
extern gboolean rm_rf (GFile *file, GCancellable *cancellable, GError **error);

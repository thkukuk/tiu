
#include <glib.h>

/* Default maximum downloadable bundle size (800 MiB) */
#define DEFAULT_MAX_DOWNLOAD_SIZE 800*1024*1024

typedef struct {
  gchar *format;
  gchar *verity_salt;
  gchar *verity_hash;
  guint64 verity_size;
} tiu_manifest;

extern void free_manifest(tiu_manifest *manifest);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(tiu_manifest, free_manifest)


/* External */
extern gboolean debug_flag;
extern gboolean extract_tiu_image(const gchar *tiuname, const gchar *outputdir, GError **error);
extern gboolean create_images (const gchar *input, GError **error);

/* Internal, move in separate header */

#include <gio/gio.h>

typedef struct {
  gchar *path;
  gchar *origpath;
  GInputStream *stream;
  goffset size;
  GBytes *sigdata;
  tiu_manifest *manifest;
  gchar *mount_point;
} TIUBundle;

extern void free_bundle(TIUBundle *bundle);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(TIUBundle, free_bundle)

extern gboolean workdir_setup (const gchar *contentpath, const gchar **workdir, GError **error);
extern gboolean workdir_destroy (const gchar *workdir, GError **error);
extern gboolean casync_make (const gchar *inputdir, const gchar *outfile, const gchar *store, GError **error);
extern gboolean casync_extract(const gchar *input, const gchar *dest, const gchar *store, const gchar *seed, GError **error);
extern gboolean rm_rf (GFile *file, GCancellable *cancellable, GError **error);
extern gboolean check_tiu_archive(const gchar *tiuname, TIUBundle **bundle, GError **error);
extern gboolean umount_tiu_archive (TIUBundle *bundle, GError **error);
extern gboolean mount_tiu_archive(TIUBundle *bundle, GError **error);

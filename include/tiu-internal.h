/*  Copyright (C) 2021  Thorsten Kukuk <kukuk@suse.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <glib.h>
#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG "/var/log/tiu/"

/* Default maximum downloadable bundle size (800 MiB) */
#define DEFAULT_MAX_DOWNLOAD_SIZE 800*1024*1024

/* Manifest description file */
#define MANIFEST_TIU "manifest.tiu"

#define CATAR "catar"
#define CAIDX "caidx"
#define CASTR "castr"

typedef struct {
  gchar *format;
  gchar *verity_salt;
  gchar *verity_hash;
  guint64 verity_size;
} tiu_manifest;

extern void free_manifest(tiu_manifest *manifest);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(tiu_manifest, free_manifest)

extern gboolean verbose_flag;
extern gboolean debug_flag;

typedef struct TIUBundle {
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
extern gboolean desync_tar (const gchar *inputdir, const gchar *outfile, const gchar *store, GError **error);
extern gboolean desync_untar(const gchar *input, const gchar *dest, const gchar *store, GError **error);
extern gboolean rm_rf (GFile *file, GCancellable *cancellable, GError **error);
extern gboolean rmdir_rf (const gchar *dir, GCancellable *cancellable, GError **error);

#ifdef __cplusplus
}
#endif

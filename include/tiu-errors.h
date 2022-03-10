#include <glib.h>

#define T_ARCHIVE_ERROR t_archive_error_quark()
GQuark t_archive_error_quark(void);

typedef enum {
  T_ARCHIVE_ERROR_IDENTIFIER,
  T_ARCHIVE_ERROR_UNSAFE,
  T_ARCHIVE_ERROR_PAYLOAD,
  T_ARCHIVE_ERROR_SIGNATURE,
  T_ARCHIVE_ERROR_NO_DATA,
} TArchiveError;

#define T_MANIFEST_ERROR t_manifest_error_quark()
GQuark t_manifest_error_quark(void);

typedef enum {
  T_MANIFEST_CHECK_ERROR,
  T_MANIFEST_ERROR_NO_DATA,
} TManifestError;


#include "tiu-errors.h"

GQuark
t_archive_error_quark(void)
{
  return g_quark_from_static_string("t-archive-error-quark");
}

GQuark
t_manifest_error_quark(void)
{
  return g_quark_from_static_string("t-manifest-error-quark");
}

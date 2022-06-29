/* This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   in Version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <unistd.h>
#include <glib/gprintf.h>
#include <libeconf.h>
#include "tiu.h"
#include "tiu-internal.h"

#define INSTALL "install"
#define EXTRACT "extract"
#define UPDATE "update"

static gchar *archive_file = NULL;
static gchar *target_dir = NULL;
static gboolean force_installation = false;
static GOptionEntry entries_extract[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &archive_file, "swu archive", "FILENAME"},
  {"output", 'o', 0, G_OPTION_ARG_FILENAME, &target_dir, "target directory", "DIRECTORY"},
  {0}
};
static GOptionGroup *extract_group;

static gchar *device = NULL;
static GOptionEntry entries_install[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &archive_file, "swu archive", "FILENAME"},
  {"device", 'd', 0, G_OPTION_ARG_FILENAME, &device, "installation device", "DEVICE"},
  {"force", '\0', 0, G_OPTION_ARG_NONE, &force_installation, "no user confirmation for disk erasing", NULL},
  {0}
};
static GOptionGroup *install_group;

static GOptionEntry entries_update[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &archive_file, "swu archive", "FILENAME"},
  {0}
};
static GOptionGroup *update_group;

static void
init_group_options (void)
{
  extract_group = g_option_group_new(EXTRACT, "Extract options:",
				    "Show help options for extract", NULL, NULL);
  g_option_group_add_entries(extract_group, entries_extract);

  install_group = g_option_group_new(INSTALL, "Installation options:",
				    "Show help options for install", NULL, NULL);
  g_option_group_add_entries(install_group, entries_install);

  update_group = g_option_group_new(UPDATE, "Update options:",
				    "Show help options for update", NULL, NULL);
  g_option_group_add_entries(update_group, entries_update);
}

static void
read_config(const gchar *kind, gchar **archive, gchar **archive_md5sum,
	    gchar **disk_layout)
{
   econf_file *key_file = NULL;
   econf_err ecerror;
   ecerror = econf_readDirs (&key_file,
			     "/usr/share/tiu",
			     "/etc",
			     "tiu",
			     "conf",
			     "=", "#");
   if (ecerror != ECONF_SUCCESS)
   {
      g_fprintf (stderr, "ERROR: While reading tiu.conf file: %s\n", econf_errString(ecerror));
      exit (1);
   }
   /* only read archive is not specified on commandline */
   if (*archive == NULL)
     {
       /* Try at first special *kind* group ("update", "install",...), if not set, use "global" */
       ecerror = econf_getStringValue(key_file, kind, "archive", archive);
       if (ecerror != ECONF_SUCCESS)
	 ecerror = econf_getStringValue(key_file, "global", "archive", archive);

       if (ecerror != ECONF_SUCCESS || archive == NULL)
	 {
	   fprintf (stderr, "ERROR: Cannot read -archive- entry from tiu.conf: %s\n", econf_errString(ecerror));
	   exit (1);
	 }
     }

   if (strcmp(kind, INSTALL) == 0)
   {
      ecerror = econf_getStringValue(key_file, kind, "disk_layout", disk_layout);
      if (ecerror != ECONF_SUCCESS)
	 ecerror = econf_getStringValue(key_file, "global", "disk_layout", disk_layout);

      if (ecerror != ECONF_SUCCESS || disk_layout == NULL)
      {
         fprintf (stderr, "ERROR: Cannot read -disk_layout- entry from tiu.conf for installation: %s\n",
		  econf_errString(ecerror));
	          exit (1);
      }
   } else {
     *disk_layout = NULL;
   }

   ecerror = econf_getStringValue(key_file, kind, "archive_md5sum", archive_md5sum);
   if (ecerror != ECONF_SUCCESS)
     econf_getStringValue(key_file, "global", "archive_md5sum", archive_md5sum);

   econf_free (key_file);
}

static gboolean
download_and_verify (const gchar *archive_name, const gchar *archive_md5sum, gchar **location)
{
  GError *error = NULL;

  if (!download_archive (archive_name, archive_md5sum, location, &error))
    {
      if (error)
	{
	  g_fprintf (stderr, "ERROR: %s\n", error->message);
	  g_clear_error (&error);
	}
      else
	g_fprintf (stderr, "ERROR: download of the archive failed!\n");
      return FALSE;
    }

#if 0 /* XXX */
  if (!check_swu_archive (*bundle, &error))
    {
      if (error)
	{
	  g_fprintf (stderr, "ERROR: %s\n", error->message);
	  g_clear_error (&error);
	}
      else
	g_fprintf (stderr, "ERROR: checking the archive failed!\n");
      return FALSE;
    }
#endif

  return TRUE;
}

int
main(int argc, char **argv)
{
  gboolean help = FALSE, version = FALSE;
  gchar *archive_md5sum = NULL;
  gchar *disk_layout = NULL;
  g_autoptr(GOptionContext) context = NULL;
  GError *error = NULL;
  GOptionEntry options[] = {
    {"debug", '\0', 0, G_OPTION_ARG_NONE, &debug_flag, "enable debug output", NULL},
    {"verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose_flag, "enable verbose output", NULL},
    {"version", '\0', 0, G_OPTION_ARG_NONE, &version, "display version", NULL},
    {"help", 'h', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
    {0}
  };

  /* disable remote VFS */
  g_assert(g_setenv("GIO_USE_VFS", "local", TRUE));

  init_group_options ();

  context = g_option_context_new("<COMMAND>");
  g_option_context_add_main_entries (context, options, NULL);
  g_option_context_set_description (context, "List of tiu commands:\n"
				    "  extract\tExtract a tiu archive\n"
				    "  install\tInstall a new system\n"
				    "  update\tUpdate current system\n"
				    );
  g_option_context_add_group (context, extract_group);
  g_option_context_add_group (context, install_group);
  g_option_context_add_group (context, update_group);

  if (!g_option_context_parse(context, &argc, &argv, &error))
    {
      g_printerr("%s\n", error->message);
      g_error_free(error);
      exit (1);
    }

  /* If debug is choosen, enable verbose logs, too */
  if (debug_flag)
    verbose_flag = 1;

  /* Generate log directory */
  if (g_mkdir_with_parents(LOG, 0700) != 0)
    {
       g_fprintf (stderr, "ERROR: Cannot create %s!\n", LOG);
       exit (1);
    }

  if (version)
    {
      g_print("TIU - Transactional Image Update Version %s\n", "0.1");
      exit (0);
    }

  if (argc <= 1)
    {
      g_fprintf (stderr, "ERROR: no argument given!\n");
      exit (1);
    }

  if (getuid () != 0)
    {
      fprintf (stderr, "ERROR: You must be root to run TIU\n");
      exit (1);
    }

#if 0 /* XXX */
  if (strcmp (argv[1], EXTRACT) == 0)
    {
      TIUBundle *bundle = NULL;

      read_config(EXTRACT, NULL, &archive_file, &archive_md5sum, &disk_layout,
		  &config_store);

      if (target_dir == NULL)
	{
	  fprintf (stderr, "ERROR: no target directory specified!\n");
	  exit (1);
	}

      if (!g_file_test(target_dir, G_FILE_TEST_IS_DIR))
	{
	  fprintf (stderr, "ERROR: target directory does not exist!\n");
	  exit (1);
	}

      if (!download_check_mount (archive_file, archive_md5sum, &bundle,
				 config_store, &chunk_store))
      	exit (1);

      if (!extract_image(bundle, target_dir, chunk_store, &error))
	{
	  if (error)
	    {
	      g_fprintf (stderr, "ERROR: %s\n", error->message);
	      g_clear_error (&error);
	    }
	  else
	    g_fprintf (stderr, "ERROR: extracting the archive failed!\n");
	  exit (1);
	}
    }
  else
#endif

    if (strcmp (argv[1], INSTALL) == 0)
      {
	gchar *location = NULL;

	if (!force_installation)
	  {
	    gchar answer='n';
	    int count=0;
	    do {
	      g_printf("All data of device %s will be deleted. Continue (y/n)? ", device);
	      count = scanf(" %c", &answer);
	      if (answer == 'n')
		exit (0);
	    } while (count!=1 || answer != 'y');
	  }

	read_config(INSTALL, &archive_file, &archive_md5sum, &disk_layout);

      if (device == NULL)
	{
	  g_fprintf (stderr, "ERROR: no device for installation specified!\n");
	  exit (1);
	}

      if (!download_and_verify (archive_file, archive_md5sum, &location))
	exit (1);

      if (verbose_flag)
        g_printf("Installing %s with disk layout described in %s\n",
	         archive_file, disk_layout);

      if (!install_system (location, device, disk_layout, &error))
	{
	  if (error)
	    {
	      g_fprintf (stderr, "ERROR: %s\n", error->message);
	      g_clear_error (&error);
	    }
	  else
	    g_fprintf (stderr, "ERROR: installation of the archive failed!\n");

	  exit (1);
	}

      g_printf("System successfully installed...\n");
    }

  else if (strcmp (argv[1], UPDATE) == 0)
    {
      gchar *location = NULL;

      read_config(UPDATE, &archive_file, &archive_md5sum, &disk_layout);

      if (!download_and_verify (archive_file, archive_md5sum, &location))
	exit (1);

      if (verbose_flag)
        g_printf("Updating using %s\n", archive_file);

      if (!update_system (location, &error))
	{
	  if (error)
	    {
	      g_fprintf (stderr, "ERROR: %s\n", error->message);
	      g_clear_error (&error);
	    }
	  else
	    g_fprintf (stderr, "ERROR: system update failed!\n");
	  exit (1);
	}
      g_printf("System successfully updated...\n");
    }
  else
    {
      g_fprintf (stderr, "ERROR: no argument given!\n");
      exit (1);
    }

  return 0;
}

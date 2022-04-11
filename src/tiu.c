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
#define CREATE "create"
#define UPDATE "update"
#define USR_BTRFS "USR_BTRFS"
#define USR_AB "USR_AB"

static gchar *input_tar = NULL;
static GOptionEntry entries_create[] = {
  {"tar", 't', 0, G_OPTION_ARG_FILENAME, &input_tar, "tar archive", "FILENAME"},
  {0}
};
static GOptionGroup *create_group;

static gchar *squashfs_file = NULL;
static gchar *chunk_store = NULL;
static gchar *target_dir = NULL;
static gboolean usr_btrfs_flag = false;
static gboolean usr_AB_flag = false;
static gboolean force_installation = false;
static GOptionEntry entries_extract[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {"store", 's', 0, G_OPTION_ARG_FILENAME, &chunk_store,
   "URL pointing to castr repository (used for tiuidx archives).", "DIRECTORY"},
  {"output", 'o', 0, G_OPTION_ARG_FILENAME, &target_dir, "target directory", "DIRECTORY"},
  {0}
};
static GOptionGroup *extract_group;

static gchar *device = NULL;
static GOptionEntry entries_install[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {"store", 's', 0, G_OPTION_ARG_FILENAME, &chunk_store,
   "URL pointing to castr repository (used for tiuidx archives).", "DIRECTORY"},
  {"device", 'd', 0, G_OPTION_ARG_FILENAME, &device, "installation device", "DEVICE"},
  {"usr-btrfs", '\0', 0, G_OPTION_ARG_NONE, &usr_btrfs_flag, "using BTRFS disk layout (default)", NULL},
  {"usr-AB", '\0', 0, G_OPTION_ARG_NONE, &usr_AB_flag, "using disk layout with 2 partitions (A,B)", NULL},
  {"usr-ab", '\0', 0, G_OPTION_ARG_NONE, &usr_AB_flag, "using disk layout with 2 partitions (A,B)", NULL},
  {"force", '\0', 0, G_OPTION_ARG_NONE, &force_installation, "no user confirmation for disk erasing", NULL},
  {0}
};
static GOptionGroup *install_group;

static GOptionEntry entries_update[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {"store", 's', 0, G_OPTION_ARG_FILENAME, &chunk_store,
   "URL pointing to castr repository (used for tiuidx archives).", "DIRECTORY"},
  {0}
};
static GOptionGroup *update_group;

static void
init_group_options (void)
{
  extract_group = g_option_group_new(EXTRACT, "Extract options:",
				    "Show help options for extract", NULL, NULL);
  g_option_group_add_entries(extract_group, entries_extract);

  create_group = g_option_group_new(CREATE, "Create options:",
				    "Show help options for create", NULL, NULL);
  g_option_group_add_entries(create_group, entries_create);

  install_group = g_option_group_new(INSTALL, "Installation options:",
				    "Show help options for install", NULL, NULL);
  g_option_group_add_entries(install_group, entries_install);

  update_group = g_option_group_new(UPDATE, "Update options:",
				    "Show help options for update", NULL, NULL);
  g_option_group_add_entries(update_group, entries_update);
}

static void
read_manifest(const TIUBundle *bundle, gchar **format)
{
  gchar *filename = g_strjoin("/", bundle->mount_point, MANIFEST_TIU, NULL);
  econf_file *key_file = NULL;
  econf_err econf_err;

  if ((econf_err = econf_readFile (&key_file, filename, "=", "#")))
    {
      fprintf (stderr, "ERROR: Cannot read %s: %s\n", filename, econf_errString(econf_err));
      return;
    }

  gchar *value = NULL;
  g_printf ("Product name: ");
  if ((econf_err = econf_getStringValue (key_file, "global", "FULL_NAME",
					 &value)))
    {
      g_printf ("--not defined--");
    }
  else
    {
      g_printf ("%s", value);
      free(value);
    }

  if ((econf_err = econf_getStringValue (key_file, "global", "NAME",
					 &value)))
    {
      g_printf (" (--not defined--)");
    }
  else
    {
      g_printf (" (%s)", value);
      free(value);
    }

  g_printf (", version: ");
  if ((econf_err = econf_getStringValue (key_file, "global", "VERSION",
					 &value)))
    {
      g_printf ("--not defined--");
    }
  else
    {
      g_printf ("%s", value);
      free(value);
    }

  g_printf (", architecture: ");
  if ((econf_err = econf_getStringValue (key_file, "global", "ARCH",
					 &value)))
    {
      g_printf ("--not defined--\n");
    }
  else
    {
      g_printf ("%s\n", value);
      free(value);
    }

  g_printf (", format: ");
  if ((econf_err = econf_getStringValue (key_file, "update", "FORMAT",
					 &value)))
    {
      g_printf ("--not defined--\n");
    }
  else
    {
      g_printf ("%s\n", value);
      *format = value;
    }

  econf_free (key_file);
}

static void
read_config(const gchar *kind, const gchar *disk_layout_format,
	    gchar **archive, gchar **archive_md5sum, gchar **disk_layout,
	    gchar **config_store)
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

   if (strcmp(kind, INSTALL) == 0 && disk_layout_format != NULL)
   {
      ecerror = econf_getStringValue(key_file, kind, disk_layout_format, disk_layout);
      if (ecerror != ECONF_SUCCESS)
	 ecerror = econf_getStringValue(key_file, "global", disk_layout_format, disk_layout);

      if (ecerror != ECONF_SUCCESS || archive == NULL)
      {
         fprintf (stderr, "ERROR: Cannot read -%s- entry from tiu.conf for installation: %s\n",
		  disk_layout_format, econf_errString(ecerror));
	          exit (1);
      }
   } else {
      *disk_layout = NULL;
   }

   ecerror = econf_getStringValue(key_file, kind, "archive_md5sum", archive_md5sum);
   if (ecerror != ECONF_SUCCESS)
      econf_getStringValue(key_file, "global", "archive_md5sum", archive_md5sum);

   ecerror = econf_getStringValue(key_file, kind, "store", config_store);
   if (ecerror != ECONF_SUCCESS)
      econf_getStringValue(key_file, "global", "store", config_store);

   econf_free (key_file);
}

static gboolean
download_check_mount(const gchar *tiuname, const gchar *archive_md5sum, TIUBundle **bundle,
		     const gchar *config_store, gchar **store)
{
  GError *error = NULL;
  gchar *archive_format = NULL;

  if (!download_tiu_archive (tiuname, archive_md5sum, bundle, &error))
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

  if (!check_tiu_archive (*bundle, &error))
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

  if (!mount_tiu_archive (*bundle, &error))
    {
      if (error)
	{
	  g_fprintf (stderr, "ERROR: %s\n", error->message);
	  g_clear_error (&error);
	}
      else
	g_fprintf (stderr, "ERROR: mounting the archive failed!\n");
      return FALSE;
    }

  read_manifest(*bundle, &archive_format);

  if (strcmp(archive_format, CAIDX) == 0) {
     if (*store == NULL) {
	/* taking store from the tiu.conf file */
	*store = g_strdup(config_store);
     }
     if (*store == NULL) {
	g_fprintf (stderr, "ERROR: Store URL not given for tiuidx format.\n");
	return FALSE;
     } else {
        g_printf ("Taking store: %s\n", *store);
     }
  }

  return TRUE;
}

int
main(int argc, char **argv)
{
  gboolean help = FALSE, version = FALSE;
  gchar *archive_md5sum = NULL;
  gchar *disk_layout = NULL;
  gchar *config_store = NULL;
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
				    "  create\tCreate a tiu update file\n"
				    "  extract\tExtract a tiu archive\n"
				    "  install\tInstall a new system\n"
				    "  update\tUpdate current system\n"
				    );
  g_option_context_add_group (context, create_group);
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

  if (strcmp (argv[1], CREATE) == 0)
    {
      if (input_tar == NULL)
	{
	  fprintf (stderr, "ERROR: no tar archive as input specified!\n");
	  return 1;
	}

      if (!create_image(input_tar, &error))
	{
	  if (error)
	    {
	      g_fprintf (stderr, "ERROR: %s\n", error->message);
	      g_clear_error (&error);
	    }
	  else
	    g_fprintf (stderr, "ERROR: creating tiu archives failed!\n");
	  exit (1);
	}
    }
  else if (strcmp (argv[1], EXTRACT) == 0)
    {
      TIUBundle *bundle = NULL;

      read_config(EXTRACT, NULL, &squashfs_file, &archive_md5sum, &disk_layout,
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

      if (!download_check_mount (squashfs_file, archive_md5sum, &bundle,
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
      free_bundle(bundle);
    }
  else if (strcmp (argv[1], INSTALL) == 0)
    {
      TIUBundle *bundle = NULL;
      gchar *disk_layout_format = USR_BTRFS;
      TIUPartSchema schema = TIU_USR_BTRFS;

      if (!force_installation) {
	gchar answer='n';
	int count=0;
	do {
	   g_printf("All data of device %s will be deleted. Continue (y/n)?\n", device);
	   count = scanf(" %c", &answer);
	   if (answer == 'n')
	      exit (0);
	} while (count!=1 || answer != 'y');
      }

      if (!usr_btrfs_flag && !usr_AB_flag)
	{
          g_printf ("INFO: Options --usr-btrfs or --usr-AB are not defined. Taking default --usr-btrfs.\n");
	}

      if (usr_btrfs_flag && usr_AB_flag)
	{
          g_fprintf (stderr, "ERROR: Option --usr-btrfs and --usr-AB must not defined at the same time.\n");
	  exit (1);
	}

      if (usr_AB_flag)
	{
	  disk_layout_format = USR_AB;
	  schema = TIU_USR_AB;
	}

      read_config(INSTALL, disk_layout_format, &squashfs_file, &archive_md5sum,
		  &disk_layout, &config_store);

      if (device == NULL)
	{
	  g_fprintf (stderr, "ERROR: no device for installation specified!\n");
	  exit (1);
	}

      if (!download_check_mount (squashfs_file, archive_md5sum, &bundle,
				 config_store, &chunk_store))
	exit (1);

      if (verbose_flag)
        g_printf("Installing %s with disk layout described in %s\n",
	         bundle->path, disk_layout);

      if (!install_system (bundle, device, schema, disk_layout, chunk_store, &error))
	{
	  if (error)
	    {
	      g_fprintf (stderr, "ERROR: %s\n", error->message);
	      g_clear_error (&error);
	    }
	  else
	    g_fprintf (stderr, "ERROR: installation of the archive failed!\n");

	  free_bundle(bundle);
	  exit (1);
	}

      free_bundle(bundle);

      g_printf("System successfully installed...\n");
    }
  else if (strcmp (argv[1], UPDATE) == 0)
    {
      TIUBundle *bundle = NULL;

      read_config(UPDATE, NULL, &squashfs_file, &archive_md5sum, &disk_layout,
		  &config_store);

      if (!download_check_mount (squashfs_file, archive_md5sum, &bundle,
				 config_store, &chunk_store))
	exit (1);

      if (!update_system (bundle, chunk_store, &error))
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
      free_bundle(bundle);
    }
  else
    {
      g_fprintf (stderr, "ERROR: no argument given!\n");
      exit (1);
    }

  return 0;
}

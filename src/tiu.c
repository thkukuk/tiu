#include <stdio.h>
#include <unistd.h>
#include <glib/gprintf.h>
#include "tiu.h"

static gchar *input_tar = NULL;
static GOptionEntry entries_create[] = {
  {"tar", 't', 0, G_OPTION_ARG_FILENAME, &input_tar, "tar archive", "FILENAME"},
  {0}
};
static GOptionGroup *create_group;

/* XXX should be NULL, read default from a config file */
static gchar *squashfs_file = "https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/repo/tiu/openSUSE-MicroOS.tiutar";
static gchar *target_dir = NULL;
static GOptionEntry entries_extract[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {"output", 'o', 0, G_OPTION_ARG_FILENAME, &target_dir, "target directory", "DIRECTORY"},
  {0}
};
static GOptionGroup *extract_group;

static gchar *device = NULL;
static GOptionEntry entries_install[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {"device", 'd', 0, G_OPTION_ARG_FILENAME, &device, "installation device", "DEVICE"},
  {0}
};
static GOptionGroup *install_group;

static GOptionEntry entries_update[] = {
  {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &squashfs_file, "tiu archive", "FILENAME"},
  {0}
};
static GOptionGroup *update_group;

static void
init_group_options (void)
{
  extract_group = g_option_group_new("extract", "Extract options:",
				    "Show help options for extract", NULL, NULL);
  g_option_group_add_entries(extract_group, entries_extract);

  create_group = g_option_group_new("create", "Create options:",
				    "Show help options for create", NULL, NULL);
  g_option_group_add_entries(create_group, entries_create);

  install_group = g_option_group_new("install", "Installation options:",
				    "Show help options for install", NULL, NULL);
  g_option_group_add_entries(install_group, entries_install);

  update_group = g_option_group_new("update", "Update options:",
				    "Show help options for update", NULL, NULL);
  g_option_group_add_entries(update_group, entries_update);
}

int
main(int argc, char **argv)
{
  gboolean help = FALSE, version = FALSE;
  g_autoptr(GOptionContext) context = NULL;
  GError *error = NULL;
  GOptionEntry options[] = {
    {"debug", '\0', 0, G_OPTION_ARG_NONE, &debug_flag, "enable debug output", NULL},
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

  if (version)
    g_print("TIU - Transactional Image Update Version %s\n", "0.1");

  /* Nothing more to do */
  if (argc == 1)
    exit (0);

  if (getuid () != 0)
    {
      fprintf (stderr, "ERROR: You must be root to run TIU\n");
      exit (1);
    }

  if (strcmp (argv[1], "create") == 0)
    {
      if (input_tar == NULL)
	{
	  fprintf (stderr, "ERROR: no tar archive as input specified!\n");
	  return 1;
	}

      if (!create_images(input_tar, &error))
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
  else if (strcmp (argv[1], "extract") == 0)
    {
      if (squashfs_file == NULL)
	{
	  fprintf (stderr, "ERROR: no tiu archive as input specified!\n");
	  exit (1);
	}

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


      if (!extract_tiu_image(squashfs_file, target_dir, &error))
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
  else if (strcmp (argv[1], "install") == 0)
    {
      if (squashfs_file == NULL)
	{
	  fprintf (stderr, "ERROR: no tiu archive as input specified!\n");
	  exit (1);
	}

      if (device == NULL)
	{
	  fprintf (stderr, "ERROR: no device for installation specified!\n");
	  exit (1);
	}

      if (!install_system (squashfs_file, device, &error))
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
    }
  else if (strcmp (argv[1], "update") == 0)
    {
      if (squashfs_file == NULL)
	{
	  fprintf (stderr, "ERROR: no tiu archive as input specified!\n");
	  exit (1);
	}

      if (!update_system (squashfs_file, &error))
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
    }
  else
    {
      g_fprintf (stderr, "ERROR: no argument given!\n");
      exit (1);
    }

  return 0;
}

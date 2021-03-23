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

static void
init_group_options (void)
{
  create_group = g_option_group_new("create", "Create options:",
				    "create", NULL, NULL);
  g_option_group_add_entries(create_group, entries_create);
}

int
main(int argc, char **argv)
{
  gboolean help = FALSE, debug = FALSE, version = FALSE;
  g_autoptr(GOptionContext) context = NULL;
  GError *error = NULL;
  GOptionEntry options[] = {
    {"debug", 'd', 0, G_OPTION_ARG_NONE, &debug, "enable debug output", NULL},
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
				    );
  g_option_context_add_group (context, create_group);

  if (!g_option_context_parse(context, &argc, &argv, &error))
    {
      g_printerr("%s\n", error->message);
      g_error_free(error);
      exit (1);
    }

  if (debug)
    {
      /* Not yet implemented */
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

      if (!create_images(input_tar, NULL))
	return 1;
    }

  return 0;
}

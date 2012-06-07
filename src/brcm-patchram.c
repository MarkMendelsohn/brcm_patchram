
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static int
usb_init(void *context)
{
	return 0;
}

static int
usb_patch(void *context)
{
	return 0;
}

static int
usb_close(void *context)
{
	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		exit(0); /* wrong number of arguments. */

	fprintf(stderr, "argc = %d\n",argc);
	struct operations {
		const char *name;
		int (*brcm_init)(void *);
		int (*brcm_patch)(void *);
		int (*brcm_close)(void *);
	} patchram[] = {
		{ "usb", usb_init, usb_patch, usb_close },
		{ NULL, NULL, NULL, NULL }
	};

	/* determine which mode we're using. */
	int mode = 0;
	for (; patchram[mode].name != NULL && strcmp(patchram[mode].name, argv[1]); mode++) 
		;

	if (patchram[mode].name == NULL) {
		fprintf(stderr, "No such mode %s\n", argv[1]);
		exit(0);
	}

	argc--;
	argv++;

	 struct option opts[] = { 
			{ "bdaddr",		1,	0,	'b'	},
			{ "debug",		0,	0,	'd'	},
			{ "device",   1,	0,	'D'	},
			{ "help",			0,	0,	'h'	},
			{ "patchram",	1,	0,	'p'	},
			{ NULL,				0,	0,	0		}
	};

	struct context {
		bool	debug;
		char	*bdaddr;
		char	*device;
		char	*patchram;
	} c = { false, NULL, NULL, NULL };

	int arg, longindex;
	fprintf(stderr,"argc = %d\n", argc);

	while ((arg = getopt_long_only(argc, argv, "p:D:b:dh", opts, &longindex)) != -1) {
		switch (arg) {
			case 'h':	/* --help */
				fprintf(stderr,"HELP!\n");
				break;

			case 'd':	/* --debug */
				c.debug = true;
				break;

			case 'D':	/* --device */
				c.device = optarg;
				break;

			case 'p':	/* --patchram */
				c.patchram = optarg;
				break;

			case 'b':	/* --baddr */
				c.bdaddr = optarg;
				break;

			default:
				break;
		}
	}

	fprintf(stderr, "debug = %s, device = %s, patchram = %s, bdaddr = %s\n",
		c.debug ? "true" : "false",
		c.device,
		c.patchram,
		c.bdaddr);
}

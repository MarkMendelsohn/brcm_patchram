/*
 *  brcm_patchram_plus_usb.c
 *  Copyright (C) 2009-2011 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 *                                                                           
 *  Name: brcm_patchram_plus_usb.c
 *
 *  Description:
 *
 *   This program downloads a patchram files
 *   in the HCD format to Broadcom Bluetooth
 *   based silicon and combo chips and and
 *   other utility functions.
 *
 *   It can be invoked from the command line
 *   in the form:
 *
 *     --debug - Print a debug log
 *     --patchram <patchram_file>
 *			--bd_addr <bd_address>
 *		  bluez_device_name
 *
 *  Example:
 *
 *    brcm_patchram_plus --debug \
 *         --patchram BCM2045B2_002.002.011.0348.0349.hcd hci0
 *
 *    It will return 0 for success and a number
 *    greater than 0 for any errors.
 *
 *    For Android, this program invoked using a
 *    "system(2)" call from the beginning of
 *    the bt_enable function inside the file
 *    mydroid/system/bluetooth/bluedroid/bluetooth.c.
 *  
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "brcm_usb.h"

#ifdef ANDROID
# include <cutils/properties.h>
#	define LOG_TAG "brcm_patchram_plus_usb"
# include <cutils/log.h>
# undef printf
# define printf LOGD
# undef fprintf
# define fprintf(x, ...) { if(x==stderr) LOGE(__VA_ARGS__); else fprintf(x, __VA_ARGS__); }
#endif /* ANDROID */

extern int debug;

static int
test_patchram_filename(const char *hcdpath)
{
	/*
		FIXME: Is this check necessesary?  I guess
		all broadcom supplied HCD files will be named
		this way but it is possible that someone might
		rename them.

		A better sanity check would be to have a
		magic sequence at the begining of the file
		that identifies it as an HCD.
  */
	char *p;
	if (((p = strrchr(hcdpath, '.')) == '\0') || (strcasecmp(".hcd", p) != 0))
		return -1;
 
	return(0);
}

/*
 * Simply rip apart a string that looks like a sextet of
 * 8-bit hex digits that represents and pack it into the
 * global array hci_write_bd_addr[] (FIXME: figure out how
 * to get rid of this global.).
 *
 * There are two common formats:
 *
 *  XX:XX:XX:XX:XX:XX -- This is what `hcitool dev' outputs.
 *  XXXXXXXXXXXX      -- This format is what bcrm_patchram_plus_usb usually takes.
 *
 * This function tries to accept either.
 *
 * This accepts a string as input and returns -1 if the
 * address isn't parsable and 0 if it succeeds.
 */

static int
parse_cmd_line(int argc, char *argv[], char ** restrict patchram_path, char ** restrict hci_device, char ** restrict bdaddr)
{
	/* Iniitalize our 'out variables' -- the parameters we'll be
	   passing back to main. */
	*patchram_path = *hci_device = *bdaddr = NULL;

	static struct option long_options[] = {
		{"patchram",	1,	NULL, 'p'},
		{"bd_addr",		1, 	NULL, 'b'},
		{"debug",			0,	NULL, 'd'},
		{"help",			0,	NULL, 'h'},
		{0,						0,	0,		0}
	};

	/* Handle command line arguments. */
	int arg, option_index = 0;
	while ((arg = getopt_long(argc, argv, "p:b:dh", long_options, &option_index)) != -1) {
		switch (arg) {
	    case 'p':
				/* --patchram or -p */
				*patchram_path = optarg;
				break;

			case 'b':
				/* --bd_addr or -b */
				*bdaddr = optarg;
				break;

			case 'd':
				/* --debug or -d */
				debug = 1;
				break;

	    case '?':
	    case 'h':
			default:
				printf("Usage %s:\n", argv[0]);
				printf("\t--debug - Print a debug log\n");
				printf("\t--patchram patchram_file\n");
				printf("\t--bd_addr bd_address\n");
				printf("\t[bluez_device_name]\n");
				break;
		}
	}

	
	if (optind < argc)
		*hci_device = argv[optind];

	return 0;
}


#ifdef ANDROID
void
read_default_bdaddr()
{
	int sz;
	int fd;

	char path[PROPERTY_VALUE_MAX];

	char bdaddr[18];
	int len = 17;
	memset(bdaddr, 0, (len + 1) * sizeof(char));

	property_get("ro.bt.bdaddr_path", path, "");
	if (path[0] == 0)
		return;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s (%d)", path, strerror(errno),
				errno);
		return;
	}

	sz = read(fd, bdaddr, len);
	if (sz < 0) {
		fprintf(stderr, "read(%s) failed: %s (%d)", path, strerror(errno),
				errno);
		close(fd);
		return;
	} else if (sz != len) {
		fprintf(stderr, "read(%s) unexpected size %d", path, sz);
		close(fd);
		return;
	}

	if (debug)
		printf("Read default bdaddr of %s\n", bdaddr);

	parse_bdaddr(bdaddr);
}
#endif

int
main (int argc, char *argv[])
{
#ifdef ANDROID
	read_default_bdaddr();
#endif

	char *patchram_path = NULL, *hci_device = NULL, *bdaddr = NULL;

	parse_cmd_line(argc, argv, &patchram_path, &hci_device, &bdaddr);

	if (patchram_path == NULL)
		brcm_error(0, "You must supply a patch RAM file with --patchram.\n");

	/* Check if the patchram file's extenion is .hcd */
	if (test_patchram_filename(patchram_path) == -1)
		brcm_error(4, "error: %s does not appear to be an .hcd file.\n", optarg);

	int hcdfd = open(patchram_path, O_RDONLY);
	if (hcdfd == -1)
		brcm_error(5, "error: Could not open hcd file %s (%s)\n", patchram_path, strerror(errno));


/*
	fprintf(stderr, "Using hci%d\n", dev_id);

	if (dev_id == -1)
		brcm_error(1, "device %s could not be found\n", hci_device);
*/

	int hcifd = brcm_patchram_usb_init(hci_device);

	if (hcdfd > 0)
		brcm_patchram_usb(hcifd, hcdfd);

	if (bdaddr != NULL)
		brcm_set_bdaddr_usb(hcifd, bdaddr);

	exit(0);
}

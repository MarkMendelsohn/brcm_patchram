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

#include "brcm_btlib.h"

#ifdef ANDROID
# include <cutils/properties.h>
#	define LOG_TAG "brcm_patchram_plus_usb"
# include <cutils/log.h>
# undef printf
# define printf LOGD
# undef fprintf
# define fprintf(x, ...) { if(x==stderr) LOGE(__VA_ARGS__); else fprintf(x, __VA_ARGS__); }
#endif /* ANDROID */

#define hexdump(buf, len, s, ...) ({ if (debug) { fprintf(stderr, s,##__VA_ARGS__); dump(buf, len); } })

int debug = 0;

uint8_t buffer[1024];

uint8_t hci_write_bd_addr[] = {
	0x01, 0x01, 0xfc, 0x06, 
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00
};

static void
dump(const uint8_t *out, ssize_t len)
{
	for (ssize_t i = 0; i < len; i++)
		fprintf(stderr, "%02x%s", out[i], ((i+1) % 16) ? " " : "\n");
	fprintf(stderr, "\n");
}

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
parse_bdaddr(const char *bdaddr_string)
{
	int bd_addr[6];
	if (sscanf(bdaddr_string, "%02x:%02x:%02x:%02x:%02x:%02x", &bd_addr[0], &bd_addr[1], &bd_addr[2], &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6)
		if (sscanf(bdaddr_string, "%02x%02x%02x%02x%02x%02x", &bd_addr[0], &bd_addr[1], &bd_addr[2], &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6)
			return -1;

	for (unsigned i = 0; i < 6; i++)
		hci_write_bd_addr[4 + i] = bd_addr[i];

	return 0;
}

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

	if (*bdaddr != NULL)
		if (parse_bdaddr(*bdaddr) == -1)
			brcm_error(0, "error: Improper bdaddr format '%s'.\n", *bdaddr);
	
	if (optind < argc)
		*hci_device = argv[optind];

	return 0;
}

static void
init_hci(int hcifd)
{
	struct hci_filter flt;

	hci_filter_clear(&flt);
	hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
	hci_filter_all_events(&flt);

	setsockopt(hcifd, SOL_HCI, HCI_FILTER, &flt, sizeof (flt));
}

static ssize_t
read_event(int fd, uint8_t *buffer)
{
	ssize_t bytesin = read(fd, buffer, 260);
	hexdump(buffer, bytesin, "received %zd\n", bytesin);
	return bytesin;
}

static void
hci_send_data(int hcifd, uint8_t *buf, size_t nbytes)
{
	/* Attempt to write data to socket.  If we get an EAGAIN or EINTR,
	   try again.  Otherwise, exit.  */
	while (write(hcifd, buf, nbytes) < 0) 
		if (errno != EAGAIN && errno != EINTR)
			brcm_error(0, "write(): failed (%s).\n", strerror(errno));
}

static void
hci_send_command(int hcifd, const uint8_t *buf, ssize_t len)
{
	hci_command_hdr hc = {
		.opcode = buf[1] | (buf[2] << 8),
		.plen = len - 4
	};

	uint8_t type = HCI_COMMAND_PKT;
	struct iovec iv[3] = {
		[0] = { .iov_base = &type, .iov_len = 1 },
		[1] = { .iov_base = &hc, .iov_len = HCI_COMMAND_HDR_SIZE }
	};

	/* FIXME: Should this be if len - 4 > 0? */
	if (len - 4) {
		iv[2].iov_base = (void *)&buf[4];
		iv[2].iov_len = len - 4;
	}

	while (writev(hcifd, iv, (len - 4) ? 3 : 2) < 0)
		if (errno != EAGAIN && errno != EINTR)
			brcm_error(0, "writev() failed. (%s)\n", strerror(errno));
}

#define HCIT_TYPE_COMMAND 1
static void
hci_send_cmd_func(int hcifd, const uint8_t *buf, ssize_t len)
{
	hexdump(buf, len, "Writing\n");

	if (buf[0] == HCIT_TYPE_COMMAND)
		hci_send_command(hcifd, buf, len);
	else
		hci_send_data(hcifd, (uint8_t *)buf, len);
}

static void
proc_reset(int hcifd)
{
	for (unsigned try = 0; try < 5; try++) {
		const uint8_t hci_reset[] = { 0x01, 0x03, 0x0c, 0x00 };
		hci_send_cmd_func(hcifd, hci_reset, sizeof(hci_reset));
			
		/* Wait 4 seconds for descriptor to be readable. */ 
		struct pollfd pfd = { .fd = hcifd, .events = POLLIN };
		/* FIXME: We should probably catch EINTR here or use ppoll(). */
		int ready = poll(&pfd, 1, 4 * 1000);

		if (ready == 1) {
			read_event(hcifd, buffer);
			break;
		}
	}
}

static void
brcm_patchram_usb(int hcifd, int hcdfd)
{
	const uint8_t hci_download_minidriver[] = { 0x01, 0x2e, 0xfc, 0x00 };
	hci_send_cmd_func(hcifd, hci_download_minidriver, sizeof(hci_download_minidriver));

	read_event(hcifd, buffer);

	/* FIXME: Why sleep here?  Does the driver require a pause after
		sending the HCIDownloadMinidriver command?  */
	sleep(1);

	while (read(hcdfd, &buffer[1], 3)) {
		buffer[0] = 0x01;
		ssize_t bufsize = buffer[3],
						bytesin = read(hcdfd, &buffer[4], bufsize);

		hci_send_cmd_func(hcifd, buffer, bytesin + 4);
		read_event(hcifd, buffer);
	}

	proc_reset(hcifd);
}

static void
brcm_set_bdaddr_usb(int hcifd)
{
	hci_send_cmd_func(hcifd, hci_write_bd_addr, sizeof(hci_write_bd_addr));
	read_event(hcifd, buffer);
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

/* Callback for hci brcm_hci_for_each_dev() that prints the available
    bluetooth devices to stdout. */
static int
dev_list(int s __attribute__ ((unused)), int dev_id, void *arg)
{
	size_t *count = arg;
	(*count)--;
	fprintf(stderr,"hci%d%s\n", dev_id, *count == 0 ? "" : ", ");
	return 0;
}

/* Callback for hci brcm_hci_for_each_dev() that sets *arg to the first
   device found. */
static int
dev_get_first(int s __attribute__ ((unused)), int dev_id, void *arg)
{
	int *d = arg;
	*d = dev_id;
	return 1;
}

/* Callback for hci brcm_hci_for_each_dev() that sets *arg to the
   number of devices found. */
static int
dev_count(int s __attribute__ ((unused)), int dev_id __attribute__ ((unused)), void *arg)
{
	size_t *count = arg;
	(*count)++;
	return 0;
}

static int
get_hci_device(const char *hci_device)
{
	int dev_id = -1;

	if (hci_device == NULL) {
		/* The user didn't specify an hci device.  So --
			 we should first count the number of devices
			 we have.  If we find one, then open it.  If we
			 find more than one, we should ask the user
			 to be more specific.  If we don't find *any*,
			 then we print an error.  */
		size_t devices = 0;
		brcm_hci_for_each_dev(HCI_UP, dev_count, &devices);

		if (devices > 1) {
			/* Found more than we can handle. :( */
			fprintf(stderr, "error: Found more than one bluetooth device.  You must specify which one.\n  ");
			brcm_hci_for_each_dev(HCI_UP, dev_list, &devices);
		} else if (devices == 0) {
			/* Didn't find any. :( */
			fprintf(stderr, "error: Could not find any bluetooth devices.\n");
		} else if (devices == 1) {
			/* Found only one -- this will be the one we patch. */
			brcm_hci_for_each_dev(HCI_UP, dev_get_first, &dev_id);
		}
	} else {
		dev_id = hci_devid(hci_device);
	}

	return dev_id;
}

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

	int dev_id = get_hci_device(hci_device);

	fprintf(stderr, "Using hci%d\n", dev_id);

	if (dev_id == -1)
		brcm_error(1, "device %s could not be found\n", hci_device);

	int hcifd = hci_open_dev(dev_id);

	if (hcifd == -1)
		brcm_error(2, "device %s could not be found\n", argv[optind]);

	init_hci(hcifd);
	proc_reset(hcifd);

	if (hcdfd > 0)
		brcm_patchram_usb(hcifd, hcdfd);

	if (bdaddr != NULL)
		brcm_set_bdaddr_usb(hcifd);

	exit(0);
}

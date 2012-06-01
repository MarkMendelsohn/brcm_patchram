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

#include <stdio.h>
#include <getopt.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdlib.h>

#include <poll.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <string.h>
#include <signal.h>

#ifdef ANDROID
#include <cutils/properties.h>
#define LOG_TAG "brcm_patchram_plus_usb"
#include <cutils/log.h>
#undef printf
#define printf LOGD
#undef fprintf
#define fprintf(x, ...) \
  { if(x==stderr) LOGE(__VA_ARGS__); else fprintf(x, __VA_ARGS__); }
#endif //ANDROID

/* int sock = -1; */
int bdaddr_flag = 0;
int debug = 0;

unsigned char buffer[1024];
unsigned char hci_reset[] = { 0x01, 0x03, 0x0c, 0x00 };
unsigned char hci_download_minidriver[] = { 0x01, 0x2e, 0xfc, 0x00 };

unsigned char hci_write_bd_addr[] = {
	0x01, 0x01, 0xfc, 0x06, 
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00
};

#define HCIT_TYPE_COMMAND 1

int
test_patchram_filename(char *hcdpath)
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
	if (((p = strrchr(hcdpath, '.')) == '\0') || (strcasecmp(".hcd", p) != 0)) {
		fprintf(stderr, "error: %s does not appear to be an .hcd file.\n", optarg);
		exit(4);
	}

	return(0);
}

int
parse_bdaddr(char *optarg)
{
	int bd_addr[6];

	sscanf(optarg, "%02x%02x%02x%02x%02x%02x", 
		&bd_addr[0], &bd_addr[1], &bd_addr[2],
		&bd_addr[3], &bd_addr[4], &bd_addr[5]);

	for (int i = 0; i < 6; i++)
		hci_write_bd_addr[4 + i] = bd_addr[i];

	return(0);
}

int
parse_cmd_line(int argc, char *argv[], char **patchram_path, char **hci_device, char **baseaddr)
{

	/* Iniitalize our 'out variables' -- the parameters we'll be passing back to main. */
	*patchram_path = *hci_device = *baseaddr = NULL;

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
				/* --patchram or - p */
				*patchram_path = optarg;
				break;

			case 'b':
				/* --bd_addr or -b */
				*baseaddr = optarg;
				break;

			case 'd':
				/* --debug or -d */
				debug = 1;
				break;

	    case '?':
	    case 'h':
			default:
				printf("Usage %s:\n", argv[0]);
				printf("\t<-d> to print a debug log\n");
				printf("\t<--patchram patchram_file>\n");
				printf("\t<--bd_addr bd_address>\n");
				printf("\tbluez_device_name\n");
				break;
		}
	}

	if (*baseaddr != NULL)
		parse_bdaddr(*baseaddr);
	
	if (optind < argc)
		*hci_device = argv[optind];

	return 0;
}

void
init_hci(int hcifd)
{
	struct hci_filter flt;

	hci_filter_clear(&flt);
	hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
	hci_filter_all_events(&flt);

	setsockopt(hcifd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt));
}

void
dump(unsigned char *out, ssize_t len)
{
	for (int i = 0; i < len; i++) {
		if (i && !(i % 16))
			fprintf(stderr, "\n");

		fprintf(stderr, "%02x ", out[i]);
	}

	fprintf(stderr, "\n");
}

ssize_t
read_event(int fd, unsigned char *buffer)
{
	ssize_t bytesin = read(fd, buffer, 260);

	if (debug) {
		fprintf(stderr, "received %zd\n", bytesin);
		dump(buffer, bytesin);
	}

	return bytesin;
}

void
hci_send_data(int hcifd, unsigned char *buf, int len)
{
	/* Attempt to write data to socket.  If we get an EAGAIN or EINTR,
	   try again.  Otherwise, exit.
	 */
	while (write(hcifd, buf, len) < 0) 
		if (errno != EAGAIN && errno != EINTR) {
			fprintf(stderr, "write(): failed (%s).\n", strerror(errno));
			exit(0);
		}
}

void
hci_send_command(int hcifd, unsigned char *buf, int len)
{
	hci_command_hdr hc;
	struct iovec iv[3];
	int ivn;
	uint8_t type = HCI_COMMAND_PKT;

	hc.opcode = buf[1] | (buf[2] << 8);
	hc.plen = len - 4;

	iv[0].iov_base = &type;
	iv[0].iov_len = 1;
	iv[1].iov_base = &hc;
	iv[1].iov_len = HCI_COMMAND_HDR_SIZE;
	ivn = 2;

	/* FIXME: Should this be if len - 4 > 0? */
	if (len - 4) {
		iv[2].iov_base = &buf[4];
		iv[2].iov_len = len - 4;
		ivn = 3;
	}

	while (writev(hcifd, iv, ivn) < 0)
		if (errno != EAGAIN && errno != EINTR) {
			perror("writev()");
			exit(0);
		}
}

void
hci_send_cmd_func(int hcifd, unsigned char *buf, int len)
{
	if (debug) {
		fprintf(stderr, "writing\n");
		dump(buf, len);
	}

	if (buf[0] == HCIT_TYPE_COMMAND)
		hci_send_command(hcifd, buf, len);
	else
		hci_send_data(hcifd, buf, len);
}

void
proc_reset(int hcifd)
{
	for (unsigned try = 0; try < 5; try++) {
		hci_send_cmd_func(hcifd, hci_reset, sizeof(hci_reset));
			
		/* Wait 4 seconds for descriptor to be readable. */ 
		struct pollfd pfd = { .fd = hcifd, .events = POLLIN };
		int ready = poll(&pfd, 1, 4 * 1000);

		if (ready == 1) {
			read_event(hcifd, buffer);
			break;
		}
	}
}

void
proc_patchram(int hcifd, int hcdfd)
{
	hci_send_cmd_func(hcifd, hci_download_minidriver, sizeof(hci_download_minidriver));

	read_event(hcifd, buffer);

	sleep(1);

	while (read(hcdfd, &buffer[1], 3)) {
		buffer[0] = 0x01;
		int len = buffer[3];

		read(hcdfd, &buffer[4], len);

		hci_send_cmd_func(hcifd, buffer, len + 4);

		read_event(hcifd, buffer);
	}

	proc_reset(hcifd);
}

void
proc_bdaddr(int hcifd)
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

int
main (int argc, char **argv)
{
#ifdef ANDROID
	read_default_bdaddr();
#endif

	char *patchram_path = NULL, *hci_device = NULL, *baseaddr = NULL;

	parse_cmd_line(argc, argv, &patchram_path, &hci_device, &baseaddr);

	if (patchram_path == NULL) {
		fprintf(stderr, "You must supply a patch RAM file with --patchram.\n");
		exit(0);
	}

	/* Check if the patchram file's extenion is .hcd */
	test_patchram_filename(patchram_path);

	int hcdfd = open(patchram_path, O_RDONLY);
	if (hcdfd == -1) {
		fprintf(stderr, "error: Could not open hcd file %s (%s)\n", patchram_path, strerror(errno));
		exit(5);
	}

	int dev_id = hci_devid(hci_device);
	if (dev_id == -1) {
		fprintf(stderr, "device %s could not be found\n", hci_device);
		exit(1);
	}

	int hcifd = hci_open_dev(dev_id);
	if (hcifd == -1) {
		fprintf(stderr, "device %s could not be found\n", argv[optind]);
		exit(2);
	}

	init_hci(hcifd);

	proc_reset(hcifd);

	if (hcdfd > 0)
		proc_patchram(hcifd, hcdfd);

	if (baseaddr != NULL)
		proc_bdaddr(hcifd);

	exit(0);
}

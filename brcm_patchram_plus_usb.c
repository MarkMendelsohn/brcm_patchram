/*
 *
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
 *
 */


/*****************************************************************************
**                                                                           
**  Name:          brcm_patchram_plus_usb.c
**
**  Description:   This program downloads a patchram files in the HCD format
**                 to Broadcom Bluetooth based silicon and combo chips and
**				   and other utility functions.
**
**                 It can be invoked from the command line in the form
**						<-d> to print a debug log
**						<--patchram patchram_file>
**						<--bd_addr bd_address>
**						bluez_device_name
**
**                 For example:
**
**                 brcm_patchram_plus -d --patchram  \
**						BCM2045B2_002.002.011.0348.0349.hcd hci0
**
**                 It will return 0 for success and a number greater than 0
**                 for any errors.
**
**                 For Android, this program invoked using a 
**                 "system(2)" call from the beginning of the bt_enable
**                 function inside the file 
**                 mydroid/system/bluetooth/bluedroid/bluetooth.c.
**
**  
******************************************************************************/
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
int hcdfile_fd = -1;
int bdaddr_flag = 0;
int enable_lpm = 0;
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
parse_patchram(char *optarg)
{
	char *p;

	if (!(p = strrchr(optarg, '.'))) {
		fprintf(stderr, "file %s not an HCD file\n", optarg);
		exit(3);
	}

	p++;

	if (strcasecmp("hcd", p) != 0) {
		fprintf(stderr, "file %s not an HCD file\n", optarg);
		exit(4);
	}

	if ((hcdfile_fd = open(optarg, O_RDONLY)) == -1) {
		fprintf(stderr, "file %s could not be opened, error %d\n", optarg, errno);
		exit(5);
	}

	return(0);
}

int
parse_bdaddr(char *optarg)
{
	int bd_addr[6];
	int i;

	sscanf(optarg, "%02x%02x%02x%02x%02x%02x", 
		&bd_addr[0], &bd_addr[1], &bd_addr[2],
		&bd_addr[3], &bd_addr[4], &bd_addr[5]);

	for (i = 0; i < 6; i++) {
		hci_write_bd_addr[4 + i] = bd_addr[i];
	}

	bdaddr_flag = 1;	

	return(0);
}

int
parse_cmd_line(int argc, char *argv[], int *hcifd)
{
	int (*parse_param[])() = { parse_patchram, parse_bdaddr };

	static struct option long_options[] = {
		{"patchram", 1, 0, 0},
		{"bd_addr", 1, 0, 0},
		{0, 0, 0, 0}
	};

	/* Handle command line arguments. */
	int c;
	int option_index = 0;
	while ((c = getopt_long_only(argc, argv, "d", long_options, &option_index)) != -1) {
		switch (c) {
	    case 0:
	    	printf("option %s", long_options[option_index].name);

	    	if (optarg)
					printf(" with arg %s", optarg);

				printf("\n");

				parse_param[option_index](optarg);
				break;

			case 'd':
				debug = 1;
				break;

	    case '?':
			default:
				printf("Usage %s:\n", argv[0]);
				printf("\t<-d> to print a debug log\n");
				printf("\t<--patchram patchram_file>\n");
				printf("\t<--bd_addr bd_address>\n");
				printf("\tbluez_device_name\n");
				break;
		}
	}
	
	if (optind < argc) {
		printf ("%s ", argv[optind]);

		int dev_id= hci_devid(argv[optind]);

		if (dev_id == -1) {
			fprintf(stderr, "device %s could not be found\n", argv[optind]);
			exit(1);
		}

		printf("devid %d\n", dev_id);

		if ((*hcifd = hci_open_dev(dev_id)) == -1) {
			fprintf(stderr, "device %s could not be found\n", argv[optind]);
			exit(2);
		}

		printf("sock %d\n", *hcifd);
	}

	printf("\n");

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
dump(unsigned char *out, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (i && !(i % 16)) {
			fprintf(stderr, "\n");
		}

		fprintf(stderr, "%02x ", out[i]);
	}

	fprintf(stderr, "\n");
}

void
read_event(int fd, unsigned char *buffer)
{
	int i = 0;
	int count;

	count = read(fd, &buffer[i], 260);

	if (debug) {
		count += i;

		fprintf(stderr, "received %d\n", count);
		dump(buffer, count);
	}
}

void
hci_send_cmd_func(int hcifd, unsigned char *buf, int len)
{
	uint8_t type;
	hci_command_hdr hc;
	struct iovec iv[3];
	int ivn;

	if (debug) {
		fprintf(stderr, "writing\n");
		dump(buf, len);
	}

	if (buf[0] == HCIT_TYPE_COMMAND) {
		type = HCI_COMMAND_PKT;
	} else {
		while (write(hcifd, buf, len) < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}

			return;
		}

		return;
	}

	hc.opcode = buf[1] | (buf[2] << 8);
	hc.plen = len - 4;

	iv[0].iov_base = &type;
	iv[0].iov_len = 1;
	iv[1].iov_base = &hc;
	iv[1].iov_len = HCI_COMMAND_HDR_SIZE;
	ivn = 2;

	if (len - 4) {
		iv[2].iov_base = &buf[4];
		iv[2].iov_len = len - 4;
		ivn = 3;
	}

	while (writev(hcifd, iv, ivn) < 0) {
		if (errno == EAGAIN || errno == EINTR) {
			continue;
		}

		return;
	}
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
proc_patchram(int hcifd)
{
	int len;

	hci_send_cmd_func(hcifd, hci_download_minidriver, sizeof(hci_download_minidriver));

	read_event(hcifd, buffer);

	sleep(1);

	while (read(hcdfile_fd, &buffer[1], 3)) {
		buffer[0] = 0x01;

		len = buffer[3];

		read(hcdfile_fd, &buffer[4], len);

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

	if (debug) {
		printf("Read default bdaddr of %s\n", bdaddr);
	}

	parse_bdaddr(bdaddr);
}
#endif

int
main (int argc, char **argv)
{
#ifdef ANDROID
	read_default_bdaddr();
#endif

	int hcifd;

	parse_cmd_line(argc, argv, &hcifd);

	if (hcifd < 0)
		exit(1);

	init_hci(hcifd);

	proc_reset(hcifd);

	if (hcdfile_fd > 0) {
		proc_patchram(hcifd);
	}

	if (bdaddr_flag) {
		proc_bdaddr(hcifd);
	}

	return(0);
}

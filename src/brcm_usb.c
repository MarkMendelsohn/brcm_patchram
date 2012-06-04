
#include <unistd.h>
#include <stdlib.h>		/* for malloc() and free() */
#include <sys/ioctl.h>	/* for ioctl() */
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

int debug = 0;


/* utility routines we want to expose. */

/* FIXME: Will probably move dump to another module. */
void
dump(const uint8_t *out, ssize_t len)
{
	for (ssize_t i = 0; i < len; i++)
		fprintf(stderr, "%02x%s", out[i], ((i+1) % 16) ? " " : "\n");
	fprintf(stderr, "\n");
}

int brcm_hci_for_each_dev(int flag, int (*func)(int s, int dev_id, void *context), void *context)
{
    int dev_id = -1;

		if (!func)
			return -1;

		int s;
    if ((s = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0)
        return -1;

    struct hci_dev_req *dr;
    struct hci_dev_list_req *dl = NULL;
		if ((dl = malloc(HCI_MAX_DEV * sizeof (*dr) + sizeof(*dl))) == NULL)
				goto done;

    dl->dev_num = HCI_MAX_DEV;
    dr = dl->dev_req;

    if (ioctl(s, HCIGETDEVLIST, (void *)dl))
        goto done;

    for (int i = 0; i < dl->dev_num; i++, dr++) {
        if (hci_test_bit(flag, &dr->dev_opt))
            if (!func || func(s, dr->dev_id, context)) {
                dev_id = dr->dev_id;
                break;
            }
    }

done:
    close(s);
    free(dl);
    return dev_id;
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

static void
hci_send_data(int hcifd, uint8_t *buf, size_t nbytes)
{
	/* Attempt to write data to socket.  If we get an EAGAIN or EINTR,
	   try again.  Otherwise, exit.  */
	while (write(hcifd, buf, nbytes) < 0) 
		if (errno != EAGAIN && errno != EINTR)
			brcm_error(0, "write(): failed (%s).\n", strerror(errno));
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
	uint8_t buffer[1024];
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



/* patch related routines we want to expose. */

int
brcm_set_bdaddr_usb(int hcifd, const char *bdaddr_string)
{
	uint8_t hci_write_bd_addr[] = {
		0x01, 0x01, 0xfc, 0x06, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
	};

	int bd_addr[6];
	if (sscanf(bdaddr_string, "%02x:%02x:%02x:%02x:%02x:%02x", &bd_addr[0], &bd_addr[1], &bd_addr[2], &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6)
		if (sscanf(bdaddr_string, "%02x%02x%02x%02x%02x%02x", &bd_addr[0], &bd_addr[1], &bd_addr[2], &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6)
			return -1;

	for (unsigned i = 0; i < 6; i++)
		hci_write_bd_addr[4 + i] = bd_addr[i];

	uint8_t buffer[1024];
	hci_send_cmd_func(hcifd, hci_write_bd_addr, sizeof(hci_write_bd_addr));
	read_event(hcifd, buffer);

	return 0;
}


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
brcm_patchram_usb_init(const char *hci_device)
{
	int dev_id = get_hci_device(hci_device);
	int hcifd = hci_open_dev(dev_id);
	if (hcifd == -1)
		return -1;
		/* brcm_error(2, "device %s could not be found\n", argv[optind]); */

	init_hci(hcifd);

	return hcifd;
}

void
brcm_patchram_usb(int hcifd, int hcdfd /* readable descriptor for patchram file. */)
{
	uint8_t buffer[1024];
	const uint8_t hci_download_minidriver[] = { 0x01, 0x2e, 0xfc, 0x00 };

	proc_reset(hcifd);

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

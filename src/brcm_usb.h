
#ifndef _HAVE_BRCM_USB_H
#define _HAVE_BRCM_USB_H

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

/* FIXME: Maybe we can remove the file, line, and function. */
#define brcm_error(rc,s,...) ({ fprintf(stderr, "%s,%s():%d: " s, __FILE__, __func__, __LINE__, ##__VA_ARGS__); exit(rc); })
#define hexdump(buf, len, s, ...) ({ if (debug) { fprintf(stderr, s,##__VA_ARGS__); dump(buf, len); } })

/* brcm_usb.c */
void dump(const uint8_t *out, ssize_t len);
int brcm_hci_for_each_dev(int flag, int (*func)(int s, int dev_id, void *context), void *context);
int brcm_set_bdaddr_usb(int hcifd, const char *bdaddr_string);
int brcm_patchram_usb_init(const char *hci_device);
void brcm_patchram_usb(int hcifd, int hcdfd);

#endif


#include <unistd.h>
#include <stdlib.h>		/* for malloc() and free() */
#include <sys/ioctl.h>	/* for ioctl() */

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

/* HCI functions that do not require open device */  
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

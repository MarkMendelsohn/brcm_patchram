/* brcm_btlib.c */
int brcm_hci_for_each_dev(int flag, int (*func)(int s, int dev_id, void *context), void *context);

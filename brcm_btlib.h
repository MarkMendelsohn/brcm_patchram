

/* FIXME: Maybe we can remove the file, line, and function. */
#define brcm_error(rc,s,...) ({ fprintf(stderr, "%s,%s():%d: " s, __FILE__, __func__, __LINE__, ##__VA_ARGS__); exit(rc); })

/* brcm_btlib.c */
int brcm_hci_for_each_dev(int flag, int (*func)(int s, int dev_id, void *context), void *context);

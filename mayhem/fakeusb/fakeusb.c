/* See usb.h. Every programmed block is checksummed so test.sh can assert what the tool sent. */
#include <stdio.h>
#include <stdlib.h>
#include "usb.h"
struct usb_dev_handle { int unused; };
static struct usb_device dev = { NULL, { 0x16C0, 0x0478 } };
static struct usb_bus bus = { NULL, &dev };
static struct usb_dev_handle handle;
static unsigned long blocks, sum;
static void report(void)
{
	if (getenv("FAKEUSB_REPORT"))
	  fprintf(stderr, "fakeusb: blocks=%lu sum=%lu\n", blocks, sum);
}
void usb_init(void) { atexit(report); }
int usb_find_busses(void) { return 1; }
int usb_find_devices(void) { return 1; }
struct usb_bus* usb_get_busses(void) { return &bus; }
usb_dev_handle* usb_open(struct usb_device* d) { (void)d; return &handle; }
int usb_close(usb_dev_handle* h) { (void)h; return 0; }
int usb_claim_interface(usb_dev_handle* h, int i) { (void)h; (void)i; return 0; }
int usb_release_interface(usb_dev_handle* h, int i) { (void)h; (void)i; return 0; }
int usb_control_msg(usb_dev_handle* h, int rt, int r, int v, int idx, char* bytes, int size, int t)
{
	(void)h; (void)rt; (void)r; (void)v; (void)idx; (void)t;
	blocks++;
	for (int i = 0; i < size; i++)
	  sum += (unsigned char)bytes[i] * (unsigned long)(i + 1);
	return size;
}
char* usb_strerror(void) { return (char*)"fakeusb error"; }

/* Minimal stand-in for libusb-0.1's <usb.h>, for fuzzing hid_bootloader_cli: it presents one
 * HalfKay HID bootloader (0x16C0:0x0478) whose control transfers all succeed, so the tool runs its
 * whole flow (Intel HEX parse, then the block programming loop) without hardware. */
#ifndef MAYHEM_FAKE_USB_H
#define MAYHEM_FAKE_USB_H
#include <stdint.h>
struct usb_device_descriptor { uint16_t idVendor, idProduct; };
struct usb_device { struct usb_device* next; struct usb_device_descriptor descriptor; };
struct usb_bus { struct usb_bus* next; struct usb_device* devices; };
typedef struct usb_dev_handle usb_dev_handle;
void usb_init(void);
int usb_find_busses(void);
int usb_find_devices(void);
struct usb_bus* usb_get_busses(void);
usb_dev_handle* usb_open(struct usb_device* dev);
int usb_close(usb_dev_handle* h);
int usb_claim_interface(usb_dev_handle* h, int iface);
int usb_release_interface(usb_dev_handle* h, int iface);
char* usb_strerror(void);
int usb_control_msg(usb_dev_handle* h, int requesttype, int request, int value, int index,
                    char* bytes, int size, int timeout);
#endif

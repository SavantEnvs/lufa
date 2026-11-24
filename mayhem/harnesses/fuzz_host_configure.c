/* Fuzz LUFA's USB host class drivers' configuration-descriptor walkers: each class driver's
 * *_Host_ConfigurePipes (LUFA/Drivers/USB/Class/Host/*.c) searches the configuration descriptor an
 * attached (untrusted) device returned, with USB_GetNextDescriptorComp and the class's DComp_*
 * comparators (LUFA/Drivers/USB/Core/ConfigDescriptors.c), then configures pipes from the
 * endpoint descriptors it finds.
 *
 * Input: the configuration descriptor (wTotalLength bytes), as USB_Host_GetDeviceConfigDescriptor
 * hands it over. Every class driver parses its own copy in a heap buffer of CONFIG_BUFFER_SIZE
 * bytes, zero-padded past the descriptor: that is the caller's contract in every LUFA host demo
 * (Demos/Host/ClassDriver/*: uint8_t ConfigDescriptorData[512]). USB_GetNextDescriptorComp hands
 * the comparator the descriptor AT the end of the reported length once the walk runs out, so
 * the comparators always read a few bytes past wTotalLength; with an exact-size buffer every
 * input would crash there. Reads past the 512-byte buffer are still caught. The pipe registers
 * are modelled by mayhem/shim.
 *
 * Known upstream hang (not guarded, it is a finding): USB_GetNextDescriptor advances by the
 * descriptor's own bLength, so a descriptor with bLength 0 inside wTotalLength makes
 * USB_GetNextDescriptorComp spin forever: a malicious device hangs the host firmware.
 *
 * RNDISClassHost.c is not included: it declares structs with variable-length array members (a GCC
 * extension clang rejects: "fields must have a constant size"), so it cannot be built with the
 * fleet's clang toolchain. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <LUFA/Drivers/USB/USB.h>
#include "shim_usb.h"

volatile uint8_t     USB_HostState;
USB_Request_Header_t USB_ControlRequest;

/* The host enumeration state machine (USBTask.c) is not part of this target: control transfers
 * the class drivers issue run through the real HostStandardReq.c, whose wait loops call this. */
void USB_USBTask(void) { }
/* Host_AVR8.c's busy-wait on SOF interrupts; time is not modelled, the wait just elapses. */
uint8_t USB_Host_WaitMS(uint8_t MS) { (void)MS; return HOST_WAITERROR_Successful; }

bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const CurrentItem) { (void)CurrentItem; return true; }

#define CONFIG_BUFFER_SIZE 512

#define IN_PIPE(n)  { .Address = (PIPE_DIR_IN  | (n)), .Banks = 1 }
#define OUT_PIPE(n) { .Address = (PIPE_DIR_OUT | (n)), .Banks = 1 }

#define RUN(type, fn, ...)                                                    \
	do {                                                                      \
		type info = { .Config = { __VA_ARGS__ } };                            \
		uint8_t* copy = calloc(1, CONFIG_BUFFER_SIZE);                        \
		memcpy(copy, data, size);                                             \
		fn(&info, (uint16_t)size, copy);                                      \
		free(copy);                                                           \
	} while (0)

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size > CONFIG_BUFFER_SIZE) /* USB_Host_GetDeviceConfigDescriptor rejects a larger one */
	  return -1;
	shim_reset(NULL, 0);
	shim_host_mode = 1;
	USB_HostState  = HOST_STATE_Configured;

	RUN(USB_ClassInfo_HID_Host_t,   HID_Host_ConfigurePipes,   .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2), .HIDInterfaceProtocol = HID_CSCP_NonBootProtocol);
	RUN(USB_ClassInfo_CDC_Host_t,   CDC_Host_ConfigurePipes,   .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2), .NotificationPipe = IN_PIPE(3));
	RUN(USB_ClassInfo_MS_Host_t,    MS_Host_ConfigurePipes,    .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2));
	RUN(USB_ClassInfo_SI_Host_t,    SI_Host_ConfigurePipes,    .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2), .EventsPipe = IN_PIPE(3));
	RUN(USB_ClassInfo_PRNT_Host_t,  PRNT_Host_ConfigurePipes,  .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2));
	RUN(USB_ClassInfo_MIDI_Host_t,  MIDI_Host_ConfigurePipes,  .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2));
	RUN(USB_ClassInfo_Audio_Host_t, Audio_Host_ConfigurePipes, .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2));
	RUN(USB_ClassInfo_AOA_Host_t,   AOA_Host_ConfigurePipes,   .DataINPipe = IN_PIPE(1), .DataOUTPipe = OUT_PIPE(2));
	return 0;
}

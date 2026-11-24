/* Fuzz LUFA's RNDIS device class driver (LUFA/Drivers/USB/Class/Device/RNDISClassDevice.c) as a
 * USB host (untrusted) drives it: class control requests carrying encapsulated RNDIS messages
 * (INITIALIZE/QUERY/SET/RESET/KEEPALIVE with host-supplied OIDs, lengths and offsets), the
 * GetEncapsulatedResponse reply, and RNDIS data packets on the bulk OUT endpoint.
 *
 * The firmware runs natively on a modelled AVR8 USB controller (mayhem/shim): the endpoint data
 * register yields the fuzz input byte by byte. Input: a sequence of operations, each one opcode
 * byte then its payload:
 *   op%3 == 0: an 8-byte USB SETUP packet (USB_ControlRequest), then its data stage;
 *   op%3 == 1: RNDIS_Device_ReadPacket (bulk OUT: RNDIS packet header + frame);
 *   op%3 == 2: RNDIS_Device_USBTask (notification endpoint). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <LUFA/Drivers/USB/USB.h>
#include "shim_usb.h"

#define MESSAGE_BUFFER_LEN 192 /* as Demos/Device/ClassDriver/RNDISEthernet */

volatile uint8_t     USB_DeviceState;
USB_Request_Header_t USB_ControlRequest;

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	shim_reset(data, size);
	shim_ctrl_reqtype = &USB_ControlRequest.bmRequestType;
	USB_DeviceState = DEVICE_STATE_Configured;
	memset(&USB_ControlRequest, 0, sizeof(USB_ControlRequest));

	/* Exact-size heap buffer so ASan catches any write past MessageBufferLength. */
	uint8_t* msgbuf = calloc(1, MESSAGE_BUFFER_LEN);
	USB_ClassInfo_RNDIS_Device_t iface = {
		.Config = {
			.ControlInterfaceNumber   = 0,
			.DataINEndpoint           = { .Address = (ENDPOINT_DIR_IN  | 1), .Size = 64, .Banks = 1 },
			.DataOUTEndpoint          = { .Address = (ENDPOINT_DIR_OUT | 2), .Size = 64, .Banks = 1 },
			.NotificationEndpoint     = { .Address = (ENDPOINT_DIR_IN  | 3), .Size = 8,  .Banks = 1 },
			.AdapterVendorDescription = "LUFA RNDIS Demo Adapter",
			.AdapterMACAddress        = { { 0x02, 0x00, 0x02, 0x00, 0x02, 0x00 } },
			.MessageBuffer            = msgbuf,
			.MessageBufferLength      = MESSAGE_BUFFER_LEN,
		},
	};
	if (!RNDIS_Device_ConfigureEndpoints(&iface))
	  abort(); /* fixed, valid configuration: must succeed */

	while (shim_remaining() > 0)
	{
		uint8_t op = shim_take_byte();
		switch (op % 3)
		{
			case 0:
				if (!shim_take(&USB_ControlRequest, sizeof(USB_ControlRequest)))
				  goto done;
				USB_ControlRequest.wValue  = le16_to_cpu(USB_ControlRequest.wValue);
				USB_ControlRequest.wIndex  = le16_to_cpu(USB_ControlRequest.wIndex);
				USB_ControlRequest.wLength = le16_to_cpu(USB_ControlRequest.wLength);
				/* As USB_USBTask does before dispatching a control request. */
				Endpoint_SelectEndpoint(ENDPOINT_CONTROLEP);
				shim_set_setup_pending(1);
				RNDIS_Device_ProcessControlRequest(&iface);
				shim_set_setup_pending(0);
				break;
			case 1:
			{
				uint8_t  frame[ETHERNET_FRAME_SIZE_MAX];
				uint16_t len = 0;
				if (RNDIS_Device_IsPacketReceived(&iface))
				  RNDIS_Device_ReadPacket(&iface, frame, sizeof(frame), &len);
				break;
			}
			case 2:
				RNDIS_Device_USBTask(&iface);
				break;
		}
	}
done:
	free(msgbuf);
	return 0;
}

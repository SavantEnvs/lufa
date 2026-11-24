/* Fuzz LUFA's USB Mass Storage device stack as an (untrusted) USB host drives it: the Bulk-Only
 * Transport class driver (LUFA/Drivers/USB/Class/Device/MassStorageClassDevice.c: CBW reception
 * and validation, CSW return, the class control requests) and the SCSI command decoder the LUFA
 * mass-storage demo runs on every CBW (Demos/Device/ClassDriver/MassStorage/Lib/SCSI.c:
 * INQUIRY, REQUEST SENSE, READ CAPACITY, SEND DIAGNOSTIC, READ/WRITE (10), MODE SENSE,
 * TEST UNIT READY, PREVENT/ALLOW, VERIFY with host-supplied lengths, LBAs and block counts).
 *
 * The firmware runs natively on a modelled AVR8 USB controller (mayhem/shim): the bulk OUT
 * endpoint delivers the fuzz input in 64-byte packets. Input: a sequence of operations, each one
 * opcode byte then its payload:
 *   op%2 == 0: an 8-byte USB SETUP packet for MS_Device_ProcessControlRequest (reset / GetMaxLUN);
 *   op%2 == 1: MS_Device_USBTask: the CBW (31 bytes) in the current bulk OUT packet, followed by
 *              the command's OUT data (WRITE (10) blocks) in further packets. Packets are 64-byte
 *              windows of the input (mayhem/shim), so the opcode byte and its CBW share one window.
 *
 * No state survives an input: the demo's SCSI.c is compiled into this translation unit so that its
 * static SenseData (the last failed command's sense key/ASC/ASCQ, returned by REQUEST SENSE) is
 * restored to its power-on value before every input, as are the class driver's interface state and
 * the control request.
 *
 * Harness-side stand-ins:
 * - The demo's DataflashManager (SPI Dataflash on the board) is replaced by a medium that streams
 *   whole 512-byte blocks to/from the endpoints exactly as DataflashManager_Read/WriteBlocks do,
 *   but keeps no contents: the SPI Dataflash's busy-wait on its status byte has no host model.
 * - USB_USBTask, which the stream and CSW wait loops call while the host is not ready, models the
 *   host's recovery from a stalled bulk endpoint (CLEAR_FEATURE(ENDPOINT_HALT), which the real
 *   controller applies by clearing STALLRQ), so MS_Device_ReturnCommandStatus completes. The demo
 *   builds with INTERRUPT_CONTROL_ENDPOINT, where an ISR does that; this target is built without it
 *   so that the wait loops call USB_USBTask. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* The demo's SCSI command decoder, unmodified, in this translation unit (see above). It comes
 * first: it must be the first to include its header (SCSI.h declares its statics only then). */
#include "Lib/SCSI.c"
#include "MassStorage.h"
#include "shim_usb.h"

static SCSI_Request_Sense_Response_t SenseData_PowerOn;

volatile uint8_t     USB_DeviceState;
USB_Request_Header_t USB_ControlRequest;

static USB_ClassInfo_MS_Device_t Disk_MS_Interface;

void USB_USBTask(void)
{
	UECONX &= ~(1 << STALLRQ); /* the host cleared the halt */
}

/* As Demos/Device/ClassDriver/MassStorage/MassStorage.c. */
bool CALLBACK_MS_Device_SCSICommandReceived(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo)
{
	return SCSI_DecodeSCSICommand(MSInterfaceInfo);
}

/* The medium (see above). Same transfer shape as DataflashManager.c: TotalBlocks blocks of
 * VIRTUAL_MEMORY_BLOCK_SIZE bytes, abandoned on a mass storage reset. */
static uint8_t block[VIRTUAL_MEMORY_BLOCK_SIZE];
void DataflashManager_WriteBlocks(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo, const uint32_t BlockAddress, uint16_t TotalBlocks)
{
	(void)BlockAddress;
	while (TotalBlocks--)
	{
		if (Endpoint_Read_Stream_LE(block, sizeof(block), NULL) != ENDPOINT_RWSTREAM_NoError || MSInterfaceInfo->State.IsMassStoreReset)
		  return;
	}
	if (!(Endpoint_IsReadWriteAllowed()))
	  Endpoint_ClearOUT();
}
void DataflashManager_ReadBlocks(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo, const uint32_t BlockAddress, uint16_t TotalBlocks)
{
	memset(block, (uint8_t)BlockAddress, sizeof(block));
	while (TotalBlocks--)
	{
		if (Endpoint_Write_Stream_LE(block, sizeof(block), NULL) != ENDPOINT_RWSTREAM_NoError || MSInterfaceInfo->State.IsMassStoreReset)
		  return;
	}
	if (!(Endpoint_IsReadWriteAllowed()))
	  Endpoint_ClearIN();
}
bool DataflashManager_CheckDataflashOperation(void) { return true; }
void DataflashManager_ResetDataflashProtections(void) { }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	static bool snapshotted;
	if (!snapshotted)
	{
		SenseData_PowerOn = SenseData;
		snapshotted = true;
	}
	SenseData = SenseData_PowerOn;

	shim_reset(data, size);
	shim_ctrl_reqtype = &USB_ControlRequest.bmRequestType;
	USB_DeviceState = DEVICE_STATE_Configured;
	memset(&USB_ControlRequest, 0, sizeof(USB_ControlRequest));

	/* As MassStorage.c's Disk_MS_Interface. */
	memset(&Disk_MS_Interface, 0, sizeof(Disk_MS_Interface));
	Disk_MS_Interface.Config.InterfaceNumber         = INTERFACE_ID_MassStorage;
	Disk_MS_Interface.Config.DataINEndpoint.Address  = MASS_STORAGE_IN_EPADDR;
	Disk_MS_Interface.Config.DataINEndpoint.Size     = MASS_STORAGE_IO_EPSIZE;
	Disk_MS_Interface.Config.DataINEndpoint.Banks    = 1;
	Disk_MS_Interface.Config.DataOUTEndpoint.Address = MASS_STORAGE_OUT_EPADDR;
	Disk_MS_Interface.Config.DataOUTEndpoint.Size    = MASS_STORAGE_IO_EPSIZE;
	Disk_MS_Interface.Config.DataOUTEndpoint.Banks   = 1;
	Disk_MS_Interface.Config.TotalLUNs               = TOTAL_LUNS;
	if (!MS_Device_ConfigureEndpoints(&Disk_MS_Interface))
	  abort(); /* fixed, valid configuration: must succeed */

	while (shim_remaining() > 0)
	{
		uint8_t op = shim_take_byte();
		if ((op % 2) == 0)
		{
			if (!shim_take(&USB_ControlRequest, sizeof(USB_ControlRequest)))
			  break;
			USB_ControlRequest.wValue  = le16_to_cpu(USB_ControlRequest.wValue);
			USB_ControlRequest.wIndex  = le16_to_cpu(USB_ControlRequest.wIndex);
			USB_ControlRequest.wLength = le16_to_cpu(USB_ControlRequest.wLength);
			/* As USB_USBTask does before dispatching a control request. */
			Endpoint_SelectEndpoint(ENDPOINT_CONTROLEP);
			shim_set_setup_pending(1);
			MS_Device_ProcessControlRequest(&Disk_MS_Interface);
			shim_set_setup_pending(0);
		}
		else
		{
			MS_Device_USBTask(&Disk_MS_Interface);
		}
	}
	return 0;
}

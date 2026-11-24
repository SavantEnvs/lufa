/* Fuzz LUFA's USB device-mode control request handling as an (untrusted) USB host drives it:
 * the standard request handler USB_Device_ProcessControlRequest (LUFA/Drivers/USB/Core/
 * DeviceStandardReq.c: GET/SET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, GET_STATUS, features)
 * and, through EVENT_USB_Device_ControlRequest, the class control handlers of the HID, CDC, Mass
 * Storage, Audio and Printer device drivers (LUFA/Drivers/USB/Class/Device/*.c), as a composite
 * device like the LUFA demos would register them.
 *
 * The firmware runs natively on a modelled AVR8 USB controller (mayhem/shim): the control
 * endpoint's data register yields the fuzz input byte by byte. Input: a sequence of control
 * transfers, each the 8-byte SETUP packet USB_Device_ProcessControlRequest reads from the
 * endpoint, followed by its OUT data stage. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <LUFA/Drivers/USB/USB.h>
#include "shim_usb.h"

volatile uint8_t     USB_DeviceState;
USB_Request_Header_t USB_ControlRequest;
volatile uint8_t     USB_Options = USB_DEVICE_OPT_FULLSPEED | USB_OPT_REG_ENABLED | USB_OPT_AUTO_PLL; /* normally USBController_AVR8.c */

static uint8_t PrevHIDReport[8];
static USB_ClassInfo_HID_Device_t   HID;
static USB_ClassInfo_CDC_Device_t   CDC;
static USB_ClassInfo_MS_Device_t    MS;
static USB_ClassInfo_Audio_Device_t Audio;
static USB_ClassInfo_PRNT_Device_t  PRNT;

static void init_interfaces(void)
{
	memset(&HID, 0, sizeof(HID));     memset(&CDC, 0, sizeof(CDC));   memset(&MS, 0, sizeof(MS));
	memset(&Audio, 0, sizeof(Audio)); memset(&PRNT, 0, sizeof(PRNT));
	HID.Config.InterfaceNumber           = 0;
	HID.Config.ReportINEndpoint.Address  = ENDPOINT_DIR_IN | 1;
	HID.Config.ReportINEndpoint.Size     = 8;
	HID.Config.PrevReportINBuffer        = PrevHIDReport;
	HID.Config.PrevReportINBufferSize    = sizeof(PrevHIDReport);
	CDC.Config.ControlInterfaceNumber    = 1;
	CDC.Config.DataINEndpoint.Address    = ENDPOINT_DIR_IN | 2;
	CDC.Config.DataINEndpoint.Size       = 16;
	CDC.Config.DataOUTEndpoint.Address   = ENDPOINT_DIR_OUT | 3;
	CDC.Config.DataOUTEndpoint.Size      = 16;
	CDC.Config.NotificationEndpoint.Address = ENDPOINT_DIR_IN | 4;
	CDC.Config.NotificationEndpoint.Size = 8;
	MS.Config.InterfaceNumber            = 3;
	MS.Config.DataINEndpoint.Address     = ENDPOINT_DIR_IN | 5;
	MS.Config.DataINEndpoint.Size        = 64;
	MS.Config.DataOUTEndpoint.Address    = ENDPOINT_DIR_OUT | 6;
	MS.Config.DataOUTEndpoint.Size       = 64;
	MS.Config.TotalLUNs                  = 1;
	Audio.Config.ControlInterfaceNumber  = 4;
	Audio.Config.StreamingInterfaceNumber = 5;
	Audio.Config.DataOUTEndpoint.Address = ENDPOINT_DIR_OUT | 7;
	Audio.Config.DataOUTEndpoint.Size    = 64;
	PRNT.Config.InterfaceNumber          = 6;
	PRNT.Config.DataINEndpoint.Address   = ENDPOINT_DIR_IN | 2;
	PRNT.Config.DataINEndpoint.Size      = 16;
	PRNT.Config.DataOUTEndpoint.Address  = ENDPOINT_DIR_OUT | 3;
	PRNT.Config.DataOUTEndpoint.Size     = 16;
	PRNT.Config.IEEE1284String           = "MFG:LUFA;MDL:Fuzz Printer;CMD:PCL";
}

/* Descriptors, as a demo's Descriptors.c provides them. */
static const USB_Descriptor_Device_t DeviceDescriptor = {
	.Header = { .Size = sizeof(USB_Descriptor_Device_t), .Type = DTYPE_Device },
	.USBSpecification = VERSION_BCD(1,1,0), .Class = USB_CSCP_IADDeviceClass,
	.SubClass = USB_CSCP_IADDeviceSubclass, .Protocol = USB_CSCP_IADDeviceProtocol,
	.Endpoint0Size = FIXED_CONTROL_ENDPOINT_SIZE, .VendorID = 0x03EB, .ProductID = 0x2040,
	.ReleaseNumber = VERSION_BCD(0,0,1), .ManufacturerStrIndex = 1, .ProductStrIndex = 2,
	.SerialNumStrIndex = NO_DESCRIPTOR, .NumberOfConfigurations = FIXED_NUM_CONFIGURATIONS,
};
static const USB_Descriptor_Configuration_Header_t ConfigDescriptor = {
	.Header = { .Size = sizeof(USB_Descriptor_Configuration_Header_t), .Type = DTYPE_Configuration },
	.TotalConfigurationSize = sizeof(USB_Descriptor_Configuration_Header_t), .TotalInterfaces = 7,
	.ConfigurationNumber = 1, .ConfigurationStrIndex = NO_DESCRIPTOR,
	.ConfigAttributes = USB_CONFIG_ATTR_RESERVED | USB_CONFIG_ATTR_SELFPOWERED, .MaxPowerConsumption = USB_CONFIG_POWER_MA(100),
};
static const USB_Descriptor_String_t LanguageString = USB_STRING_DESCRIPTOR_ARRAY(LANGUAGE_ID_ENG);
static const USB_Descriptor_String_t ManufacturerString = USB_STRING_DESCRIPTOR(L"Dean Camera");
static const USB_Descriptor_String_t ProductString = USB_STRING_DESCRIPTOR(L"LUFA Fuzz Device");

uint16_t CALLBACK_USB_GetDescriptor(const uint16_t wValue, const uint16_t wIndex, const void** const DescriptorAddress)
{
	(void)wIndex;
	switch (wValue >> 8)
	{
		case DTYPE_Device:        *DescriptorAddress = &DeviceDescriptor; return sizeof(DeviceDescriptor);
		case DTYPE_Configuration: *DescriptorAddress = &ConfigDescriptor; return sizeof(ConfigDescriptor);
		case DTYPE_String:
			switch (wValue & 0xFF)
			{
				case 0: *DescriptorAddress = &LanguageString;     return LanguageString.Header.Size;
				case 1: *DescriptorAddress = &ManufacturerString; return ManufacturerString.Header.Size;
				case 2: *DescriptorAddress = &ProductString;      return ProductString.Header.Size;
			}
			break;
	}
	*DescriptorAddress = NULL;
	return NO_DESCRIPTOR;
}

void EVENT_USB_Device_ControlRequest(void)
{
	HID_Device_ProcessControlRequest(&HID);
	CDC_Device_ProcessControlRequest(&CDC);
	MS_Device_ProcessControlRequest(&MS);
	Audio_Device_ProcessControlRequest(&Audio);
	PRNT_Device_ProcessControlRequest(&PRNT);
}

/* Application callbacks the class drivers require, as the demos implement them. */
bool CALLBACK_HID_Device_CreateHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo, uint8_t* const ReportID,
                                         const uint8_t ReportType, void* ReportData, uint16_t* const ReportSize)
{
	(void)HIDInterfaceInfo; (void)ReportID; (void)ReportType;
	memset(ReportData, 0x5A, 8);
	*ReportSize = 8;
	return false;
}
void CALLBACK_HID_Device_ProcessHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo, const uint8_t ReportID,
                                          const uint8_t ReportType, const void* ReportData, const uint16_t ReportSize)
{
	(void)HIDInterfaceInfo; (void)ReportID; (void)ReportType;
	volatile uint8_t sink = 0;
	for (uint16_t i = 0; i < ReportSize; i++) sink ^= ((const uint8_t*)ReportData)[i];
}
bool CALLBACK_MS_Device_SCSICommandReceived(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo) { (void)MSInterfaceInfo; return false; }
/* As Demos/Device/ClassDriver/AudioOutput/AudioOutput.c: the sampling frequency control of the
 * streaming endpoint (a NULL DataLength is the driver probing whether the property exists).
 * Unlike the demo, both branches check that the driver's buffer (*DataLength bytes, sized by the
 * host's wLength in AudioClassDevice.c) holds the 3-byte value: this callback is harness code, and
 * an overrun here would be reported against mayhem/, not against the class driver under test. */
static uint32_t CurrentAudioSampleFrequency;
bool CALLBACK_Audio_Device_GetSetEndpointProperty(USB_ClassInfo_Audio_Device_t* const AudioInterfaceInfo, const uint8_t EndpointProperty,
                                                  const uint8_t EndpointAddress, const uint8_t EndpointControl,
                                                  uint16_t* const DataLength, uint8_t* Data)
{
	if (EndpointAddress != AudioInterfaceInfo->Config.DataOUTEndpoint.Address || EndpointControl != AUDIO_EPCONTROL_SamplingFreq)
	  return false;
	switch (EndpointProperty)
	{
		case AUDIO_REQ_SetCurrent:
			if (DataLength != NULL && *DataLength >= 3)
			  CurrentAudioSampleFrequency = (((uint32_t)Data[2] << 16) | ((uint32_t)Data[1] << 8) | (uint32_t)Data[0]);
			return true;
		case AUDIO_REQ_GetCurrent:
			if (DataLength != NULL && *DataLength >= 3)
			{
				*DataLength = 3;
				Data[2] = (CurrentAudioSampleFrequency >> 16);
				Data[1] = (CurrentAudioSampleFrequency >> 8);
				Data[0] = (CurrentAudioSampleFrequency &  0xFF);
			}
			return true;
	}
	return false;
}
bool CALLBACK_Audio_Device_GetSetInterfaceProperty(USB_ClassInfo_Audio_Device_t* const AudioInterfaceInfo, const uint8_t Property,
                                                   const uint8_t EntityAddress, const uint16_t Parameter,
                                                   uint16_t* const DataLength, uint8_t* Data)
{
	(void)AudioInterfaceInfo; (void)Property; (void)EntityAddress; (void)Parameter; (void)DataLength; (void)Data;
	return false;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	shim_reset(data, size);
	shim_ctrl_reqtype = &USB_ControlRequest.bmRequestType;
	USB_DeviceState = DEVICE_STATE_Configured;
	USB_Device_ConfigurationNumber = 0;
	memset(&USB_ControlRequest, 0, sizeof(USB_ControlRequest));
	init_interfaces();
	CurrentAudioSampleFrequency = 48000;

	while (shim_remaining() >= sizeof(USB_Request_Header_t))
	{
		size_t before = shim_remaining();
		/* As USB_USBTask does on a SETUP packet. */
		Endpoint_SelectEndpoint(ENDPOINT_CONTROLEP);
		shim_set_setup_pending(1);
		USB_Device_ProcessControlRequest();
		shim_set_setup_pending(0);
		USB_DeviceState = DEVICE_STATE_Configured;
		if (shim_remaining() == before)
		  break;
	}
	return 0;
}

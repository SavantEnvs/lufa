/* Fuzz LUFA's HID report descriptor parser (LUFA/Drivers/USB/Class/Common/HIDParser.c), the code a
 * USB host runs on a descriptor sent by an attached (untrusted) HID device, then the report item
 * accessors on a report from that device.
 *
 * Input: byte 0 = length L (mod 64) of the report that follows; bytes 1..L = report data;
 * the rest = the HID report descriptor. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __INCLUDE_FROM_USB_DRIVER
#define __INCLUDE_FROM_HID_DRIVER
#include <LUFA/Drivers/USB/Class/Common/HIDParser.h>

bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const CurrentItem)
{
	(void)CurrentItem;
	return true;
}

static HID_ReportInfo_t ParserData;

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 1)
	  return 0;
	size_t rlen = data[0] % 64;
	if (size < 1 + rlen)
	  return 0;
	const uint8_t* report = data + 1;
	const uint8_t* desc   = data + 1 + rlen;
	size_t dlen = size - 1 - rlen;
	if (dlen > 0xFFFF) /* USB_ProcessHIDReport takes a uint16_t descriptor length */
	  return 0;

	/* Exact-size heap copy so ASan sees any read past the descriptor. */
	uint8_t* d = malloc(dlen ? dlen : 1);
	memcpy(d, desc, dlen);
	memset(&ParserData, 0, sizeof(ParserData));
	uint8_t err = USB_ProcessHIDReport(d, (uint16_t)dlen, &ParserData);
	free(d);
	if (err != HID_PARSE_Successful)
	  return 0;

	for (uint8_t i = 0; i < ParserData.TotalReportItems && i < HID_MAX_REPORTITEMS; i++)
	{
		HID_ReportItem_t* item = &ParserData.ReportItems[i];
		/* The accessors' contract: ReportData holds the item's whole report (plus the report ID
		 * prefix when the item has one). Size the buffer to exactly that. */
		size_t need = (item->ReportID ? 1 : 0) + ((size_t)item->BitOffset + item->Attributes.BitSize + 7) / 8;
		uint8_t* r = calloc(need ? need : 1, 1);
		memcpy(r, report, rlen < need ? rlen : need);
		if (USB_GetHIDReportItemInfo(r, item))
		  USB_SetHIDReportItemInfo(r, item);
		free(r);
	}
	return 0;
}

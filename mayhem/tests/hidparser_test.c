/* Known-answer test for LUFA's HID report parser: parse a standard 3-button mouse report
 * descriptor, then decode a known report and print every item, for test.sh to diff against
 * the golden output. Built with normal flags (not the fuzz build). */
#include <stdio.h>
#include <stdint.h>
#define __INCLUDE_FROM_USB_DRIVER
#define __INCLUDE_FROM_HID_DRIVER
#include <LUFA/Drivers/USB/Class/Common/HIDParser.h>

bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const CurrentItem) { (void)CurrentItem; return true; }

static const uint8_t mouse[] = {
	0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
	0xC0, 0xC0,
};

int main(void)
{
	static HID_ReportInfo_t info;
	uint8_t err = USB_ProcessHIDReport(mouse, sizeof(mouse), &info);
	printf("err=%u items=%u usingids=%u\n", err, info.TotalReportItems, info.UsingReportIDs);
	uint8_t report[3] = { 0x05, 0xFB, 0x10 }; /* buttons 1+3, X=-5, Y=16 */
	for (uint8_t i = 0; i < info.TotalReportItems; i++)
	{
		HID_ReportItem_t* it = &info.ReportItems[i];
		bool ok = USB_GetHIDReportItemInfo(report, it);
		printf("item%u type=%u off=%u bits=%u page=0x%02x usage=0x%02x min=%ld max=%ld ok=%u value=%lu\n",
		       i, it->ItemType, it->BitOffset, it->Attributes.BitSize, it->Attributes.Usage.Page,
		       it->Attributes.Usage.Usage, (long)it->Attributes.Logical.Minimum,
		       (long)it->Attributes.Logical.Maximum, ok, (unsigned long)it->Value);
	}
	return 0;
}

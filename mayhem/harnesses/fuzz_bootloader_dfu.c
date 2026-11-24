/* Fuzz LUFA's DFU bootloader (Bootloaders/DFU/BootloaderDFU.c) as an (untrusted) USB host drives
 * it: EVENT_USB_Device_ControlRequest, the DFU class request handler (DNLOAD with its Atmel DFU
 * command headers and host-sized flash/EEPROM write data, UPLOAD reads, GETSTATUS, GETSTATE,
 * CLRSTATUS, ABORT) and the commands it dispatches (memory read/blank check, chip erase, start
 * address/jump, page select).
 *
 * The firmware runs natively on a modelled AT90USB1287 (mayhem/shim). The bootloader source is
 * compiled, unmodified, into this translation unit so that its statics (DFU state machine, address
 * range, security flag) are reset before every input. Input: a sequence of 8-byte USB SETUP
 * packets, each followed by its data stage on the control endpoint (mayhem/shim).
 *
 * Hardware stand-ins (the self-programming hardware has no host equivalent):
 * - Flash is a RAM image of the part's FLASHEND+1 bytes. The SPM primitives behind
 *   BootloaderAPI.c (page erase, temporary page buffer fill, page write) and the ELPM reads
 *   (pgm_read_*_far) act on it with the hardware's address decoding: the Z/RAMPZ address wraps at
 *   FLASHEND and the page buffer is indexed by the in-page offset.
 * - The bootloader leaves its endpoint wait loops only when USB_DeviceState becomes Unattached,
 *   which on the device happens when the host disconnects. Here the host disconnects when the
 *   input is exhausted.
 * - The bootloader jumps to the application from main(), which is never called. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <LUFA/Common/Common.h>
/* Application_Jump_Check is a naked .init3 routine on the AVR (it runs before main and jumps to
 * the application); here it is an ordinary function that is never called. */
#undef ATTR_INIT_SECTION
#define ATTR_INIT_SECTION(x) __attribute__((used))
#define INCLUDE_FROM_BOOTLOADER_C
#include "BootloaderDFU.h"
#include "shim_usb.h"

/* Config/LUFAConfig.h keeps USB_DeviceState in GPIOR0 (DEVICE_STATE_AS_GPIOR). */
#define HARNESS_DEVICE_STATE_REG GPIOR0
USB_Request_Header_t USB_ControlRequest;

/* --- the flash memory and its self-programming interface -------------------------------------- */
static uint8_t flash[(uint32_t)FLASHEND + 1];
static uint16_t page_buf[SPM_PAGESIZE / 2];
static uint8_t lock_bits;

#define FLASH_ADDR(a) ((uint32_t)(a) & (uint32_t)FLASHEND)
static inline uint8_t flash_read_byte(uint32_t a) { return flash[FLASH_ADDR(a)]; }
static inline uint16_t flash_read_word(uint32_t a)
{
	return (uint16_t)(flash_read_byte(a) | (flash_read_byte(a + 1) << 8));
}
#undef pgm_read_byte_far
#undef pgm_read_word_far
#undef pgm_read_word_near
#define pgm_read_byte_far(a)  flash_read_byte((uint32_t)(a))
#define pgm_read_word_far(a)  flash_read_word((uint32_t)(a))
#define pgm_read_word_near(a) flash_read_word((uint32_t)(uintptr_t)(a))

/* As BootloaderAPI.c, on the flash image. */
void BootloaderAPI_ErasePage(const uint32_t Address)
{
	memset(&flash[FLASH_ADDR(Address) & ~(uint32_t)(SPM_PAGESIZE - 1)], 0xFF, SPM_PAGESIZE);
}
void BootloaderAPI_WritePage(const uint32_t Address)
{
	uint8_t* page = &flash[FLASH_ADDR(Address) & ~(uint32_t)(SPM_PAGESIZE - 1)];
	for (unsigned i = 0; i < SPM_PAGESIZE / 2; i++)
	{
		/* Programming only clears bits; the page buffer is then reset to all ones. */
		page[2 * i]     &= (uint8_t)page_buf[i];
		page[2 * i + 1] &= (uint8_t)(page_buf[i] >> 8);
	}
	memset(page_buf, 0xFF, sizeof(page_buf));
}
void BootloaderAPI_FillWord(const uint32_t Address, const uint16_t Word)
{
	page_buf[(Address & (SPM_PAGESIZE - 1)) / 2] = Word;
}
uint8_t BootloaderAPI_ReadSignature(const uint16_t Address) { return boot_signature_byte_get(Address); }
uint8_t BootloaderAPI_ReadFuse(const uint16_t Address)      { return boot_lock_fuse_bits_get(Address); }
uint8_t BootloaderAPI_ReadLock(void)                        { return lock_bits; }
void    BootloaderAPI_WriteLock(const uint8_t LockBits)     { lock_bits &= (uint8_t)(LockBits | 0xC0); }

/* --- the USB device stack around the bootloader (main() is never called) ---------------------- */
void USB_Init(void) { }
void USB_USBTask(void) { }
void USB_Disable(void) { }

/* The host disconnects when it has nothing more to send (see above). */
static uint8_t harness_device_state(void)
{
	return shim_remaining() ? DEVICE_STATE_Configured : DEVICE_STATE_Unattached;
}

#define main bootloader_main
#undef USB_DeviceState
#define USB_DeviceState harness_device_state()
#include "BootloaderDFU.c"
#undef main

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	shim_reset(data, size);
	shim_ctrl_reqtype = &USB_ControlRequest.bmRequestType;
	HARNESS_DEVICE_STATE_REG = DEVICE_STATE_Configured;
	memset(&USB_ControlRequest, 0, sizeof(USB_ControlRequest));

	/* The bootloader's power-on state. */
	memset(flash, 0xFF, sizeof(flash));
	memset(page_buf, 0xFF, sizeof(page_buf));
	lock_bits     = 0xFF;
	IsSecure      = SECURE_MODE;
	RunBootloader = true;
	WaitForExit   = false;
	DFU_State     = dfuIDLE;
	DFU_Status    = OK;
	memset(&SentCommand, 0, sizeof(SentCommand));
	ResponseByte  = 0;
	AppStartPtr   = (AppPtr_t)0x0000;
	Flash64KBPage = 0;
	StartAddr     = 0;
	EndAddr       = 0;

	while (shim_remaining() >= sizeof(USB_ControlRequest))
	{
		shim_take(&USB_ControlRequest, sizeof(USB_ControlRequest));
		USB_ControlRequest.wValue  = le16_to_cpu(USB_ControlRequest.wValue);
		USB_ControlRequest.wIndex  = le16_to_cpu(USB_ControlRequest.wIndex);
		USB_ControlRequest.wLength = le16_to_cpu(USB_ControlRequest.wLength);
		/* As USB_USBTask does before dispatching a control request. */
		Endpoint_SelectEndpoint(ENDPOINT_CONTROLEP);
		shim_set_setup_pending(1);
		EVENT_USB_Device_ControlRequest();
		shim_set_setup_pending(0);
	}
	return 0;
}

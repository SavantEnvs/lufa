/* Fuzz the AVRISP-MKII clone programmer firmware (Projects/AVRISP-MKII): the STK500v2/AVRISP-MKII
 * command interpreter V2Protocol_ProcessCommand and everything it dispatches to — parameters,
 * ISP (SPI) programming, and XPROG (PDI/TPI) programming of XMEGA/TINY targets
 * (XPROGProtocol.c, XMEGANVM.c, TINYNVM.c) — as driven by the (untrusted) USB host.
 *
 * The firmware runs natively on a modelled AT90USB1287 (mayhem/shim). The input is the byte
 * stream on the bulk OUT endpoint, delivered in 64-byte USB packets as AVRISP_Task in
 * AVRISP-MKII.c receives them (one V2 command per packet; a command's data may span packets).
 * The bytes the attached target answers over SPI/USART are read from the end of the input,
 * backwards. */
#include <stdint.h>
#include <string.h>
#include "Lib/V2Protocol.h"
#include "shim_usb.h"

/* ISRs of ISPProtocol.c (avr-libc names them __vector_N). */
void TIMER1_OVF_vect(void);
void PCINT0_vect(void);
volatile uint8_t* shim_reg_target(void);

/* CMD_OSCCAL (ISPProtocol_Calibrate) enables the TIMER1 overflow and MISO pin-change interrupts
 * and spins until the ISRs count the calibration window down. Deliver them here: one timer
 * overflow per half cycle, and a MISO toggle whenever the target's next response byte has bit 0
 * set, until the device answers (SUCCESS_TOGGLE_NUM toggles) or the window ends (timeout). */
static void avrisp_irq(void)
{
	if (!(TIMSK1 & (1 << TOIE1)))
	  return;
	unsigned toggles = 0;
	for (unsigned i = 0; i < ISPPROTOCOL_CALIB_HALF_CYCLE_LIMIT; i++)
	{
		TIMER1_OVF_vect();
		if ((PCICR & (1 << PCIE0)) && (*shim_reg_target() & 0x01))
		{
			PCINT0_vect();
			if (++toggles == ISPPROTOCOL_CALIB_SUCCESS_TOGGLE_NUM)
			  return;
		}
	}
}

extern uint8_t shim_eeprom[4096];
extern uint8_t XPROG_SelectedProtocol; /* XPROGProtocol.c */
USB_Request_Header_t USB_ControlRequest; /* normally USBTask.c (not linked: no control endpoint here) */

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	shim_reset(data, size);
	shim_irq_hook = avrisp_irq;

	/* Per-input state: the host-writable parameter table back to its power-on defaults, and the
	 * globals the protocol keeps between commands. */
	V2Params_SetParameterValue(PARAM_SCK_DURATION, 6);
	V2Params_SetParameterValue(PARAM_RESET_POLARITY, 0x01);
	V2Params_SetParameterValue(PARAM_DISCHARGEDELAY, 0x00);
	memset(shim_eeprom, 0xFF, sizeof(shim_eeprom));
	CurrentAddress          = 0;
	MustLoadExtendedAddress = false;
	XPROG_SelectedProtocol  = XPROG_PROTOCOL_PDI;
	XPROG_Param_NVMBase       = 0x010001C0;
	XPROG_Param_EEPageSize    = 32;
	XPROG_Param_NVMCMDRegAddr = 0x33;
	XPROG_Param_NVMCSRRegAddr = 0x32;
	ISPTarget_HardwareSPIMode = true;

	USB_DeviceState = DEVICE_STATE_Configured;
	V2Protocol_Init();

	/* AVRISP_Task */
	while (shim_remaining() > 0)
	{
		Endpoint_SelectEndpoint(AVRISP_DATA_OUT_EPADDR);
		Endpoint_SetEndpointDirection(ENDPOINT_DIR_OUT);
		if (!Endpoint_IsOUTReceived())
		  break;
		size_t before = shim_remaining();
		V2Protocol_ProcessCommand();
		if (shim_remaining() == before)
		  break;
	}
	return 0;
}

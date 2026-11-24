/* Host shim for <avr/io.h>: pulls in avr-libc's real register map for the selected MCU, then
 * redirects every memory-mapped register into a RAM array, and backs the USB data/status
 * registers with the fuzz input (see mayhem/shim/shim_usb.c). */
#ifndef MAYHEM_SHIM_AVR_IO_H
#define MAYHEM_SHIM_AVR_IO_H
#include <stdint.h>
#include_next <avr/io.h>

#undef _MMIO_BYTE
#undef _MMIO_WORD
#undef _MMIO_DWORD
extern volatile uint8_t shim_mem[0x10000];
#define _MMIO_BYTE(a)  (shim_mem[(uint16_t)(a)])
#define _MMIO_WORD(a)  (*(volatile uint16_t*)&shim_mem[(uint16_t)(a)])
#define _MMIO_DWORD(a) (*(volatile uint32_t*)&shim_mem[(uint16_t)(a)])

volatile uint8_t*  shim_reg_data(void);     /* UEDATX / UPDATX: next fuzz byte on read */
volatile uint8_t*  shim_reg_int(void);      /* UEINTX / UPINTX: ready/received while input remains */
volatile uint8_t*  shim_reg_ok(void);       /* UESTA0X / UPSTAX: CFGOK always set */
volatile uint16_t* shim_reg_frame(void);    /* UDFNUM / UHFNUM: advances every read so timeouts expire */
volatile uint16_t* shim_reg_bcx(void);      /* UEBCX / UPBCX: bytes remaining in the input */
volatile uint8_t*  shim_reg_bclx(void);

volatile uint8_t*  shim_reg_ep_cfg0(void);
volatile uint8_t*  shim_reg_pipe_cfg0(void);
#undef UECFG0X
#define UECFG0X (*shim_reg_ep_cfg0())
#undef UPCFG0X
#define UPCFG0X (*shim_reg_pipe_cfg0())
volatile uint8_t*  shim_reg_target(void);
volatile uint8_t*  shim_reg_allset(void);
volatile uint8_t*  shim_reg_toggle(void);
volatile uint8_t*  shim_reg_ticks(void);
#undef SPDR
#define SPDR (*shim_reg_target())
#undef UDR1
#define UDR1 (*shim_reg_target())
#undef SPSR
#define SPSR (*shim_reg_allset())
#undef UCSR1A
#define UCSR1A (*shim_reg_allset())
#undef PIND
#define PIND (*shim_reg_toggle())
#undef GPIOR1
#define GPIOR1 (*shim_reg_ticks())
#undef UEDATX
#define UEDATX (*shim_reg_data())
#undef UPDATX
#define UPDATX (*shim_reg_data())
#undef UEINTX
#define UEINTX (*shim_reg_int())
#undef UPINTX
#define UPINTX (*shim_reg_int())
#undef UESTA0X
#define UESTA0X (*shim_reg_ok())
#undef UPSTAX
#define UPSTAX (*shim_reg_ok())
#undef UDFNUM
#define UDFNUM (*shim_reg_frame())
#undef UHFNUM
#define UHFNUM (*shim_reg_frame())
#undef UEBCX
#define UEBCX (*shim_reg_bcx())
#undef UPBCX
#define UPBCX (*shim_reg_bcx())
#undef UEBCLX
#define UEBCLX (*shim_reg_bclx())
#undef UPBCLX
#define UPBCLX (*shim_reg_bclx())
#endif

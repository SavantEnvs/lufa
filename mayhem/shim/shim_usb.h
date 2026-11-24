#ifndef MAYHEM_SHIM_USB_H
#define MAYHEM_SHIM_USB_H
#include <stdint.h>
#include <stddef.h>
void    shim_reset(const uint8_t* data, size_t size);
size_t  shim_remaining(void);
int     shim_take(void* out, size_t n);
uint8_t shim_take_byte(void);
void    shim_set_setup_pending(int pending);
extern int shim_host_mode;
extern void (*shim_tick_hook)(void);
/* Called when firmware enables interrupts to wait on ISRs (util/atomic.h NONATOMIC_BLOCK). */
extern void (*shim_irq_hook)(void);
/* Device mode: &USB_ControlRequest.bmRequestType, for the control endpoint's data-stage direction. */
extern const volatile uint8_t* shim_ctrl_reqtype;
#endif

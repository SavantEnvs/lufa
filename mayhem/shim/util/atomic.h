#pragma once
/* Interrupts never fire asynchronously on the host. A NONATOMIC_BLOCK is where firmware enables
 * interrupts and busy-waits for ISRs to make progress, so entering one delivers the pending
 * interrupts through the harness's shim_irq_hook (if any). */
void shim_interrupts_on(void);
#define ATOMIC_BLOCK(t) for (int _shim_once = 1; _shim_once; _shim_once = 0)
#define ATOMIC_RESTORESTATE
#define ATOMIC_FORCEON
#define NONATOMIC_BLOCK(t) for (int _shim_once = (shim_interrupts_on(), 1); _shim_once; _shim_once = 0)
#define NONATOMIC_RESTORESTATE

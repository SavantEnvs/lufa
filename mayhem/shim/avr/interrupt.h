#ifndef MAYHEM_SHIM_AVR_INTERRUPT_H
#define MAYHEM_SHIM_AVR_INTERRUPT_H
#include <avr/io.h>
#define sei() do { } while (0)
#define cli() do { } while (0)
#define ISR_BLOCK
#define ISR_NOBLOCK
#define ISR_NAKED
#define ISR_ALIASOF(v)
#define ISR(vector, ...) void vector(void); void vector(void)
#define EMPTY_INTERRUPT(vector) void vector(void) { }
#define reti() return
#endif

#ifndef MAYHEM_SHIM_AVR_BOOT_H
#define MAYHEM_SHIM_AVR_BOOT_H
#include <stdint.h>
/* Device signature / serial-number row: a fixed pattern. */
#define boot_signature_byte_get(addr) ((uint8_t)(0xA5 ^ (uint8_t)(addr)))
#define boot_lock_fuse_bits_get(addr) ((uint8_t)0xFF)
#define GET_LOW_FUSE_BITS      0
#define GET_LOCK_BITS          1
#define GET_EXTENDED_FUSE_BITS 2
#define GET_HIGH_FUSE_BITS     3
#endif

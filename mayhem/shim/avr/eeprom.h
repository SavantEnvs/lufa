#ifndef MAYHEM_SHIM_AVR_EEPROM_H
#define MAYHEM_SHIM_AVR_EEPROM_H
#include <stdint.h>
#include <stddef.h>
#define EEMEM
extern uint8_t shim_eeprom[4096];
static inline uint8_t eeprom_read_byte(const uint8_t* a) { return shim_eeprom[(uintptr_t)a % sizeof(shim_eeprom)]; }
static inline void eeprom_write_byte(uint8_t* a, uint8_t v) { shim_eeprom[(uintptr_t)a % sizeof(shim_eeprom)] = v; }
static inline void eeprom_update_byte(uint8_t* a, uint8_t v) { eeprom_write_byte(a, v); }
static inline void eeprom_read_block(void* d, const void* s, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t*)d)[i] = eeprom_read_byte((const uint8_t*)s + i); }
static inline void eeprom_update_block(const void* s, void* d, size_t n) { for (size_t i = 0; i < n; i++) eeprom_write_byte((uint8_t*)d + i, ((const uint8_t*)s)[i]); }
static inline void eeprom_write_block(const void* s, void* d, size_t n) { eeprom_update_block(s, d, n); }
static inline uint16_t eeprom_read_word(const uint16_t* a) { uint16_t v; eeprom_read_block(&v, a, 2); return v; }
static inline uint32_t eeprom_read_dword(const uint32_t* a) { uint32_t v; eeprom_read_block(&v, a, 4); return v; }
static inline void eeprom_update_word(uint16_t* a, uint16_t v) { eeprom_update_block(&v, a, 2); }
static inline void eeprom_update_dword(uint32_t* a, uint32_t v) { eeprom_update_block(&v, a, 4); }
static inline void eeprom_write_word(uint16_t* a, uint16_t v) { eeprom_update_block(&v, a, 2); }
static inline void eeprom_write_dword(uint32_t* a, uint32_t v) { eeprom_update_block(&v, a, 4); }
#define eeprom_busy_wait() do { } while (0)
#endif

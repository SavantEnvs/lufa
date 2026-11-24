#ifndef MAYHEM_SHIM_AVR_PGMSPACE_H
#define MAYHEM_SHIM_AVR_PGMSPACE_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define pgm_read_byte(a)  (*(const uint8_t*)(a))
#define pgm_read_word(a)  (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#undef pgm_read_ptr
#define pgm_read_ptr(a)   (*(void* const*)(a))
#define pgm_read_byte_near pgm_read_byte
#define pgm_read_word_near pgm_read_word
#define pgm_read_byte_far(a) pgm_read_byte((uintptr_t)(a))
#define memcpy_P memcpy
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strncasecmp_P strncasecmp
#define sprintf_P sprintf
#define snprintf_P snprintf
#define printf_P printf
#endif

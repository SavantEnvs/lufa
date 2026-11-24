/* Host model of the AVR8 USB controller for fuzzing LUFA firmware code natively.
 *
 * Every memory-mapped register lives in shim_mem[] (see shim/avr/io.h). The USB endpoint/pipe
 * data register reads the next byte of the fuzz input; the interrupt/status register reports
 * "data received / bank ready" while input remains and "SETUP received, IN ready, nothing to
 * read" once it is exhausted, so every LUFA stream and wait loop terminates. The frame-number
 * register advances on each read so LUFA's own USB_STREAM_TIMEOUT_MS timeouts expire. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <avr/io.h>
#include "shim_usb.h"

volatile uint8_t shim_mem[0x10000];
uint8_t shim_eeprom[4096];

static const uint8_t* shim_in;
static size_t shim_len, shim_pos;
static int shim_setup_pending;
int shim_ep_is_in(void);
int shim_pipe_is_out(void);
extern int shim_host_mode;
static volatile uint8_t data_scratch, int_scratch, ok_scratch, bclx_scratch;
static volatile uint16_t frame_scratch, bcx_scratch;
static uint8_t int_last;
static uint16_t frame_counter;
void (*shim_tick_hook)(void);
void (*shim_irq_hook)(void);
void shim_interrupts_on(void)
{
	if (shim_irq_hook)
	  shim_irq_hook();
}

/* Per-endpoint/pipe configuration register 0, banked by the selected endpoint/pipe number, so
 * the model knows a stream's direction once the input is exhausted: writes to an IN endpoint
 * (OUT pipe) still complete, reads from an OUT endpoint (IN pipe) stall until LUFA times out. */
static volatile uint8_t ep_cfg0[8], pipe_cfg0[8];
int shim_host_mode;
volatile uint8_t* shim_reg_ep_cfg0(void)   { return &ep_cfg0[UENUM & 7]; }
volatile uint8_t* shim_reg_pipe_cfg0(void) { return &pipe_cfg0[UPNUM & 7]; }
int shim_ep_is_in(void)    { return (UENUM & 7) && (ep_cfg0[UENUM & 7] & (1 << EPDIR)); }
int shim_pipe_is_out(void) { return (pipe_cfg0[UPNUM & 7] & 0x30) == 0x20; }

size_t shim_remaining(void) { return shim_len - shim_pos; }

int shim_take(void* out, size_t n)
{
	if (shim_remaining() < n)
	  return 0;
	memcpy(out, shim_in + shim_pos, n);
	shim_pos += n;
	return 1;
}

uint8_t shim_take_byte(void)
{
	uint8_t b = 0;
	shim_take(&b, 1);
	return b;
}

void shim_set_setup_pending(int pending) { shim_setup_pending = pending; }

/* Bulk/interrupt OUT endpoints (and IN pipes) deliver the input in 64-byte packets: clearing the
 * bank (Endpoint_ClearOUT / Pipe_ClearIN) discards what is left of the current packet. */
#define SHIM_PACKET 64
static size_t pkt_start;
static int int_last_bulk_read;

/* The control endpoint is bidirectional: while a SETUP packet is pending its bytes are read, and
 * afterwards the data stage direction is the request's bmRequestType direction bit. */
const volatile uint8_t* shim_ctrl_reqtype;
static int shim_is_read_side(void)
{
	if (shim_host_mode)
	  return !shim_pipe_is_out();
	if (!(UENUM & 7))
	  return shim_setup_pending || !shim_ctrl_reqtype || !(*shim_ctrl_reqtype & 0x80);
	return !shim_ep_is_in();
}

static int shim_is_bulk_read_side(void)
{
	return shim_host_mode ? ((UPNUM & 7) && !shim_pipe_is_out()) : ((UENUM & 7) && !shim_ep_is_in());
}

static size_t shim_packet_left(void)
{
	size_t end = pkt_start + SHIM_PACKET;
	if (end > shim_len) end = shim_len;
	return (end > shim_pos) ? end - shim_pos : 0;
}

/* Observe what LUFA wrote back into the status register since the last access. */
static void shim_sync(void)
{
	uint8_t cleared = int_last & ~int_scratch;
	int_last = int_scratch;
	if (cleared & (1 << 3))
	  shim_setup_pending = 0; /* Endpoint_ClearSETUP */
	if ((cleared & ((1 << 2) | (1 << 0) | (1 << 7))) && int_last_bulk_read)
	{
		/* Endpoint_ClearOUT / Pipe_ClearIN: release the bank, next packet starts. */
		if (shim_pos < pkt_start + SHIM_PACKET)
		  shim_pos = (pkt_start + SHIM_PACKET < shim_len) ? pkt_start + SHIM_PACKET : shim_len;
		pkt_start = shim_pos;
	}
	if (shim_pos > pkt_start + SHIM_PACKET)
	  pkt_start = shim_pos;
}

volatile uint8_t* shim_reg_data(void)
{
	shim_sync();
	if (!shim_is_read_side())
	  return &data_scratch;      /* a write towards the host/device: sink it */
	if (shim_is_bulk_read_side() && !shim_packet_left())
	  data_scratch = 0;          /* reading past the end of the packet */
	else
	  data_scratch = (shim_pos < shim_len) ? shim_in[shim_pos++] : 0;
	return &data_scratch;
}

volatile uint8_t* shim_reg_int(void)
{
	shim_sync();
	uint8_t v;
	int bulk_read = shim_is_bulk_read_side();
	if (shim_pos < shim_len)
	  v = (1 << 0) | (1 << 2) | (1 << 7) | ((!bulk_read || shim_packet_left()) ? (1 << 5) : 0);
	else if (shim_host_mode)
	  v = (1 << 2) | (shim_pipe_is_out() ? (1 << 5) : 0); /* OUT pipe ready + writable; IN pipe idle */
	else if (!(UENUM & 7))
	  v = (1 << 0) | (1 << 2) | (1 << 3); /* control: IN ready, status-stage OUT, SETUP (host aborted) */
	else
	  v = (1 << 0) | (1 << 3) | (shim_ep_is_in() ? (1 << 5) : 0); /* IN ready, SETUP (host aborted), IN writable */
	if (shim_setup_pending)
	  v |= (1 << 3);
	int_last = v;
	int_scratch = v;
	int_last_bulk_read = bulk_read; /* the endpoint/pipe a later write-back applies to */
	return &int_scratch;
}

/* Target-side serial data (SPI SPDR / USART UDR1 of the AVRISP programmer): the attached target's
 * responses are a second stream, taken from the END of the input backwards, so they never shift
 * the host's USB packets at the front. */
static volatile uint8_t target_scratch, status_scratch, pind_scratch;
volatile uint8_t* shim_reg_target(void)
{
	target_scratch = (shim_pos < shim_len) ? shim_in[--shim_len] : 0;
	return &target_scratch;
}
volatile uint8_t* shim_reg_allset(void) { status_scratch = 0xFF; return &status_scratch; }
volatile uint8_t* shim_reg_toggle(void) { pind_scratch ^= 0xFF; return &pind_scratch; }

/* GPIOR1 is the AVRISP command timeout counter (decremented by its 10 ms timer ISR): model time
 * passing as register reads, so every wait on the target times out. */
static volatile uint8_t gpior1_scratch;
static unsigned gpior1_reads;
volatile uint8_t* shim_reg_ticks(void)
{
	if (gpior1_scratch && (++gpior1_reads % 8) == 0)
	  gpior1_scratch--;
	return &gpior1_scratch;
}

volatile uint8_t* shim_reg_ok(void) { ok_scratch = 0xFF; return &ok_scratch; }

volatile uint16_t* shim_reg_frame(void)
{
	frame_scratch = ++frame_counter;
	if (shim_tick_hook)
	  shim_tick_hook();
	return &frame_scratch;
}

volatile uint16_t* shim_reg_bcx(void)
{
	shim_sync();
	size_t r = shim_is_bulk_read_side() ? shim_packet_left() : shim_remaining();
	bcx_scratch = (r > 64) ? 64 : (uint16_t)r;
	return &bcx_scratch;
}

volatile uint8_t* shim_reg_bclx(void)
{
	shim_sync();
	size_t r = shim_is_bulk_read_side() ? shim_packet_left() : shim_remaining();
	bclx_scratch = (r > 64) ? 64 : (uint8_t)r;
	return &bclx_scratch;
}

void shim_reset(const uint8_t* data, size_t size)
{
	memset((void*)shim_mem, 0, sizeof(shim_mem));
	memset(shim_eeprom, 0xFF, sizeof(shim_eeprom));
	shim_in = data; shim_len = size; shim_pos = 0;
	shim_setup_pending = 0; int_last = 0; int_scratch = 0; frame_counter = 0; pkt_start = 0; int_last_bulk_read = 0;
	gpior1_scratch = 0; gpior1_reads = 0; pind_scratch = 0;
	memset((void*)ep_cfg0, 0, sizeof(ep_cfg0)); memset((void*)pipe_cfg0, 0, sizeof(pipe_cfg0));
}


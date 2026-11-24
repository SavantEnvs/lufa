#!/usr/bin/env bash
#
# mayhem/build.sh — build LUFA's fuzz targets and the functional tests.
#
# LUFA is AVR8/XMEGA/UC3 firmware with no host build, so its sources are compiled natively against
# avr-libc's register headers (from /opt/toolchains/deps, installed by mayhem/Dockerfile) plus the
# small host shim in mayhem/shim: <avr/io.h> keeps avr-libc's AT90USB1287 register map but maps every
# register to RAM, and backs the USB endpoint/pipe data and status registers with the fuzz input
# (mayhem/shim/shim_usb.c). The PC-side hid_bootloader_cli is built against mayhem/fakeusb, a
# stand-in libusb-0.1 that presents one bootloader device.
#
# Outputs (mayhem/out/, gitignored):
#   fuzz_hidparser, fuzz_rndis_device, fuzz_device_control, fuzz_mass_storage, fuzz_host_configure,
#   fuzz_avrisp_v2, fuzz_bootloader_cdc, fuzz_bootloader_dfu
#                                                       libFuzzer targets (+ *-standalone reproducers)
#   hid_bootloader_cli                                  AFL-instrumented file-input target
#   test/{hidparser_test,hid_bootloader_cli}            normal-flag builds for mayhem/test.sh
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${AVR_LIBC_INCLUDE:=/opt/toolchains/deps/avr-libc-2.2.1/include}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
mkdir -p "$OUT/test"

[ -f "$AVR_LIBC_INCLUDE/avr/iousbxx6_7.h" ] || { echo "avr-libc headers missing at $AVR_LIBC_INCLUDE" >&2; exit 1; }

# The firmware build: AT90USB1287 (USB device + host capable), as LUFA's own demos default to.
FW_FLAGS=(-std=gnu99 -I"$SRC/mayhem/shim" -idirafter "$AVR_LIBC_INCLUDE" -I"$SRC"
          -DARCH=ARCH_AVR8 -D__AVR_AT90USB1287__ -DF_USB=8000000UL -DF_CPU=8000000UL
          -Wno-unused-command-line-argument)
FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"

$CC $FUZZ_CFLAGS -c "$SRC/mayhem/lsan_off.c" -o "$OUT/lsan_off.o"
$CC $FUZZ_CFLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"

# fuzz_target <name> <extra cflags (one string)> <sources...>
fuzz_target() {
  local name="$1" extra="$2"; shift 2
  local objdir="$OUT/obj/$name"; mkdir -p "$objdir"
  local objs=() src obj
  for src in "$@"; do
    obj="$objdir/$(echo "$src" | tr '/' '_' ).o"
    # shellcheck disable=SC2086
    $CC $FUZZ_CFLAGS -fsanitize=fuzzer-no-link "${FW_FLAGS[@]}" $extra -c "$SRC/$src" -o "$obj"
    objs+=("$obj")
  done
  # shellcheck disable=SC2086
  $CC $FUZZ_CFLAGS $LIB_FUZZING_ENGINE "${objs[@]}" "$OUT/lsan_off.o" -o "$OUT/$name"
  # shellcheck disable=SC2086
  $CC $FUZZ_CFLAGS "${objs[@]}" "$OUT/standalone_main.o" "$OUT/lsan_off.o" -o "$OUT/$name-standalone"
}

# 1) HID report descriptor parser (host side: descriptor from an attached device).
fuzz_target fuzz_hidparser "" \
  mayhem/harnesses/fuzz_hidparser.c LUFA/Drivers/USB/Class/Common/HIDParser.c

# 2) RNDIS device class driver (control requests + bulk packets from the USB host).
fuzz_target fuzz_rndis_device "-DUSB_DEVICE_ONLY -DINTERRUPT_CONTROL_ENDPOINT" \
  mayhem/harnesses/fuzz_rndis_device.c mayhem/shim/shim_usb.c \
  LUFA/Drivers/USB/Class/Device/RNDISClassDevice.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 3) USB device control requests: the standard request handler + HID/CDC/MS/Audio/Printer class handlers,
#    with RAM descriptors (USE_RAM_DESCRIPTORS) and an 8-byte control endpoint, as a composite demo device.
D=LUFA/Drivers/USB/Class/Device
fuzz_target fuzz_device_control "-DUSB_DEVICE_ONLY -DINTERRUPT_CONTROL_ENDPOINT -DUSE_RAM_DESCRIPTORS -DFIXED_CONTROL_ENDPOINT_SIZE=8 -DFIXED_NUM_CONFIGURATIONS=1" \
  mayhem/harnesses/fuzz_device_control.c mayhem/shim/shim_usb.c \
  $D/HIDClassDevice.c $D/CDCClassDevice.c $D/MassStorageClassDevice.c $D/AudioClassDevice.c $D/PrinterClassDevice.c \
  LUFA/Drivers/USB/Core/DeviceStandardReq.c LUFA/Drivers/USB/Core/Events.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 3a) USB Mass Storage device: the Bulk-Only Transport class driver + the mass-storage demo's SCSI command
#     decoder (Demos/Device/ClassDriver/MassStorage, BOARD=USBKEY as its makefile). Built without
#     INTERRUPT_CONTROL_ENDPOINT so the driver's wait loops call USB_USBTask (see the harness). The
#     harness #includes $MSD/Lib/SCSI.c so it can reset that file's static SenseData per input.
MSD=Demos/Device/ClassDriver/MassStorage
fuzz_target fuzz_mass_storage "-I$SRC/$MSD -DBOARD=BOARD_USBKEY -DUSB_DEVICE_ONLY -DUSE_FLASH_DESCRIPTORS -DFIXED_CONTROL_ENDPOINT_SIZE=8 -DFIXED_NUM_CONFIGURATIONS=1" \
  mayhem/harnesses/fuzz_mass_storage.c mayhem/shim/shim_usb.c \
  $D/MassStorageClassDevice.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 3b) USB host class drivers: every *_Host_ConfigurePipes walking a device's configuration descriptor.
H=LUFA/Drivers/USB/Class/Host
fuzz_target fuzz_host_configure "-DUSB_HOST_ONLY -DINTERRUPT_CONTROL_ENDPOINT" \
  mayhem/harnesses/fuzz_host_configure.c mayhem/shim/shim_usb.c \
  $H/HIDClassHost.c $H/CDCClassHost.c $H/MassStorageClassHost.c \
  $H/StillImageClassHost.c $H/PrinterClassHost.c $H/MIDIClassHost.c $H/AudioClassHost.c \
  $H/AndroidAccessoryClassHost.c LUFA/Drivers/USB/Class/Common/HIDParser.c \
  LUFA/Drivers/USB/Core/HostStandardReq.c LUFA/Drivers/USB/Core/ConfigDescriptors.c \
  LUFA/Drivers/USB/Core/AVR8/Pipe_AVR8.c LUFA/Drivers/USB/Core/AVR8/PipeStream_AVR8.c

# 4) AVRISP-MKII clone programmer: V2 command interpreter + ISP/XPROG (built as Projects/AVRISP-MKII/makefile
#    does: its Config/ headers, BOARD=USBKEY). NO_VTARGET_DETECT skips the ADC VTARGET sampling, a
#    hardware-only path; INTERRUPT_CONTROL_ENDPOINT keeps stream functions from servicing endpoint 0.
#    -fpack-struct=1 reproduces the AVR ABI (every type is 1-byte aligned): the firmware reads
#    host commands straight into unpacked structs (e.g. XPROGProtocol_ReadMemory's parameters),
#    whose wire layout only matches without x86 padding. UBSan's `alignment` check is off for this
#    target only, since on AVR no access is misaligned; ASan and the rest of UBSan stay halting.
P=Projects/AVRISP-MKII
fuzz_target fuzz_avrisp_v2 "-fpack-struct=1 -fno-sanitize=alignment -I$SRC/$P -I$SRC/$P/Config -DUSE_LUFA_CONFIG_HEADER -DBOARD=BOARD_USBKEY -DNO_VTARGET_DETECT -DINTERRUPT_CONTROL_ENDPOINT" \
  mayhem/harnesses/fuzz_avrisp_v2.c mayhem/shim/shim_usb.c \
  $P/Lib/V2Protocol.c $P/Lib/V2ProtocolParams.c $P/Lib/ISP/ISPProtocol.c $P/Lib/ISP/ISPTarget.c \
  $P/Lib/XPROG/XPROGProtocol.c $P/Lib/XPROG/XPROGTarget.c $P/Lib/XPROG/XMEGANVM.c $P/Lib/XPROG/TINYNVM.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 4b) CDC bootloader (Bootloaders/CDC, as its makefile: Config/ headers, BOOT_START_ADDR of an 8 KB boot
#     section on the AT90USB1287). The harness #includes BootloaderCDC.c and stands in for
#     BootloaderAPI.c (SPM) with a RAM flash image. The source insists on -Os (__OPTIMIZE_SIZE__) only
#     to fit the boot section; the fuzz build keeps -O1 and defines the macro.
B=Bootloaders/CDC
fuzz_target fuzz_bootloader_cdc "-I$SRC/$B -I$SRC/$B/Config -DUSE_LUFA_CONFIG_HEADER -DBOOT_START_ADDR=0x1E000 -DBOARD=BOARD_USBKEY -D__OPTIMIZE_SIZE__=1 -Wno-builtin-macro-redefined" \
  mayhem/harnesses/fuzz_bootloader_cdc.c mayhem/shim/shim_usb.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 4c) DFU bootloader (Bootloaders/DFU), built and stubbed like the CDC bootloader above.
B=Bootloaders/DFU
fuzz_target fuzz_bootloader_dfu "-I$SRC/$B -I$SRC/$B/Config -DUSE_LUFA_CONFIG_HEADER -DBOOT_START_ADDR=0x1E000 -DBOARD=BOARD_USBKEY -D__OPTIMIZE_SIZE__=1 -Wno-builtin-macro-redefined" \
  mayhem/harnesses/fuzz_bootloader_dfu.c mayhem/shim/shim_usb.c \
  LUFA/Drivers/USB/Core/AVR8/Endpoint_AVR8.c LUFA/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c

# 5) hid_bootloader_cli (PC tool, Intel HEX file) — AFL-instrumented, against the fake libusb.
#    shellcheck disable=SC2086
afl-clang-fast $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -DUSE_LIBUSB -I"$SRC/mayhem/fakeusb" \
  "$SRC/Bootloaders/HID/HostLoaderApp/hid_bootloader_cli.c" "$SRC/mayhem/fakeusb/fakeusb.c" \
  "$SRC/mayhem/lsan_off.c" -o "$OUT/hid_bootloader_cli"

# 6) Functional tests, normal flags.
# shellcheck disable=SC2086
$CC -O2 $COVERAGE_FLAGS "${FW_FLAGS[@]}" "$SRC/mayhem/tests/hidparser_test.c" \
  "$SRC/LUFA/Drivers/USB/Class/Common/HIDParser.c" -o "$OUT/test/hidparser_test"
# shellcheck disable=SC2086
$CC -O2 $COVERAGE_FLAGS -DUSE_LIBUSB -I"$SRC/mayhem/fakeusb" \
  "$SRC/Bootloaders/HID/HostLoaderApp/hid_bootloader_cli.c" "$SRC/mayhem/fakeusb/fakeusb.c" \
  -o "$OUT/test/hid_bootloader_cli"

echo "build.sh: done"

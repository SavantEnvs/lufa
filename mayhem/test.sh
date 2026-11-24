#!/usr/bin/env bash
#
# mayhem/test.sh — known-answer functional tests (LUFA ships no host test suite). Runs what
# mayhem/build.sh built with normal flags in mayhem/out/test and diffs the results:
#   1. HID report parser: parse a standard mouse descriptor + decode a known report, diff against
#      mayhem/tests/hidparser_test.expected.
#   2. hid_bootloader_cli: parse mayhem/tests/blink.ihx and program it into the fake bootloader;
#      assert the reported byte count and the checksum of every block sent over USB.
#   3. hid_bootloader_cli must reject a corrupt HEX file (bad checksum).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

T="$SRC/mayhem/out/test"
for b in hidparser_test hid_bootloader_cli; do
  [ -x "$T/$b" ] || { echo "missing $T/$b — run mayhem/build.sh" >&2; emit_ctrf "lufa-known-answer" 0 1; exit 1; }
done
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
passed=0; failed=0
check() { if "$@"; then passed=$((passed+1)); echo "PASS: $name"; else failed=$((failed+1)); echo "FAIL: $name"; fi; }

name="hidparser mouse descriptor"
"$T/hidparser_test" > "$tmp/hid.out" 2>&1
check diff -u "$SRC/mayhem/tests/hidparser_test.expected" "$tmp/hid.out"

name="hid_bootloader_cli programs blink.ihx"
FAKEUSB_REPORT=1 "$T/hid_bootloader_cli" -mmcu=atmega32u4 -v "$SRC/mayhem/tests/blink.ihx" > "$tmp/cli.out" 2>&1
check grep -q -e 'Read ".*blink.ihx": 16 bytes' "$tmp/cli.out"
name="hid_bootloader_cli USB transfer checksum"
check grep -qx 'fakeusb: blocks=2 sum=2138592' "$tmp/cli.out"

name="hid_bootloader_cli rejects bad checksum"
sed 's/6A$/6B/' "$SRC/mayhem/tests/blink.ihx" > "$tmp/bad.ihx"
"$T/hid_bootloader_cli" -mmcu=atmega32u4 "$tmp/bad.ihx" > "$tmp/bad.out" 2>&1
check grep -q 'rror reading intel hex file' "$tmp/bad.out"

emit_ctrf "lufa-known-answer" "$passed" "$failed"

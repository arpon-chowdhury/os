#!/bin/sh
# Verify BIOS handoff, welcome rendering, and repeated PIT delivery.
set -eu
qemu=$1
iso=$2
output=$3
seconds=$4
shift 4
case "$seconds" in
    ''|*[!0-9]*|0) echo 'BOOT_TIMEOUT must be a positive integer.' >&2; exit 2 ;;
esac
command -v "$qemu" >/dev/null 2>&1 || { echo "Missing QEMU: $qemu" >&2; exit 1; }
test -f "$iso" || { echo "Missing ISO: $iso" >&2; exit 1; }
mkdir -p "$output"
debug="$output/boot-test-debug.log"
serial="$output/boot-test-serial.log"
stderr="$output/boot-test-qemu.log"
# Truncate before launch so stale success cannot pass a failed boot.
: > "$debug"
: > "$serial"
: > "$stderr"
"$qemu" -machine q35 -accel tcg -cpu qemu64 "$@" \
    -cdrom "$iso" -boot d -display none -serial "file:$serial" -monitor none \
    -debugcon "file:$debug" -global isa-debugcon.iobase=0xe9 \
    -no-reboot -no-shutdown > "$stderr" 2>&1 &
pid=$!
cleanup() {
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
elapsed=0
while [ "$elapsed" -lt "$seconds" ]; do
    if grep -Fxq 'ON.: Limine handoff OK; x86-64 kernel entered.' "$debug" &&
       grep -Fxq 'ON.: framebuffer welcome screen ready.' "$debug" &&
       grep -Fxq 'ON.: system ticks reached 200.' "$debug"; then
        echo "Verified x86-64 BIOS handoff, welcome rendering, and PIT tick progress; logs: $output/boot-test-*.log"
        exit 0
    fi
    if grep -Eq 'ON\.: (framebuffer unavailable|unsupported framebuffer)' "$debug"; then
        echo "Framebuffer unavailable or unsupported; inspect $debug" >&2
        exit 1
    fi
    if grep -Fq 'ON.: unsupported Limine base revision.' "$debug"; then
        echo "Unsupported Limine base revision; inspect $debug" >&2
        exit 1
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "QEMU exited before verified handoff; inspect $stderr and $serial" >&2
        exit 1
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done
echo "No verified handoff within ${seconds}s; inspect $debug, $serial, and $stderr" >&2
exit 1

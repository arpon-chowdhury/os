#!/usr/bin/env python3
"""Boot QEMU, capture the actual display, or verify the no-VGA fallback."""
import argparse
import json
import os
import re
import select
import shutil
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu')
    parser.add_argument('iso')
    parser.add_argument('output')
    parser.add_argument('--timeout', type=int, default=30)
    parser.add_argument('--virtual-test', action='store_true')
    parser.add_argument('--memory-test', type=int, choices=[1, 2, 3])
    parser.add_argument('--ram', choices=['128M', '256M'], default='256M')
    parser.add_argument('--no-framebuffer', action='store_true')
    parser.add_argument('--ovmf-code', type=Path)
    parser.add_argument('--ovmf-vars', type=Path)
    parser.add_argument('--exception', choices=['ud', 'de', 'gp', 'pf', 'df', 'stack', 'ro', 'nx', 'unmapped'])
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--nm', default='llvm-nm')
    args = parser.parse_args()
    if args.exception and args.memory_test:
        parser.error('--exception and --memory-test must be run separately')
    if args.exception and (not args.elf or args.no_framebuffer):
        parser.error('--exception requires --elf and a framebuffer')
    if bool(args.ovmf_code) != bool(args.ovmf_vars):
        parser.error('--ovmf-code and --ovmf-vars must be supplied together')
    if args.ovmf_code and args.no_framebuffer:
        parser.error('no-framebuffer fallback is currently tested with BIOS only')
    for firmware in (args.ovmf_code, args.ovmf_vars):
        if firmware and not firmware.is_file():
            parser.error('Missing OVMF firmware: ' + str(firmware))
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = 'no-framebuffer' if args.no_framebuffer else 'framebuffer'
    if args.ovmf_code:
        prefix = 'uefi-framebuffer'
    if args.exception:
        prefix = 'exception-' + args.exception + ('-uefi' if args.ovmf_code else '-bios')
    if args.memory_test:
        prefix = 'memory-' + str(args.memory_test) + ('-uefi' if args.ovmf_code else '-bios')
    debug = output / (prefix + '-debug.log')
    serial = output / (prefix + '-serial.log')
    screenshot = output / ('welcome-uefi.png' if args.ovmf_code else 'welcome.png')
    if args.exception or args.memory_test:
        screenshot = output / (prefix + '.png')
    debug.write_text('')
    serial.write_text('')
    if not args.no_framebuffer:
        screenshot.unlink(missing_ok=True)
    command = [args.qemu, '-machine', 'q35', '-accel', 'tcg', '-cpu', 'qemu64',
               '-m', args.ram, '-cdrom', str(Path(args.iso).resolve()), '-boot', 'd',
               '-display', 'none', '-vga', 'none' if args.no_framebuffer else 'std',
               '-serial', 'file:' + str(serial), '-monitor', 'none',
               '-debugcon', 'file:' + str(debug), '-global', 'isa-debugcon.iobase=0xe9',
               '-qmp', 'stdio', '-no-reboot', '-no-shutdown']
    if args.ovmf_code:
        # Each test starts with clean NVRAM; never modify installed firmware.
        variables = output / 'ovmf-test-vars.fd'
        if variables.resolve() in (args.ovmf_code.resolve(), args.ovmf_vars.resolve()):
            parser.error('The test NVRAM output must not overwrite a firmware input')
        shutil.copyfile(args.ovmf_vars, variables)
        command += ['-drive', 'if=pflash,format=raw,unit=0,readonly=on,file=' + str(args.ovmf_code.resolve()),
                    '-drive', 'if=pflash,format=raw,unit=1,file=' + str(variables)]
    with (output / (prefix + '-qemu.log')).open('w') as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=errors)
        try:
            deadline = time.monotonic() + args.timeout
            expected = ('ON.: framebuffer unavailable; screen skipped.' if args.no_framebuffer
                        else 'ON.: framebuffer welcome screen ready.')
            if args.exception:
                expected = 'ON.: fatal exception halted safely.'
            else:
                expected = 'ON.: system ticks reached 200.'
            handoff = 'ON.: Limine handoff OK; x86-64 kernel entered.'

            def serial_lines():
                # UEFI console output can leave ANSI cursor controls before kernel text.
                return re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', serial.read_text()).splitlines()

            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError('QEMU exited before the expected diagnostic')
                if expected in debug.read_text().splitlines() and expected in serial_lines():
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('Timed out; inspect ' + str(debug))
            if handoff not in debug.read_text().splitlines():
                raise RuntimeError('Kernel entry diagnostic missing')
            if expected not in serial_lines() or handoff not in '\n'.join(serial_lines()):
                raise RuntimeError('Expected kernel diagnostic missing from COM1 log')
            if not args.exception and 'ON.: FATAL CPU EXCEPTION' in debug.read_text():
                raise RuntimeError('Unexpected fatal exception during normal boot')
            if args.exception:
                log = debug.read_text()
                vector, error = {'ud': (6, 0), 'de': (0, 0), 'gp': (13, 0x28),
                                 'pf': (14, 2), 'df': (8, 0), 'stack': (6, 0), 'ro': (14, 3), 'nx': (14, 17), 'unmapped': (14, 2)}[args.exception]
                fields = dict((key, int(value, 16)) for key, value in
                              re.findall(r'^([a-z0-9_]+)=(0x[0-9a-f]+)$', log, re.MULTILINE))
                symbols = {}
                for line in subprocess.check_output([args.nm, str(args.elf)], text=True).splitlines():
                    parts = line.split()
                    if len(parts) == 3:
                        symbols[parts[2]] = int(parts[0], 16)
                assert fields['vector'] == vector and fields['error'] == error, fields
                assert fields['rip'] == symbols['x86_test_fault'], fields
                assert fields['cs'] == 8 and fields['ss'] == 16, fields
                assert not fields['rflags'] & 0x200, fields
                stack = symbols['x86_exception_stacks'] + (0 if vector == 8 else 3) * 16384
                assert stack <= fields['handler_frame'] < stack + 16384, fields
                if args.exception == 'stack':
                    assert fields['rsp'] == 0, fields
                if vector == 14:
                    assert fields['cr2'] == (0x400000000000 if args.exception in ('ro', 'nx', 'unmapped') else 0x1000), fields
                if vector == 0:
                    assert fields['rax'] == 1 and fields['rcx'] == 0 and fields['rdx'] == 0, fields
                assert 'ON.: framebuffer welcome screen ready.' in log
                assert 'nested fatal' not in log and 'ERROR exception test returned' not in log
            if not args.exception:
                memory_log = debug.read_text()
                if 'PMM TEST FAILED' in memory_log:
                    raise RuntimeError('Physical memory test failed')
                fallback = args.memory_test in (2, 3)
                memory_marker = ('ON.: physical memory unavailable; allocator disabled.' if fallback
                                 else 'ON.: physical memory ready; 4096-byte frames.')
                if memory_marker not in memory_log.splitlines() or memory_marker not in serial_lines():
                    raise RuntimeError('Missing physical memory diagnostic: ' + memory_marker)
                if fallback:
                    if 'pmm_usable_frames=' in memory_log:
                        raise RuntimeError('Unexpected active allocator in fallback test')
                    markers = ['ON.: PMM TEST missing-response fallback passed.']
                else:
                    fields = {key: int(value, 16) for key, value in
                              re.findall(r'^(pmm_[a-z_]+)=(0x[0-9a-f]+)$', memory_log, re.MULTILINE)}
                    required = ['map_bytes', 'reserved_bytes', 'usable_frames', 'metadata_frames',
                                'metadata_base', 'allocated_frames', 'free_frames']
                    if any('pmm_' + key not in fields for key in required):
                        raise RuntimeError('Missing PMM statistics')
                    if (fields['pmm_usable_frames'] != fields['pmm_free_frames'] +
                            fields['pmm_metadata_frames'] + fields['pmm_allocated_frames'] or
                            fields['pmm_map_bytes'] != fields['pmm_reserved_bytes'] +
                            fields['pmm_usable_frames'] * 4096 or
                            fields['pmm_free_frames'] <= 0 or fields['pmm_allocated_frames'] != 0 or
                            fields['pmm_metadata_frames'] <= 0 or
                            fields['pmm_metadata_base'] == 0 or fields['pmm_metadata_base'] % 4096):
                        raise RuntimeError('Inconsistent PMM statistics: ' + str(fields))
                    # Both firmware paths must leave a substantial portion of RAM usable.
                    if fields['pmm_usable_frames'] * 4096 <= int(args.ram[:-1]) * 1024 * 1024 // 4:
                        raise RuntimeError('Unexpectedly little usable RAM')
                    markers = []
                    if args.memory_test == 1:
                        markers = ['ON.: PMM TEST alignment, reserved protection, and reuse passed.',
                                   'ON.: PMM TEST exhaustion and full restoration passed.']
                        if (fields.get('pmm_test_exhausted_frames') != fields['pmm_free_frames'] or
                                fields.get('pmm_test_restored_frames') != fields['pmm_free_frames']):
                            raise RuntimeError('PMM test did not exhaust and restore every free frame')
                    for key, value in fields.items():
                        if f'{key}=0x{value:016x}' not in serial_lines():
                            raise RuntimeError('PMM statistic missing from COM1: ' + key)
                for marker in markers:
                    if marker not in memory_log.splitlines() or marker not in serial_lines():
                        raise RuntimeError('Missing PMM test marker: ' + marker)
                log = debug.read_text().splitlines()
                if args.virtual_test:
                    if 'ON.: VMM TEST allocation rollback passed.' not in log:
                        raise RuntimeError('Missing VMM rollback success marker')
                    marker = 'ON.: VMM TEST translation, permissions, remap TLB, and reclamation passed.'
                    if marker not in log or marker not in serial_lines():
                        raise RuntimeError('Missing VMM selftest success marker')
                for marker in ('ON.: PIC IRQs ready; PIT ~100 Hz; only IRQ0 unmasked.',
                               'ON.: system ticks reached 100.', 'ON.: system ticks reached 200.'):
                    if marker not in log or marker not in serial_lines():
                        raise RuntimeError('Missing timer diagnostic: ' + marker)
                if ('ON.: framebuffer unavailable; screen skipped.' if args.no_framebuffer
                        else 'ON.: framebuffer welcome screen ready.') not in log:
                    raise RuntimeError('Missing framebuffer diagnostic')
            if args.no_framebuffer:
                if 'welcome screen ready' in debug.read_text():
                    raise RuntimeError('Unexpected render in the no-framebuffer test')
                print('Verified missing-framebuffer fallback and serial diagnostics')
                return
            pending = bytearray()

            def receive(deadline):
                while b'\n' not in pending:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not select.select([process.stdout], [], [], remaining)[0]:
                        raise RuntimeError('Timed out waiting for QMP')
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        raise RuntimeError('QMP closed unexpectedly')
                    pending.extend(chunk)
                line, _, rest = pending.partition(b'\n')
                pending[:] = rest
                return json.loads(line)

            if 'QMP' not in receive(time.monotonic() + 5):
                raise RuntimeError('Invalid QMP greeting')

            def execute(name, arguments=None):
                request = {'execute': name}
                if arguments is not None:
                    request['arguments'] = arguments
                process.stdin.write((json.dumps(request) + '\n').encode())
                process.stdin.flush()
                deadline = time.monotonic() + 5
                while True:
                    response = receive(deadline)
                    if 'error' in response:
                        raise RuntimeError(str(response['error']))
                    if 'return' in response:
                        return response['return']

            execute('qmp_capabilities')
            if args.exception:
                registers = execute('human-monitor-command', {'command-line': 'info registers'})
                assert 'HLT=1' in registers, registers
                (output / (prefix + '-registers.log')).write_text(registers)
                status = execute('query-status')
                assert status['status'] == 'running', status  # QEMU is live; guest CPU is halted.
            execute('screendump', {'filename': str(screenshot), 'format': 'png'})
            if not screenshot.is_file() or screenshot.stat().st_size == 0:
                raise RuntimeError('QEMU did not produce a screenshot')
            print('Verified ' + (args.exception + ' fatal exception, saved frame, IST stack, and halted CPU; ' if args.exception else '') +
                  ('physical memory, timer, ' if not args.exception else '') + 'framebuffer and COM1 diagnostics; screenshot: ' + str(screenshot))
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdin.close()
            process.stdout.close()


if __name__ == '__main__':
    main()

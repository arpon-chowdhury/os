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
    parser.add_argument('--windows-test', action='store_true')
    parser.add_argument('--desktop-test', action='store_true')
    parser.add_argument('--ui-test', action='store_true')
    parser.add_argument('--graphics-test', action='store_true')
    parser.add_argument('--mouse-test', action='store_true')
    parser.add_argument('--terminal-test', action='store_true')
    parser.add_argument('--keyboard-test', action='store_true')
    parser.add_argument('--heap-test', type=int, choices=[1, 2])
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
    if sum(bool(mode) for mode in (args.exception, args.memory_test, args.virtual_test, args.heap_test)) > 1:
        parser.error('injected test modes must be run separately')
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
                heap_marker = ('ON.: kernel heap unavailable; existing services retained.'
                               if fallback or args.heap_test == 2 else
                               'ON.: kernel heap ready; 1 MiB, supervisor writable NX.')
                if heap_marker not in log or heap_marker not in serial_lines():
                    raise RuntimeError('Missing heap initialization diagnostic')
                if args.heap_test:
                    marker = ('ON.: HEAP TEST alignment, coalescing, exhaustion, statistics, and stress passed.'
                              if args.heap_test == 1 else 'ON.: HEAP TEST provisioning rollback passed.')
                    if marker not in log or marker not in serial_lines():
                        raise RuntimeError('Missing heap test success marker')
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
            if args.desktop_test or args.windows_test:
                def wait_ui(marker, start=0):
                    until=time.monotonic()+5
                    while marker not in debug.read_text().splitlines()[start:]:
                        if time.monotonic()>=until: raise RuntimeError('Missing desktop event: '+marker)
                        time.sleep(0.02)
                def keys(text):
                    for key in text:
                        execute('human-monitor-command', {'command-line':'sendkey '+key+' 20'})
                        time.sleep(0.06)
                def shot(name):
                    time.sleep(0.2)
                    execute('screendump', {'filename':str(output/(name+'.png')),'format':'png'})
                keys(list('desktop')+['ret'])
                wait_ui('ON.: desktop ready.')
                measure=output/'desktop-size.ppm'
                execute('screendump', {'filename':str(measure)})
                w,h=map(int,measure.read_bytes().split(b'\n',3)[1].split())
                mx,my=w//2,h//2
                def move(x,y):
                    nonlocal mx,my
                    while (mx,my)!=(x,y):
                        dx=max(-60,min(60,x-mx));dy=max(-60,min(60,y-my))
                        start=len(debug.read_text().splitlines())
                        execute('input-send-event', {'events':[
                            {'type':'rel','data':{'axis':'x','value':dx}},
                            {'type':'rel','data':{'axis':'y','value':dy}}]})
                        until=time.monotonic()+5
                        while not any(line.startswith('ON.: mouse ') for line in debug.read_text().splitlines()[start:]):
                            if time.monotonic()>=until: raise RuntimeError('UI mouse motion stopped')
                            time.sleep(0.02)
                        mx+=dx;my+=dy
                def button(down):
                    execute('input-send-event', {'events':[{'type':'btn','data':{'button':'left','down':down}}]})
                    time.sleep(0.15)
                def pixels():
                    path=output/'desktop-check.ppm'
                    execute('screendump', {'filename':str(path)})
                    return path.read_bytes().split(b'\n',3)[3]
                dark_scene=pixels()
                assert tuple(dark_scene[(h//2*w)*3:(h//2*w)*3+3])==(12,20,38)
                shot('desktop-dark')
                cx,cy=w//8,h//5
                move(cx+30,cy+40);button(True);button(False)
                wait_ui('ON.: UI theme Light.')
                light_scene=pixels()
                assert tuple(light_scene[(h//2*w)*3:(h//2*w)*3+3])==(242,245,250)
                shot('desktop-light')
                # Change wallpaper, then drag the same retained Control surface.
                move(cx+30,cy+130);button(True);button(False)
                wait_ui('ON.: UI activate Change wallpaper')
                assert pixels()!=light_scene, 'Wallpaper did not change'
                shot('desktop-wallpaper')
                move(cx+30,cy+10);button(True);move(cx+90,cy+40);button(False)
                wait_ui('ON.: UI window moved.')
                cx+=60;cy+=30
                move(cx+210,cy+10);button(True);button(False)
                wait_ui('ON.: desktop window minimized.')
                shot('desktop-minimized')
                keys(['f1']+list('zzz'))
                shot('desktop-search-miss')
                keys(['backspace']*3+list('control'))
                shot('desktop-launcher')
                keys(['ret']);wait_ui('ON.: desktop Control opened.')
                move(cx+266,cy+10);button(True);button(False)
                wait_ui('ON.: desktop window closed.')
                start=len(debug.read_text().splitlines())
                move(w//2, h-28);button(True);button(False)
                wait_ui('ON.: desktop Control opened.',start)
                if args.windows_test:
                    # Control was restored above. Its original moved bounds are still retained.
                    def event(key,marker):
                        start=len(debug.read_text().splitlines());keys([key]);wait_ui(marker,start)
                    shot('windows-overlap')
                    event('f6','ON.: UI focus About ON.')
                    event('shift-f6','ON.: UI focus Control')
                    # Resize Control through its bottom/right corner using real PS/2 events.
                    move(cx+277,cy+177);button(True)
                    start=len(debug.read_text().splitlines())
                    move(cx+357,cy+227);button(False);wait_ui('ON.: window resized.',start)
                    wait_ui('ON.: window bounds Control '+str(cx)+','+str(cy)+',360,230',start)
                    shot('windows-resized')
                    event('f8','ON.: window maximized.')
                    wait_ui('ON.: window bounds Control 0,28,'+str(w)+','+str(h-104))
                    shot('windows-maximized')
                    event('f7','ON.: desktop window minimized.')
                    # Restore the maximized Control through its per-window dock item.
                    start=len(debug.read_text().splitlines())
                    move(w//2-110,h-62);button(True);button(False)
                    wait_ui('ON.: desktop Control opened.',start)
                    shot('windows-restored-maximized')
                    event('f8','ON.: window geometry restored.')
                    wait_ui('ON.: window bounds Control '+str(cx)+','+str(cy)+',360,230')
                    shot('windows-restored')
                    # Independent About window lifecycle; Control remains visible.
                    event('f6','ON.: UI focus About ON.')
                    event('f9','ON.: window closed About ON.')
                    shot('windows-about-closed')
                    start=len(debug.read_text().splitlines())
                    move(w//2,h-62);button(True);button(False)
                    wait_ui('ON.: desktop About ON. opened.',start)
                    event('f6','ON.: UI focus Palette')
                    event('f8','ON.: window maximized.')
                    shot('windows-palette-maximized')
                    event('f8','ON.: window geometry restored.')
                    # Mouse close on Palette, then filtered launcher reopen.
                    start=len(debug.read_text().splitlines())
                    move(w//3+266,h//2+10);button(True);button(False)
                    wait_ui('ON.: window closed Palette',start)
                    keys(['f1']+['backspace']*7+list('palette'))
                    shot('windows-launcher-palette')
                    start=len(debug.read_text().splitlines());keys(['ret'])
                    wait_ui('ON.: desktop Palette opened.',start)
                    shot('windows-final')
                keys(['esc']);wait_ui('ON.: retained UI closed; terminal restored.')
                keys(list('mem')+['ret']);wait_ui('ON.: command mem')
                keys(list('heap')+['ret']);wait_ui('ON.: command heap')
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text(): raise RuntimeError('Desktop fatal exception')
                if args.windows_test: print('Verified multiple-window focus, reverse switching, resize, maximize/restore, independent close and dock/launcher restore')
                print('Verified desktop themes, wallpaper, drag, minimize, search, close, restore, and terminal memory commands')
            if args.ui_test:
                def wait_ui(marker, start=0):
                    until=time.monotonic()+5
                    while marker not in debug.read_text().splitlines()[start:] or marker not in serial_lines():
                        if time.monotonic()>=until:
                            raise RuntimeError('Missing UI event: '+marker)
                        time.sleep(0.05)
                for key in ('u','i','ret'):
                    execute('human-monitor-command', {'command-line':'sendkey '+key+' 20'})
                    time.sleep(0.05)
                wait_ui('ON.: retained UI ready; Escape returns to terminal.')
                def capture(name):
                    path=output/(name+'.ppm')
                    execute('screendump', {'filename':str(path)})
                    magic,size,maximum,pixels=path.read_bytes().split(b'\n',3)
                    assert magic==b'P6' and maximum==b'255'
                    w,h=map(int,size.split())
                    return w,h,pixels
                w,h,dark_pixels=capture('ui-dark')
                assert tuple(dark_pixels[(h//2*w)*3:(h//2*w)*3+3])==(12,20,38)
                execute('screendump', {'filename':str(output/'ui-dark.png'),'format':'png'})
                mx,my=w//2,h//2
                def move(x,y):
                    nonlocal mx,my
                    while (mx,my)!=(x,y):
                        dx=max(-60,min(60,x-mx));dy=max(-60,min(60,y-my))
                        start=len(debug.read_text().splitlines())
                        execute('input-send-event', {'events':[
                            {'type':'rel','data':{'axis':'x','value':dx}},
                            {'type':'rel','data':{'axis':'y','value':dy}}]})
                        until=time.monotonic()+5
                        while not any(line.startswith('ON.: mouse ') for line in debug.read_text().splitlines()[start:]):
                            if time.monotonic()>=until: raise RuntimeError('UI mouse motion stopped')
                            time.sleep(0.02)
                        mx+=dx;my+=dy
                def button(down):
                    execute('input-send-event', {'events':[{'type':'btn','data':{'button':'left','down':down}}]})
                    time.sleep(0.15)
                # Focus Control and activate its Light button with the mouse.
                control_x=w//8;control_y=h//5
                move(control_x+30,control_y+44)
                start=len(debug.read_text().splitlines());button(True);button(False)
                wait_ui('ON.: UI activate Light',start);wait_ui('ON.: UI theme Light.',start)
                _,_,light_pixels=capture('ui-light')
                assert tuple(light_pixels[(h//2*w)*3:(h//2*w)*3+3])==(242,245,250)
                execute('screendump', {'filename':str(output/'ui-light.png'),'format':'png'})
                # Tab/Enter are routed to Control, switching to Dark then Auto.
                for theme in ('Dark','Auto'):
                    start=len(debug.read_text().splitlines())
                    execute('human-monitor-command', {'command-line':'sendkey tab 20'});time.sleep(0.05)
                    execute('human-monitor-command', {'command-line':'sendkey ret 20'})
                    wait_ui('ON.: UI theme '+theme+'.',start)
                _,_,auto_pixels=capture('ui-auto')
                assert tuple(auto_pixels[(h//2*w)*3:(h//2*w)*3+3])==(12,20,38)
                # Drag Control by its title bar and verify foreground ownership.
                move(control_x+30,control_y+10);button(True)
                start=len(debug.read_text().splitlines());move(control_x+90,control_y+40)
                wait_ui('ON.: UI window moved.',start);button(False)
                execute('human-monitor-command', {'command-line':'sendkey x 20'})
                wait_ui('ON.: UI key routed to Control')
                start=len(debug.read_text().splitlines())
                execute('human-monitor-command', {'command-line':'sendkey f6 20'})
                wait_ui('ON.: UI focus Canvas',start)
                execute('human-monitor-command', {'command-line':'sendkey z 20'})
                wait_ui('ON.: UI key routed to Canvas',start)
                # Dock selection is a UI affordance, without launching apps.
                move(w//2-160,h-28);start=len(debug.read_text().splitlines());button(True);button(False)
                wait_ui('ON.: UI dock selected.',start)
                execute('screendump', {'filename':str(output/'ui-final.png'),'format':'png'})
                execute('human-monitor-command', {'command-line':'sendkey esc 20'})
                wait_ui('ON.: retained UI closed; terminal restored.')
                for key in ('h','e','l','p','ret'):
                    execute('human-monitor-command', {'command-line':'sendkey '+key+' 20'});time.sleep(0.05)
                wait_ui('ON.: command help')
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text(): raise RuntimeError('UI caused fatal exception')
                print('Verified retained UI mouse activation, theme pixels, keyboard focus, dragging, dock, and terminal restoration')
            if args.graphics_test:
                def key_text(text):
                    for key in text:
                        execute('human-monitor-command', {'command-line': 'sendkey ' + ('ret' if key=='\n' else key) + ' 20'})
                        time.sleep(0.05)
                def marker_wait(marker):
                    until=time.monotonic()+5
                    while marker not in debug.read_text().splitlines() or marker not in serial_lines():
                        if time.monotonic()>=until:
                            raise RuntimeError('Missing graphics diagnostic: '+marker)
                        time.sleep(0.05)
                key_text('graphics\n')
                marker_wait('ON.: graphics demo ready; Escape returns to terminal.')
                demo=output/'graphics-demo.ppm'
                execute('screendump', {'filename': str(demo)})
                execute('screendump', {'filename': str(output/'graphics-demo.png'), 'format':'png'})
                magic,size,maximum,pixels=demo.read_bytes().split(b'\n',3)
                assert magic==b'P6' and maximum==b'255'
                width,height=map(int,size.split())
                def color(x,y):
                    start=(y*width+x)*3
                    return tuple(pixels[start:start+3])
                assert color(0,height//2)==(12,20,38), 'Wrong graphics clear color'
                assert color(0,0)==(24,37,61), 'Top bar missing'
                assert color(8,height-8)==(56,189,248), 'Pixel probe missing'
                panel_x=width//10;panel_y=height//6
                assert color(panel_x+10,panel_y+10)==(24,37,61), 'Rounded panel missing'
                execute('human-monitor-command', {'command-line':'sendkey esc 20'})
                marker_wait('ON.: graphics demo closed; terminal restored.')
                key_text('help\n')
                marker_wait('ON.: command help')
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text():
                    raise RuntimeError('Graphics caused a fatal exception')
                print('Verified graphics demo pixels, screenshot, Escape restoration, and terminal input')
            if args.mouse_test:
                for marker in ('ON.: PS/2 mouse ready; IRQ12, three-byte packets.',
                               'ON.: framebuffer mouse cursor ready.'):
                    if marker not in debug.read_text().splitlines() or marker not in serial_lines():
                        raise RuntimeError('Missing mouse initialization: ' + marker)
                def mouse_input(events, expected):
                    start = len(debug.read_text().splitlines())
                    execute('input-send-event', {'events': events})
                    until = time.monotonic() + 5
                    while time.monotonic() < until:
                        if expected in debug.read_text().splitlines()[start:] and expected in serial_lines():
                            return
                        time.sleep(0.05)
                    raise RuntimeError('Missing mouse event: ' + expected)
                def relative(x, y):
                    return [{'type': 'rel', 'data': {'axis': 'x', 'value': x}},
                            {'type': 'rel', 'data': {'axis': 'y', 'value': y}}]
                mouse_input(relative(30, 20), 'ON.: mouse X+ Y- L0 R0')
                mouse_input(relative(-30, -20), 'ON.: mouse X- Y+ L0 R0')
                for button, pressed, marker in [('left', True, 'L1 R0'), ('left', False, 'L0 R0'),
                                                ('right', True, 'L0 R1'), ('right', False, 'L0 R0')]:
                    mouse_input([{'type': 'btn', 'data': {'button': button, 'down': pressed}}],
                                'ON.: mouse ' + marker)
                def clear_help():
                    execute('human-monitor-command', {'command-line': 'sendkey c-l-e-a-r-ret 20'})
                    time.sleep(0.3)
                    for key in ('h', 'e', 'l', 'p', 'ret'):
                        execute('human-monitor-command', {'command-line': 'sendkey ' + key + ' 20'})
                        time.sleep(0.05)
                    time.sleep(0.3)
                clear_help()
                execute('screendump', {'filename': str(output / 'mouse-baseline.ppm')})
                # Keep each injection within one PS/2 packet. QEMU otherwise
                # carries large relative motion into later sample periods.
                for _ in range(20):
                    mouse_input(relative(-100, -100), 'ON.: mouse X- Y+ L0 R0')
                clear_help()
                execute('screendump', {'filename': str(output / 'mouse-over-text.ppm')})
                mouse_input(relative(100, 100), 'ON.: mouse X+ Y- L0 R0')
                execute('screendump', {'filename': str(output / 'mouse-after-move.ppm')})
                # Compare the restored top-left text against the baseline.
                def ppm(path):
                    raw = path.read_bytes()
                    magic, size, maximum, pixels = raw.split(b'\n', 3)
                    if magic != b'P6' or maximum != b'255':
                        raise RuntimeError('Unexpected QEMU PPM format')
                    w, h = map(int, size.split())
                    return w, h, pixels
                w, h, over = ppm(output / 'mouse-over-text.ppm')
                w2, h2, moved = ppm(output / 'mouse-after-move.ppm')
                assert (w, h) == (w2, h2)
                assert over != moved, 'Mouse cursor did not visibly move'
                assert over[(4*w+1)*3:(4*w+1)*3+3] == bytes([255]*3), 'Cursor did not clamp at top-left'
                bw, bh, baseline = ppm(output / 'mouse-baseline.ppm')
                assert (bw, bh) == (w, h)
                for y in range(16):
                    start = y*w*3
                    assert moved[start:start+12*3] == baseline[start:start+12*3], 'Cursor damaged terminal text'
                for button, down, marker in [('left', True, 'L1 R0'), ('right', True, 'L1 R1'),
                                              ('left', False, 'L0 R1'), ('right', False, 'L0 R0')]:
                    mouse_input([{'type': 'btn', 'data': {'button': button, 'down': down}}], 'ON.: mouse '+marker)
                for _ in range(20):
                    mouse_input(relative(100, 100), 'ON.: mouse X+ Y- L0 R0')
                execute('screendump', {'filename': str(output / 'mouse-bottom-right.ppm')})
                cw, ch, corner = ppm(output / 'mouse-bottom-right.ppm')
                offset = ((ch-16+4)*cw + cw-12+1)*3
                assert corner[offset:offset+3] == bytes([255]*3), 'Cursor did not clamp at bottom-right'
                commands_before = debug.read_text().splitlines().count('ON.: command help')
                for key in ('h', 'e', 'l', 'p', 'ret'):
                    execute('human-monitor-command', {'command-line': 'sendkey ' + key + ' 20'})
                    mouse_input(relative(-10, -10), 'ON.: mouse X- Y+ L0 R0')
                until = time.monotonic() + 5
                while debug.read_text().splitlines().count('ON.: command help') <= commands_before:
                    if time.monotonic() >= until:
                        raise RuntimeError('Keyboard failed during concurrent mouse input')
                    time.sleep(0.05)
                assert 'ON.: command help' in debug.read_text().splitlines()
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text():
                    raise RuntimeError('Mouse caused a fatal exception')
                print('Verified IRQ12 mouse movement, left/right press/release, visible cursor, and keyboard coexistence')
            if args.terminal_test:
                if 'ON.: framebuffer terminal ready.' not in debug.read_text().splitlines():
                    raise RuntimeError('Terminal initialization failed')
                def type_line(text):
                    keynames = {' ': 'spc', '!': 'shift-1', '\b': 'backspace', '\n': 'ret'}
                    for ch in text:
                        key = keynames.get(ch, 'shift-' + ch.lower() if ch.isupper() else ch)
                        execute('human-monitor-command', {'command-line': 'sendkey ' + key + ' 20'})
                        time.sleep(0.05)
                type_line('help\nversion\nticks\nmem\nheap\nunknown\n')
                # Force scrolling, then verify editing and Shift on a fresh screen.
                for _ in range(16):
                    type_line('echo scroll\n')
                execute('screendump', {'filename': str(output / 'terminal-scroll.png'), 'format': 'png'})
                type_line('clear\necho Ab!x\b\nhelp\n')
                until = time.monotonic() + 5
                required = ['help', 'version', 'ticks', 'mem', 'heap', 'unknown', 'clear', 'echo']
                while time.monotonic() < until:
                    if all('ON.: command ' + name in debug.read_text().splitlines() and
                           'ON.: command ' + name in serial_lines() for name in required):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError('Terminal command dispatch failed')
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text():
                    raise RuntimeError('Terminal caused a fatal exception')
                print('Verified interactive terminal commands, editing, Shift input, and scrolling')
            if args.keyboard_test:
                marker = 'ON.: PS/2 keyboard ready; IRQ1, translated set 1.'
                if marker not in debug.read_text().splitlines() or marker not in serial_lines():
                    raise RuntimeError('Keyboard initialization failed')
                for keys in ('a', 'shift-a', 'shift-1', 'ret', 'backspace', 'a'):
                    execute('human-monitor-command', {'command-line': 'sendkey ' + keys})
                    time.sleep(0.2)
                wanted = ['ON.: key a', 'ON.: key A', 'ON.: key !',
                          'ON.: key ENTER', 'ON.: key BACKSPACE', 'ON.: key a']
                until = time.monotonic() + 5
                while time.monotonic() < until:
                    actual = [line for line in debug.read_text().splitlines() if line.startswith('ON.: key ')]
                    serial_actual = [line for line in serial_lines() if line.startswith('ON.: key ')]
                    if actual == wanted and serial_actual == wanted:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError('Keyboard events differ: ' + repr(actual))
                execute('human-monitor-command', {'command-line': 'sendkey b'})
                until = time.monotonic() + 5
                while 'ON.: key b' not in debug.read_text().splitlines():
                    if time.monotonic() >= until:
                        raise RuntimeError('Keyboard IRQ delivery stopped')
                    time.sleep(0.1)
                if 'ON.: FATAL CPU EXCEPTION' in debug.read_text():
                    raise RuntimeError('Keyboard caused a fatal exception')
                print('Verified IRQ1 keyboard input, Shift release, Enter, Backspace, and continued delivery')
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

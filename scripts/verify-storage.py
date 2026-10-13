#!/usr/bin/env python3
"""Create disposable images, verify ATA I/O and reboot persistence under BIOS/UEFI."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

SECTORS=4096

def original_image():
    return bytes((lba*17+n*13+(n>>8))&255 for lba in range(SECTORS) for n in range(512))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', required=True)
    parser.add_argument('--make', default='make')
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--ovmf-code', required=True)
    parser.add_argument('--ovmf-vars', required=True)
    parser.add_argument('--timeout', default='60')
    args=parser.parse_args()
    root=args.build.resolve()
    if not root.is_relative_to(Path('build').resolve()) or root==Path('build').resolve():
        parser.error('test output must be a subdirectory of repository build/')
    root.mkdir(parents=True,exist_ok=True)
    images={}
    for mode in range(1,6):
        output=root/('mode-'+str(mode))
        if output.is_symlink(): parser.error('test output must not be a symlink')
        output.mkdir(exist_ok=True)
        with (output/'build.log').open('w') as log:
            subprocess.run([args.make,'iso','BUILD='+str(output.relative_to(Path.cwd())),
                            'STORAGE_TEST='+str(mode)],stdout=log,stderr=subprocess.STDOUT,check=True)
        images[mode]=output/'on.iso'
    before=original_image()
    expected=bytearray(before)
    expected[32*512:40*512]=bytes((0xa7+n*29+(n>>8))&255 for n in range(4096))
    evidence=[]
    for firmware in ('bios','uefi'):
        folder=root/firmware
        if folder.is_symlink(): parser.error('test output must not be a symlink')
        folder.mkdir(exist_ok=True)
        disk=folder/'disposable.raw'
        if disk.is_symlink(): parser.error('disposable image must not be a symlink')
        disk.write_bytes(before)
        for mode in range(1,6):
            if mode in (4,5): disk.write_bytes(before)
            command=[sys.executable,'scripts/verify-framebuffer.py',args.qemu,str(images[mode]),
                     str(folder/('mode-'+str(mode))),'--timeout',args.timeout,'--storage-test',str(mode)]
            if mode!=3: command+=['--storage-image',str(disk)]
            if mode in (2,4): command+=['--storage-readonly']
            if mode==1: command+=['--windows-test']
            if firmware=='uefi': command+=['--ovmf-code',args.ovmf_code,'--ovmf-vars',args.ovmf_vars]
            subprocess.run(command,check=True)
            actual=disk.read_bytes()
            assert actual==(before if mode==4 else expected), firmware+' mode '+str(mode)+' changed unexpected disk bytes'
            evidence.append({'firmware':firmware,'mode':mode,'whole_disk_match':True})
            print('Verified exact full-disk content after '+firmware+' storage mode '+str(mode),flush=True)
    (root/'results.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('All 10 BIOS/UEFI storage checks passed; durable reboot contents, outside-range preservation and read-only image integrity verified.')

if __name__=='__main__': main()

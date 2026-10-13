#!/usr/bin/env python3
"""Filesystem verification on exclusively created disposable raw images."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

BASE=64
VOLUME=66
BANK=33
PAYLOAD=16384

def put32(b,o,n): struct.pack_into('<I',b,o,n)
def put64(b,o,n): struct.pack_into('<Q',b,o,n)
def crc(b): return zlib.crc32(b)&0xffffffff

def blank_image():
    disk=bytearray(b'\xa5'*(4096*512))
    payload=bytearray(PAYLOAD)
    payload[:8]=b'ONNODES1';put32(payload,8,1);put32(payload,12,16)
    payload[512]=2;put64(payload,536,1)
    for bank in range(2):
        offset=(BASE+bank*BANK)*512
        header=bytearray(512);header[:8]=b'ONFSCOW1'
        put32(header,8,1);put32(header,12,bank);put64(header,16,BASE)
        put32(header,24,VOLUME);put32(header,28,32);put64(header,32,bank+1)
        put32(header,40,crc(payload));put32(header,508,crc(header[:508]))
        disk[offset:offset+512]=header;disk[offset+512:offset+512+PAYLOAD]=payload
    return bytes(disk)

def banks(disk):
    valid=[]
    for bank in range(2):
        offset=(BASE+bank*BANK)*512;h=disk[offset:offset+512];p=disk[offset+512:offset+512+PAYLOAD]
        if h[:8]==b'ONFSCOW1' and struct.unpack_from('<I',h,508)[0]==crc(h[:508]) and struct.unpack_from('<I',h,40)[0]==crc(p):
            valid.append((struct.unpack_from('<Q',h,32)[0],bank,p))
    return sorted(valid)

def check_files(disk):
    good=banks(disk);assert good
    payload=good[-1][2];nodes={}
    for i in range(16):
        entry=payload[512+i*64:576+i*64]
        if entry[0]: nodes[i]=(entry[0],struct.unpack_from('<I',entry,8)[0],entry[32:64].split(b'\0')[0],struct.unpack_from('<I',entry,12)[0],struct.unpack_from('<I',entry,16)[0])
    def find(path):
        parent=0
        for name in path.split('/')[1:]:
            choices=[i for i,e in nodes.items() if i and e[1]==parent and e[2]==name.encode()]
            assert len(choices)==1;parent=choices[0]
        return nodes[parent]
    note=find('/docs/sub/note');assert note[0]==1 and note[3]==1512
    expected=bytearray((n*23+(n>>8))&255 for n in range(1512));expected[510:515]=b'ONFS!'
    assert payload[2048+note[4]*512:2048+note[4]*512+1512]==expected
    stable=find('/stable');assert stable[3]==6 and payload[2048+stable[4]*512:2048+stable[4]*512+6]==b'anchor'
    assert not any(e[2] in (b'trash',b'empty') for e in nodes.values())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',required=True);parser.add_argument('--make',default='make')
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--ovmf-code',required=True);parser.add_argument('--ovmf-vars',required=True)
    parser.add_argument('--timeout',default='60')
    args=parser.parse_args();root=args.build.resolve();allowed=Path('build').resolve()
    if not root.is_relative_to(allowed) or root==allowed: parser.error('output must be below repository build/')
    root.mkdir(parents=True,exist_ok=True)
    run=Path(tempfile.mkdtemp(prefix='run-',dir=root))
    images={}
    for mode in range(1,8):
        output=run/('image-'+str(mode));output.mkdir()
        with (output/'build.log').open('w') as log:
            subprocess.run([args.make,'iso','BUILD='+str(output.relative_to(Path.cwd())),'FS_TEST='+str(mode)],stdout=log,stderr=subprocess.STDOUT,check=True)
        images[mode]=output/'on.iso'
    original=blank_image();results=[]
    def invoke(firmware,label,mode,image,readonly=False,windows=False):
        before=image.read_bytes();output=image.parent/label
        command=[sys.executable,'scripts/verify-framebuffer.py',args.qemu,str(images[mode]),str(output),'--timeout',args.timeout,'--fs-test',str(mode),'--storage-image',str(image)]
        if readonly: command+=['--storage-readonly']
        if windows: command+=['--windows-test']
        if firmware=='uefi': command+=['--ovmf-code',args.ovmf_code,'--ovmf-vars',args.ovmf_vars]
        subprocess.run(command,check=True)
        after=image.read_bytes();assert len(after)==len(original)
        assert after[:BASE*512]==original[:BASE*512] and after[(BASE+VOLUME)*512:]==original[(BASE+VOLUME)*512:], 'Write escaped filesystem volume'
        if mode in (2,3,4,7): assert after==before,'Read-only/rejected operation modified disk'
        results.append({'firmware':firmware,'case':label,'mode':mode,'volume_bounds_verified':True})
        print('Verified filesystem volume boundaries: '+firmware+' '+label,flush=True)
        return after
    def new_disk(folder,name,data):
        path=folder/(name+'.raw')
        with path.open('xb') as output: output.write(data)
        return path
    for firmware in ('bios','uefi'):
        folder=run/firmware;folder.mkdir()
        disk=new_disk(folder,'persistent',original)
        baseline=invoke(firmware,'create-desktop',1,disk,windows=True);check_files(baseline)
        assert len(banks(baseline))==2
        invoke(firmware,'reboot-persistence',2,disk,readonly=True)
        for kind in ('checksum','semantic'):
            broken=bytearray(baseline)
            for bank in range(2):
                offset=(BASE+bank*BANK)*512
                if kind=='checksum': broken[offset+508]^=1
                else:
                    broken[offset+512+512]=3
                    put32(broken,offset+40,crc(broken[offset+512:offset+512+PAYLOAD]))
                    put32(broken,offset+508,crc(broken[offset:offset+508]))
            invoke(firmware,'corrupt-'+kind,3,new_disk(folder,'corrupt-'+kind,broken),readonly=True)
        damaged=bytearray(baseline);inactive=banks(baseline)[0][1];damaged[(BASE+inactive*BANK)*512]^=1
        invoke(firmware,'degraded-read-only',4,new_disk(folder,'degraded',damaged),readonly=True)
        full=invoke(firmware,'capacity-reuse',5,new_disk(folder,'capacity',original))
        latest=banks(full)[-1][2];assert all(latest[512+i*64]==0 for i in range(1,16)) and latest[2048:]==bytes(PAYLOAD-2048)
        crash=new_disk(folder,'crash',baseline)
        interrupted=invoke(firmware,'hard-kill',6,crash)
        assert len(banks(interrupted))==1;check_files(interrupted)
        invoke(firmware,'hard-kill-recovery',4,crash,readonly=True)
        invoke(firmware,'write-error',7,new_disk(folder,'write-error',baseline),readonly=True)
    (run/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    (root/'latest-run.txt').write_text(str(run)+'\n')
    print('All 18 BIOS/UEFI filesystem checks passed; evidence: '+str(run),flush=True)

if __name__=='__main__': main()

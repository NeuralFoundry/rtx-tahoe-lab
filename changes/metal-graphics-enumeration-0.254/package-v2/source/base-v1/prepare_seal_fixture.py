"""CPU oracle: existing Python binder/ABI encoders vs the new C++ seal.

Synthetic pages deliberately include gaps; they are never used for hardware.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

import prepare_gsp
import gsp_startup

ROOT = Path(__file__).resolve().parent
NAMES = ('radix3', 'bootloader', 'signature', 'metadata', 'queues', 'rmargs', 'libos_args', 'logs', 'booter_load')


def system_fields(bar0=0xfb000000, bar1=0x824000000, bar3=0x820000000,
                  max_va=0x7fffffe00000, revision=0xa1, link_cap=0x12345678):
    return dict(gpuPhysAddr=bar0, gpuPhysFbAddr=bar1, gpuPhysInstAddr=bar3,
                maxUserVa=max_va, PCIRevisionID=revision, pcieConfigReg=link_cap,
                nvDomainBusDeviceFunc=0x100, PCIDeviceID=0x252010de, PCISubDeviceID=0x104c1043,
                pciConfigMirrorBase=0x88000, pciConfigMirrorSize=0x1000,
                hostPageSize=4096, bIsPassthru=True)


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    snapshot = ROOT/'results/gsp-preflight-20260906T122322Z/snapshot.plist'
    if hashlib.sha256(snapshot.read_bytes()).hexdigest() != '384f2c3f3929793525de96e653f8fa5a804928c09de99b6917cb4f4cec20aaf1':
        raise ValueError('Pinned selection evidence changed')
    report, _ = prepare_gsp.prepare(ROOT/'firmware/570.144', snapshot)
    pages = {}
    cursor = 0x100000
    for name in NAMES:
        count = report['host_resources'][name]['pages']
        stride = 4096 if name in ('bootloader', 'logs') else 8192
        pages[name] = [cursor + p*stride for p in range(count)]
        cursor += count*stride + 0x10000
    _, buffers = prepare_gsp.bind(ROOT/'firmware/570.144', pages, snapshot)
    system = gsp_startup.encode_system_info(system_fields())
    registry = gsp_startup.encode_registry({'RMForcePcieConfigSave': 1, 'RMSecBusResetEnable': 1})
    queues = gsp_startup.prefill_queue(pages['queues'], system, registry)
    buffers['queues'] = queues['shared_memory']
    flat = [p for name in NAMES for p in pages[name]]
    (output/'pages.bin').write_bytes(struct.pack('<%dQ' % len(flat), *flat))
    for name in NAMES:
        (output/(name+'.bin')).write_bytes(buffers[name])
    manifest = {name: hashlib.sha256(buffers[name]).hexdigest() for name in NAMES}
    (output/'sha256.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Synthetic seal oracle prepared:', sum(len(b) for b in buffers.values()), 'bytes')


if __name__ == '__main__':
    generate(Path(sys.argv[1]))

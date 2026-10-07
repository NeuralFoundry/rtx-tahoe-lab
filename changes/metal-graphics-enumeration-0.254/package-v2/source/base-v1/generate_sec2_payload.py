"""Generate the exact pinned SEC2 staging image; CPU-only, no device access."""
import argparse
import hashlib
import json
from pathlib import Path

from prepare_gsp import prepare


def generate(firmware, snapshot):
    report, files = prepare(firmware, snapshot)
    selection = report['booter_signature_selection']
    if not selection or (selection['register'], selection['fuse_raw'], selection['signature_index']) != ('0x824148', 1, 0):
        raise ValueError('The measured GA106 SEC2 fuse=1/index0 profile is required')
    image = files['booter-payload-selected.bin']
    if len(image) != 0xec00 or selection['patch_offset'] != 0x8a10 or selection['patch_size'] != 384:
        raise ValueError('Unexpected SEC2 payload geometry')
    padded = image + bytes(0xf000 - len(image))
    image_hash = hashlib.sha256(image).hexdigest()
    text = ['#pragma once', '// Generated from pinned 570.144 assets; no firmware execution permission.',
            'namespace SEC2Payload {',
            'constexpr unsigned ImageBytes = 0xec00, AllocationBytes = 0xf000;',
            'constexpr unsigned CodeOffset = 0x100, CodeBytes = 0x8900;',
            'constexpr unsigned DataOffset = 0x8a00, DataBytes = 0x6200;',
            'constexpr unsigned Fuse = 1, SignatureIndex = 0;',
            'constexpr const char *SHA256 = "' + image_hash + '";',
            'static const unsigned char Image[ImageBytes] = {']
    for offset in range(0, len(image), 16):
        text.append('  ' + ','.join('0x%02x' % value for value in image[offset:offset + 16]) + ',')
    text.extend(('};', '} // namespace SEC2Payload', ''))
    return '\n'.join(text), {
        'schema': 'ga106-sec2-embedded-staging-image-v1', 'image_bytes': len(image),
        'image_sha256': image_hash, 'allocation_bytes': len(padded),
        'allocation_sha256': hashlib.sha256(padded).hexdigest(),
        'dmem_sha256': hashlib.sha256(image[0x8a00:]).hexdigest(),
        'signature_selection': selection, 'firmware_executed': False,
        'live_fuse_recheck_required': True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, default=Path('firmware/570.144'))
    parser.add_argument('--fuse-snapshot', type=Path,
                        default=Path('results/gsp-preflight-20260906T122322Z/snapshot.plist'))
    parser.add_argument('--output', type=Path, default=Path('driver/generated/sec2-payload.hpp'))
    parser.add_argument('--report', type=Path, default=Path('results/sec2-stage-development/payload.json'))
    args = parser.parse_args()
    header, report = generate(args.firmware, args.fuse_snapshot)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding='utf-8')
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

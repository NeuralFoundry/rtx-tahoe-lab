"""0.14 host-only native seal transaction. No firmware/engine launch selector.

Fresh pages are bound and uploaded; native startup bytes are checked against
the separate Python ABI encoder. Finish/Abort are valid in this host-only ABI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import gsp_seal_client as client
import gsp_startup
import prepare_gsp
from prepare_seal_fixture import system_fields

FIELDS = ('magic', 'abi', 'generation', 'phase', 'region_owned', 'frozen', 'exposed',
          'start_attempted', 'sealed', 'checked_bytes', 'compared_pages', 'hashed_pages',
          'bad_resource', 'bad_page', 'bad_byte', 'direct_pages', 'pages_valid',
          'startup_prepared', 'bar0', 'bar1', 'bar3', 'max_user_va', 'revision', 'link_cap',
          'reserved_start', 'reserved_end', 'structural_mask', 'firmware_mask',
          'pci_command', 'provider_open', 'pinned', 'reserved')


def decode_seal(data, generation):
    if type(data) is not bytes or len(data) != 256:
        raise ValueError('SealInfo must have 32 u64 words')
    row = dict(zip(FIELDS, struct.unpack('<32Q', data)))
    if (row['magic'], row['abi'], row['generation']) != (0x5254585345414c31, 1, generation):
        raise ValueError('Wrong seal ABI or generation')
    for name in ('region_owned', 'frozen', 'exposed', 'start_attempted', 'sealed',
                 'direct_pages', 'pages_valid', 'startup_prepared', 'provider_open', 'pinned'):
        if row[name] not in (0, 1):
            raise ValueError('Invalid seal boolean: '+name)
    if row['phase'] > 8 or row['pci_command'] != 0 or row['exposed'] or row['start_attempted'] or row['reserved']:
        raise ValueError('Unexpected state for a host-only seal rehearsal')
    if (row['reserved_start'], row['reserved_end']) != (0x173e00000, 0x17ff00000):
        raise ValueError('Wrong VRAM reservation')
    if row['sealed'] and (row['phase'] != 3 or not all(row[k] for k in
            ('region_owned', 'frozen', 'direct_pages', 'pages_valid', 'startup_prepared', 'provider_open')) or
            row['pinned'] or row['checked_bytes'] != 66404352 or
            row['compared_pages']+row['hashed_pages'] != 16212 or
            row['structural_mask'] != 511 or row['firmware_mask'] != 263):
        raise ValueError('Incomplete native seal evidence')
    return row


def rehearse(backend, firmware, snapshot):
    result = dict(passed=False, probe_version='0.14.0', firmware_executed=False,
                  dma_transfer_executed=False, cleanup_verified=False, connection_closed=False)
    errors = []
    try:
        backend.begin()
        info = client.decode_info(backend.info())
        generation = info['generation']
        pages = {}
        for index, name in enumerate(client.NAMES):
            count = info['resources'][name]['page_count']
            pages[name] = []
            for start in range(0, count, 256):
                pages[name].extend(backend.pages(index, start, min(256, count-start)))
        _, buffers = prepare_gsp.bind(firmware, pages, snapshot)
        for index, name in enumerate(client.NAMES):
            for offset in range(0, len(buffers[name]), 4096):
                backend.write(index, offset, buffers[name][offset:offset+4096])
        backend.prepare_startup()
        backend.publish()
        backend.seal()
        sealed = decode_seal(backend.seal_info(), generation)
        if not sealed['sealed']:
            raise ValueError('Native validator did not seal the contents')
        fields = system_fields(*(sealed[key] for key in
            ('bar0', 'bar1', 'bar3', 'max_user_va', 'revision', 'link_cap')))
        startup = gsp_startup.prefill_queue(pages['queues'], gsp_startup.encode_system_info(fields),
            gsp_startup.encode_registry({'RMForcePcieConfigSave': 1, 'RMSecBusResetEnable': 1}))
        buffers['queues'] = startup['shared_memory']
        digests = {}
        for index, name in enumerate(client.NAMES):
            digest = hashlib.sha256()
            for offset in range(0, len(buffers[name]), 4096):
                digest.update(backend.read(index, offset, min(4096, len(buffers[name])-offset)))
            expected = hashlib.sha256(buffers[name]).hexdigest()
            if digest.hexdigest() != expected:
                raise ValueError('Post-seal independent readback mismatch: '+name)
            digests[name] = expected
        # Only the specific NotPermitted response proves the frozen write gate.
        try:
            backend.write(3, 0, b'\xff')
        except client.BindingError as error:
            if '0xe00002e2' not in str(error):
                raise
        else:
            raise ValueError('Native accepted a write after seal')
        after = decode_seal(backend.seal_info(), generation)
        if after != sealed:
            raise ValueError('Rejected write changed the seal or resource lifetime')
        backend.finish()
        final = client.decode_info(backend.info())
        closed = decode_seal(backend.seal_info(), generation)
        if not final['cleanup_verified'] or final['allocated'] or final['state_name'] != 'finished':
            raise ValueError('Host cleanup incomplete')
        for row in final['resources'].values():
            if row['resources_retained'] or any(row[key] for key in client.CLEANUP_RETURNS):
                raise ValueError('A native completion or clear failed')
        if closed['region_owned'] or closed['provider_open'] or closed['pinned'] or closed['phase'] != 8:
            raise ValueError('Coordinator did not release the host-only lifetime')
        result.update(passed=True, generation=generation, seal=sealed, final_seal=closed,
                      host_bytes_checked=66404352, readback_sha256=digests,
                      frozen_write_rejected=True, cleanup_verified=True)
    except (ValueError, OSError, client.BindingError) as error:
        result['error'] = str(error)
        try:
            result['failure_seal_words'] = list(struct.unpack('<32Q', backend.seal_info()))
        except (ValueError, OSError, client.BindingError):
            pass
    finally:
        try:
            backend.close()
            result['connection_closed'] = True
        except (ValueError, OSError, client.BindingError) as error:
            errors.append(str(error))
    result['cleanup_errors'] = errors
    result['passed'] = result['passed'] and result['connection_closed'] and not errors
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--fuse-snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    # Reserve the evidence path before any native call.
    with args.output.open('x') as output:
        try:
            result = rehearse(client.MacIOKitBackend(), args.firmware, args.fuse_snapshot)
        except (ValueError, OSError, client.BindingError) as error:
            result = dict(passed=False, error=str(error))
        json.dump(result, output, indent=2)
        output.write('\n')
    print(json.dumps({k: result.get(k) for k in ('passed', 'host_bytes_checked', 'cleanup_verified', 'connection_closed', 'error')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

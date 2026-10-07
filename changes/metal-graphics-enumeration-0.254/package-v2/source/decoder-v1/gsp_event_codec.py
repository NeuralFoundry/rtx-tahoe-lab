"""Independent decoding of bounded, immutable 0.16 event capture evidence."""
import hashlib
import json
import struct
import gsp_rpc
import gsp_startup
import gsp_canonical_sequence

FIELDS = ('magic abi generation stop header_valid passed count pages bytes producer polls elapsed_ns '
          'init_done sequencer nocat_count failed_slot failure reader '
          'hdr0 hdr1 hdr2 hdr3 hdr4 hdr5 hdr6 hdr7 max_records max_pages duration_ns max_polls '
          'partial_polls pending_pages').split()
ROW_FIELDS = 'offset bytes function result sequence payload_bytes flags slot elapsed_us'.split()
STOPS = {0: 'not-run', 1: 'init-done', 2: 'sequencer-handler-required',
         3: 'queue-capacity', 4: 'observation-deadline', 5: 'capture-error'}


def decode_info(raw, generation):
    if type(raw) is not bytes or len(raw) != 256:
        raise ValueError('EventInfo requires 32 immutable u64 words')
    r = dict(zip(FIELDS, struct.unpack('<32Q', raw)))
    if not generation or (r['magic'], r['abi'], r['generation']) != (0x52545845564e5431, 1, generation):
        raise ValueError('Wrong event ABI or generation')
    if (r['max_records'], r['max_pages'], r['duration_ns'], r['max_polls']) != (62, 62, 5_000_000_000, 50000):
        raise ValueError('Wrong capture limits')
    if r['stop'] not in STOPS or r['failure'] > 10 or any(r[k] not in (0, 1) for k in
            ('passed', 'header_valid', 'init_done', 'sequencer')):
        raise ValueError('Invalid event state or boolean')
    if (not r['count'] <= r['pages'] <= r['producer'] <= 62 or r['bytes'] != r['pages']*4096 or
            r['count'] == 0 and r['pages'] != 0 or r['nocat_count'] > r['count'] or
            r['partial_polls'] > r['polls'] or r['polls'] > 50000 or r['pending_pages'] > 16):
        raise ValueError('Inconsistent event geometry')
    if r['count'] and (not r['header_valid'] or not r['polls']):
        raise ValueError('Records missing initialized header or poll evidence')
    if bool(r['init_done']) != (r['stop'] == 1) or bool(r['sequencer']) != (r['stop'] == 2):
        raise ValueError('Stop classification disagrees with event flags')
    if bool(r['passed']) != (r['stop'] in (1, 2, 3, 4) and r['count'] > 0):
        raise ValueError('Capture pass disagrees with termination evidence')
    if r['stop'] == 5:
        if not r['failure'] or r['failed_slot'] != r['pages']:
            raise ValueError('Invalid capture failure evidence')
    elif r['failure'] or r['failed_slot'] != 0xffffffff:
        raise ValueError('Unexpected failure evidence')
    if r['stop'] == 0 and any(r[k] for k in ('count', 'polls', 'elapsed_ns', 'header_valid')):
        raise ValueError('Not-run collector has run evidence')
    if r['stop'] == 3 and r['pages'] != 62:
        raise ValueError('Queue capacity stop without full capture')
    if r['stop'] == 4 and r['elapsed_ns'] < r['duration_ns'] and r['polls'] < r['max_polls']:
        raise ValueError('Deadline stop without bounded wait evidence')
    if r['stop'] != 5:
        if r['reader']:
            raise ValueError('Host status queue read index was changed')
        if r['header_valid'] and tuple(r['hdr'+str(i)] for i in range(8)) != (
                0, 0x40000, 4096, 63, r['producer'], 1, 32, 4096):
            raise ValueError('Invalid status header')
    r['stop_name'] = STOPS[r['stop']]
    return r


def decode_index(raw, summary):
    if type(raw) is not bytes or len(raw) != summary['count']*72:
        raise ValueError('Incorrect event index size')
    rows, offset, last_us, nocats = [], 0, 0, 0
    for index in range(summary['count']):
        r = dict(zip(ROW_FIELDS, struct.unpack_from('<9Q', raw, index*72)))
        if (r['offset'] != offset or r['slot'] != offset//4096 or r['sequence'] != index or
                not 4096 <= r['bytes'] <= 65536 or r['bytes'] % 4096 or
                r['function'] > 0xffffffff or r['result'] > 0xffffffff or
                r['payload_bytes'] >= 65456 or (80+r['payload_bytes']+4095)//4096*4096 != r['bytes'] or
                not last_us <= r['elapsed_us'] <= summary['elapsed_ns']//1000):
            raise ValueError('Invalid event index row geometry')
        flags = classify(r['function'], r['result'], r['payload_bytes'])
        if r['flags'] != flags or (flags & 3 and index != summary['count']-1):
            raise ValueError('Invalid terminal event flags or ordering')
        nocats += bool(flags & 4)
        offset += r['bytes']; last_us = r['elapsed_us']; rows.append(r)
    if offset != summary['bytes'] or nocats != summary['nocat_count']:
        raise ValueError('Event index totals disagree with capture')
    tail = rows[-1]['flags'] if rows else 0
    if bool(tail & 1) != bool(summary['init_done']) or bool(tail & 2) != bool(summary['sequencer']):
        raise ValueError('Terminal event disagrees with collector stop')
    return rows


def classify(function, result, length):
    return 8 | int(function == 0x1001 and result == 0 and length == 4) | \
        (2 if function == 0x1002 else 0) | (4 if function == 0x1020 else 0)


def decode_nocat(payload):
    # NVIDIA570.144 NV2080CtrlNocatJournalInsertRecord. Four bytes may follow
    # sizeof(struct) in the RPC placeholder envelope. Never interpret strings
    # past their first NUL or guess the meaning of an errorCode.
    if type(payload) is not bytes or len(payload) not in (1208, 1212):
        raise ValueError('Unsupported NOCAT envelope length')
    def u32(off): return struct.unpack_from('<I', payload, off)[0]
    def u64(off): return struct.unpack_from('<Q', payload, off)[0]
    def string(off):
        value = payload[off:off+65]
        if b'\0' not in value: raise ValueError('Unterminated NOCAT string')
        return value.split(b'\0', 1)[0].decode('utf-8', errors='backslashreplace')
    size = u32(176)
    if size > 1024: raise ValueError('NOCAT diagnostic length exceeds capacity')
    diagnostic = payload[180:180+size]
    return dict(flags=u32(0), timestamp=u64(8), record_type=payload[16],
                assert_record=payload[16] == 5, bugcheck_code=u32(20), source=string(24),
                subsystem=u32(92), error_code=hex(u64(96)), faulting_module=string(104),
                tdr_reason=u32(172), diagnostic_bytes=size,
                diagnostic_sha256=hashlib.sha256(diagnostic).hexdigest(),
                diagnostic_words=list(struct.unpack('<'+str(size//4)+'I', diagnostic)) if size % 4 == 0 else None,
                error_meaning_resolved=False)


def capture(backend, summary, output):
    index = b''.join(backend.event_index(start, min(32, summary['count']-start))
                     for start in range(0, summary['count'], 32))
    (output/'event-index.bin').write_bytes(index)
    rows = decode_index(index, summary)
    details = []
    for row in rows:
        raw = b''.join(backend.event_data(row['offset']+off, min(4096, row['bytes']-off))
                       for off in range(0, row['bytes'], 4096))
        path = 'event-%03d.bin' % row['sequence']
        (output/path).write_bytes(raw)
        parsed = gsp_rpc.decode_record(raw, expected_sequence=row['sequence'])
        rpc = parsed.rpc
        if (len(raw), rpc.function, rpc.result, len(rpc.payload)) != (
                row['bytes'], row['function'], row['result'], row['payload_bytes']):
            raise ValueError('Independent packet decoder differs from native index')
        item = dict(row, file=path, sha256=hashlib.sha256(raw).hexdigest(),
                    rpc_sequence=rpc.sequence, framing_verified=True, payload_sha256=hashlib.sha256(rpc.payload).hexdigest())
        try:
            if row['flags'] & 1:
                item['init_done'] = gsp_startup.parse_init_done(raw, expected_sequence=row['sequence'])
            if row['flags'] & 2: item['sequencer'] = gsp_canonical_sequence.decode(rpc.payload)
            if row['flags'] & 4: item['nocat'] = decode_nocat(rpc.payload)
        except ValueError as error:
            # A malformed diagnostic body does not erase a verified transport
            # prefix. Body errors remain explicit; no sequencer is executed.
            item['body_decode_error'] = str(error)
        details.append(item)
        (output/'events.json').write_text(json.dumps(details, indent=2)+'\n')
    return details

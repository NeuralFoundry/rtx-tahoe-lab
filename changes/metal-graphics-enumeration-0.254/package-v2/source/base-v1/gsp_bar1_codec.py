"""Independent decoder for fixed CPU-to-BAR1 pattern/readback evidence."""
import hashlib
import struct

START=0x01000000
SIZE=8192
FIELDS=('magic abi generation validated attempted passed failure saved_words written_words checked_words captured_bytes restored_words '
        'reads writes ticks cleanup_reads cleanup_writes window_changes window_before window_current window_after last_address last_value '
        'expected pass failed_word start_ns elapsed_ns cleanup_ns window_saved modified original_restored window_restored cleanup_attempted '
        'lease_start lease_end start end bytes capture_bytes budget_ns cleanup_budget_ns max_ticks claimed lease_owned prep_passed '
        'owner_phase pinned pci_command owned bar1_mapped bar1_base mapping_physical mapping_offset mapping_bytes bar1_bytes').split()+['reserved'+str(i) for i in range(56,64)]
BOOLS='validated attempted passed window_saved modified original_restored window_restored cleanup_attempted claimed lease_owned prep_passed pinned owned'.split()


def patterns():
    words=[]
    for i in range(SIZE//4):
        x=(0x9e3779b9^(i*0x45d9f3b)^(START>>12))&0xffffffff
        x=(x^(x<<13))&0xffffffff;x^=x>>17;x=(x^(x<<5))&0xffffffff
        words.append(x)
    return struct.pack('<4096I',*(words+[v^0xffffffff for v in words]))


def decode(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or not generation: raise ValueError('Wrong BAR1 info length/generation')
    r=dict(zip(FIELDS,struct.unpack('<64Q',raw)))
    if (r['magic'],r['abi'],r['generation'])!=(0x5254584241523231,1,generation): raise ValueError('Wrong BAR1 ABI identity')
    if any(r[k] not in (0,1) for k in BOOLS) or any(r[k] for k in FIELDS[56:]): raise ValueError('BAR1 booleans/reserved')
    if (r['lease_start'],r['lease_end'],r['start'],r['end'],r['bytes'],r['capture_bytes'],r['budget_ns'],r['cleanup_budget_ns'],r['max_ticks'])!=(
            0x01000000,0x01010000,START,START+SIZE,SIZE,2*SIZE,15_000_000_000,5_000_000_000,60000): raise ValueError('Wrong BAR1 fixed region/bounds')
    if (r['failure']>10 or r['saved_words']>2048 or r['written_words']>4096 or r['checked_words']>4096 or r['restored_words']>2048 or
            r['captured_bytes']>16384 or r['captured_bytes']%4 or r['ticks']>60001 or r['pass']>1): raise ValueError('BAR1 bounds exceeded')
    if r['bar1_mapped'] not in (0,1): raise ValueError('Invalid BAR1 mapping flag')
    if r['attempted'] and (not r['bar1_mapped'] or r['bar1_base']<=0 or r['bar1_base']>=1<<40 or r['bar1_base']%0x4000000 or
            r['mapping_physical']!=r['bar1_base']+START or r['mapping_offset']!=START or r['mapping_bytes']!=SIZE or r['bar1_bytes']!=0x4000000):
        raise ValueError('Native BAR1 mapping mismatch')
    if r['attempted'] and (not all(r[k] for k in ('validated','claimed','lease_owned','prep_passed','pinned','owned')) or
            r['pci_command']!=6 or r['owner_phase'] not in (7,17)): raise ValueError('Missing current retained BAR1 owner')
    if r['passed'] and (r['failure'] or not all(r[k] for k in BOOLS) or r['owner_phase']!=17 or
            r['saved_words']!=2048 or r['written_words']!=4096 or r['checked_words']!=4096 or r['restored_words']!=2048 or
            r['captured_bytes']!=16384 or r['window_before']!=r['window_after'] or r['window_before']==0xffffffff or
            r['failed_word']!=0xffffffff or r['elapsed_ns']>=r['budget_ns'] or r['cleanup_ns']>=r['cleanup_budget_ns']):
        raise ValueError('Incomplete BAR1 success/restoration evidence')
    return r


def capture(backend,summary,output):
    size=summary['captured_bytes']
    raw=b''.join(backend.bar1_data(off,min(4096,size-off)) for off in range(0,size,4096))
    if len(raw)!=size: raise ValueError('Incomplete native BAR1 data')
    (output/'bar1-pattern-readback.bin').write_bytes(raw)
    matches=raw==patterns()[:size]
    if summary['passed'] and (size!=16384 or not matches): raise ValueError('BAR1 patterns differ from independent serializer')
    return dict(bytes=size,sha256=hashlib.sha256(raw).hexdigest(),patterns_match=matches,
                readback_verified=bool(summary['passed'] and matches and size==16384),
                original_restored=bool(summary['original_restored']),window_restored=bool(summary['window_restored']),
                gpu_compute_verified=False,metal_verified=False)

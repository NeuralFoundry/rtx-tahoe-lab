"""Strict sequence/ownership/continuation evidence ABI. No device access."""
import struct

FIELDS=('magic abi generation validated attempted passed failure completed word opcode ticks reads writes polls resets '
        'imem_commands dmem_commands last_address last_value falcon_start falcon_halted sec2_start resumed '
        'falcon_cpu falcon_mailbox0 falcon_mailbox1 sec2_cpu sec2_mailbox0 riscv bcr handoff elapsed_ns libos_args '
        'workspace_owned workspace_start workspace_end consumer_attempted consumer_written consumer_verified '
        'consumer_failure reader_before reader_after producer sequence_claimed gsp_start_noted sec2_start_noted '
        'after_count after_pages after_bytes after_slot after_sequence after_published after_stop after_passed '
        'after_init_done after_sequencer init_done_observed budget_ns max_ticks payload_bytes used_words '
        'capacity_words operation_count reserved').split()
BOOLS=('validated attempted passed falcon_start falcon_halted sec2_start resumed workspace_owned '
       'consumer_attempted consumer_written consumer_verified sequence_claimed gsp_start_noted sec2_start_noted '
       'after_passed after_init_done after_sequencer init_done_observed').split()


def decode(raw,generation):
    if type(raw) is not bytes or len(raw)!=512 or len(FIELDS)!=64:
        raise ValueError('SequenceInfo requires 64 immutable u64 words')
    r=dict(zip(FIELDS,struct.unpack('<64Q',raw)))
    if not generation or (r['magic'],r['abi'],r['generation'])!=(0x5254585345513031,1,generation):
        raise ValueError('Wrong sequencer identity/ABI')
    if any(r[k] not in (0,1) for k in BOOLS) or r['reserved']:
        raise ValueError('Invalid sequencer boolean/reserved word')
    if (r['budget_ns'],r['max_ticks'],r['payload_bytes'],r['used_words'],r['capacity_words'],r['operation_count']) != (
            15_000_000_000,200000,6296,1564,16354,420):
        raise ValueError('Wrong canonical sequence limits')
    if (r['workspace_start'],r['workspace_end'])!=(0x173c40000,0x173e00000):
        raise ValueError('Wrong sequencer VRAM lease')
    if (r['failure']>13 or r['completed']>420 or r['word']>1564 or r['opcode'] not in (*range(9),0xffffffff) or
            r['ticks']>200001 or r['reads']>200000 or r['writes']>400 or r['polls']>200000 or
            r['resets']>2 or r['imem_commands']>64 or r['dmem_commands']>36 or r['consumer_failure']>8):
        raise ValueError('Sequencer exceeds bounded protocol')
    if r['consumer_written'] and not r['consumer_attempted']:
        raise ValueError('Consumer write without attempt')
    if r['consumer_verified'] and (not r['consumer_written'] or r['consumer_failure'] or
            r['reader_before']!=0 or not 2<=r['reader_after']<=9 or not r['reader_after']<=r['producer']<=62):
        raise ValueError('Consumer publication evidence incomplete')
    if r['sequence_claimed'] and (not r['validated'] or not r['consumer_verified']):
        raise ValueError('Sequence started without validated consumed prefix')
    if r['attempted'] and (not r['sequence_claimed'] or not r['workspace_owned']):
        raise ValueError('Sequence writes outside owned lease')
    if (r['falcon_start'] and not r['gsp_start_noted'] or r['sec2_start'] and not r['sec2_start_noted'] or
            r['sec2_start_noted'] and not r['gsp_start_noted']):
        raise ValueError('Native START ordering evidence invalid')
    if r['passed']:
        required=('validated','attempted','falcon_start','falcon_halted','sec2_start','resumed',
                  'workspace_owned','consumer_verified','sequence_claimed','gsp_start_noted','sec2_start_noted')
        if (not all(r[k] for k in required) or r['failure'] or
                (r['completed'],r['word'],r['resets'],r['imem_commands'],r['dmem_commands'])!=(420,1564,2,64,36) or
                r['sec2_mailbox0'] or not r['riscv']&0x80 or (r['bcr']&0x111)!=0x111 or
                not r['handoff']&0x04000000 or not r['falcon_cpu']&0x10 or
                not 0<r['libos_args']<1<<40 or r['libos_args']%4096):
            raise ValueError('Incomplete sequencer resume evidence')
    if (not r['after_count']<=r['after_pages']<=r['after_published']<=62 or
            r['after_bytes']!=r['after_pages']*4096 or r['after_stop']>5):
        raise ValueError('Invalid continuation geometry')
    if r['after_stop'] and (not r['passed'] or (r['after_slot'],r['after_sequence'])!=(r['reader_after'],r['reader_after']-1)):
        raise ValueError('Continuation without completed sequence/origin')
    if bool(r['after_init_done'])!=(r['after_stop']==1) or bool(r['after_sequencer'])!=(r['after_stop']==2):
        raise ValueError('Invalid continuation stop classification')
    return r


def cross_check(sequence, initial, after):
    if (sequence['generation'],after['generation'])!=(initial['generation'],initial['generation']):raise ValueError('Initial/sequence/continuation generation')
    if sequence['passed'] and (sequence['reader_after'],sequence['after_slot'],sequence['after_sequence'],after['start_slot'],after['start_sequence'])!=(initial['pages'],initial['pages'],initial['count'],initial['pages'],initial['count']):raise ValueError('Initial/sequence/continuation origin')
    for key in ('count','pages','bytes','stop','passed','init_done','sequencer','published'):
        if sequence['after_'+key]!=after[key]:
            raise ValueError('Continuation ABI differs from sequencer ABI: '+key)
    if bool(sequence['init_done_observed']) != bool(initial['init_done'] or after['init_done']):
        raise ValueError('INIT_DONE claim differs from captured events')

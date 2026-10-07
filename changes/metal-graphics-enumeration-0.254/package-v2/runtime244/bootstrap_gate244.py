"""Final pre-dispatch evidence gate. No device, file or process operations."""
from channel_chain_audit import require_clean_chain
import flush_evidence,startup_evidence

def require_clean_bootstrap(result):
    if type(result) is not dict or result.get('passed') is not True:
        raise ValueError('Bootstrap evidence rejected before program dispatch')
    if result.get('probe_version')!='0.83.1':raise ValueError('USERD bootstrap probe version')
    flush=flush_evidence.require_programmed(result.get('flush'),result.get('generation'))
    mapping=result.get('flush_mapping',{})
    if any(mapping.get(k)!=v for k,v in flush.items()) or mapping.get('public_pages_checked')!=16212:raise ValueError('Flush private/public mapping proof')
    digest=mapping.get('public_pages_sha256')
    if type(digest) is not str or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):raise ValueError('Flush bootstrap page digest')
    startup=startup_evidence.require_chain(result)
    if result.get('startup_chain')!=startup:raise ValueError('Initial bootstrap chain proof')
    chain=require_clean_chain(result.get('channel',{}),result.get('execution',{}))
    if result.get('generation')!=chain['generation']:raise ValueError('Bootstrap/channel generation')
    # Inspect every captured stage, including initial events, RM preparation
    # and page-table exchanges. An undecoded journal is not zero-assert proof.
    pending=[(result,0)];seen=set();visited=0;journals=0
    while pending:
        value,depth=pending.pop();visited+=1
        if depth>64 or visited>50000:raise ValueError('Bootstrap diagnostic structure exceeds bounds')
        if type(value) not in (dict,list):continue
        if id(value) in seen:continue
        seen.add(id(value))
        if type(value) is list:
            pending.extend((item,depth+1) for item in value);continue
        if 'nocat_error' in value:raise ValueError('Undecoded firmware journal before dispatch')
        if value.get('function')==0x1020 and ('framing_verified' in value or 'flags' in value) and 'nocat' not in value:
            raise ValueError('Missing decoded firmware journal before dispatch')
        if 'nocat' in value:
            journal=value['nocat']
            if type(journal) is not dict or journal.get('assert_record') is not False:
                raise ValueError('Firmware assertion or incomplete journal before dispatch')
            journals+=1
        if 'assert_record' in value and value['assert_record'] is not False:
            raise ValueError('Firmware assertion before dispatch')
        if 'firmware_assertions' in value and value['firmware_assertions']!=[]:
            raise ValueError('Firmware assertions before dispatch')
        if value.get('firmware_assertions_unresolved',False):
            raise ValueError('Unresolved firmware assertions before dispatch')
        pending.extend((item,depth+1) for item in value.values())
    return dict(passed=True,probe_version='0.83.1',generation=chain['generation'],channel_chain=chain,
                firmware_assertion_count=0,decoded_nonassert_journals=journals,visited_values=visited,
                stage='before-broker-dispatch')

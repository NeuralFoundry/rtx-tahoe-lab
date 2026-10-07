"""075 initial prefix and RX boundary proof; imported decoders perform no I/O."""
import gsp_sequence_codec as sequence_codec
import gsp_init_event_codec
import gsp_init_done
import gsp_compute_prep_codec
import gsp_application_page_tables_native
import gsp_event_codec
import gsp_canonical_sequence
import flush_evidence
import channel_chain_audit
PAYLOAD_SHA256='c87b36fd704822988097b9b7011012c8147eef056970635cb7e7cfc36e05e663'
def require_prefix(initial,records):
 if type(initial) is not dict or type(records) is not list:raise ValueError('Initial prefix shape')
 count=initial.get('count')
 if type(count) is not int or not 1<=count<=8 or len(records)!=count:raise ValueError('Initial prefix record count')
 if (initial.get('passed'),initial.get('header_valid'),initial.get('stop'),initial.get('failure'),initial.get('reader'),initial.get('pages'),initial.get('bytes'),initial.get('nocat_count'),initial.get('sequencer'),initial.get('init_done'))!=(1,1,2,0,0,count+1,(count+1)*4096,count-1,1,0):raise ValueError('Initial prefix geometry/state')
 offset=0
 for i,r in enumerate(records):
  tail=i==count-1;size=8192 if tail else 4096
  if (r.get('offset'),r.get('slot'),r.get('sequence'),r.get('bytes'),r.get('result'),r.get('framing_verified'))!=(offset,offset//4096,i,size,0,True):raise ValueError('Initial prefix index/frame')
  if tail:
   if (r.get('function'),r.get('payload_bytes'),r.get('payload_sha256'),r.get('flags'))!=(0x1002,6296,PAYLOAD_SHA256,10):raise ValueError('Initial canonical sequencer payload')
  elif r.get('function')!=0x1020 or r.get('flags')!=12 or r.get('payload_bytes') not in (1208,1212) or r.get('nocat',{}).get('assert_record') is not False:raise ValueError('Initial firmware journal assertion or missing body')
  if 'body_decode_error' in r:raise ValueError('Initial undecoded body')
  offset+=size
 return dict(passed=True,records=count,pages=count+1,start_slot=count+1,start_sequence=count)
def require_chain(result):
 p=require_prefix(result['collection'],result['events']);s=result['sequence'];a=result['after_collection'];rm=result['rm'];pd=result['page_rm'];gold=result['channel']['rm']
 sequence_codec.cross_check(s,result['collection'],a)
 if (a['start_slot'],a['start_sequence'])!=(p['start_slot'],p['start_sequence']):raise ValueError('Initial/continuation RX boundary')
 if (rm['initial_reader'],rm['initial_sequence'])!=((a['start_slot']+a['pages'])%63,a['start_sequence']+a['count']):raise ValueError('Continuation/RM RX boundary')
 if (pd['initial_reader'],pd['initial_sequence'])!=(rm['rx_reader'],rm['rx_sequence']):raise ValueError('RM/page-directory RX boundary')
 if (gold['initial_reader'],gold['initial_sequence'])!=(pd['rx_reader'],pd['rx_sequence']):raise ValueError('Page-directory/golden RX boundary')
 if (rm['tx_reader'],rm['tx_writer'],pd['tx_reader'],pd['tx_writer'])!=(8,8,9,9):raise ValueError('Bootstrap fixed TX boundary')
 if any(row['generation']!=result['generation'] for row in (result['collection'],s,a,rm,pd,gold)):raise ValueError('Bootstrap stage generation')
 return dict(p,generation=result['generation'],rm_rx_first=rm['initial_sequence'],golden_rx_first=gold['initial_sequence'])

"""Decode private075 DMA/flush snapshots. No device or filesystem operations."""
import hashlib,json,struct
FIELDS=('magic abi generation phase failure address physical page_bytes segment_bytes physical_bytes segments '
 'started allocated dma_allocated zeroed memory_prepared descriptor_attached dma_prepared synchronized ready '
 'exposed retained cleanup_attempted cleanup_succeeded operation error complete_error clear_error memory_complete_error '
 'begun program_generation program_exposed memory_attempted high_attempted low_attempted verified restored passed '
 'before_low before_high planned_low planned_high after_low after_high command_before command_enabled command_after reads writes '
 'low_register high_register address_limit alignment shift high_mask').split()
BOOLS=FIELDS[11:24]+['begun',*FIELDS[31:38]]
COUNTS=dict(radix3=15546,bootloader=6,signature=1,metadata=1,queues=129,rmargs=1,libos_args=1,logs=512,booter_load=15)
def integer(value):return type(value) is int and 0<=value<1<<64
def decode(raw,generation):
 if type(raw) is not bytes or len(raw)!=512 or not integer(generation) or not generation:raise ValueError('Flush snapshot size/generation')
 words=struct.unpack('<64Q',raw);r=dict(zip(FIELDS,words))
 if words[:3]!=(0x525458464c555331,1,generation) or any(words[55:]):raise ValueError('Flush snapshot identity/reserved fields')
 if any(r[n] not in (0,1) for n in BOOLS) or r['phase']>8 or r['failure']>8 or r['operation']>14:raise ValueError('Flush snapshot state')
 if tuple(r[n] for n in FIELDS[49:])!=(0x100c10,0x100c40,1<<40,4096,8,0x7f):raise ValueError('Flush register/address profile')
 if any(r[n]>0xffffffff for n in [*FIELDS[24:29],*FIELDS[38:49]]):raise ValueError('Flush register/error width')
 if r['reads']>4 or r['writes']>2 or r['segments']>1 or r['page_bytes']!=4096:raise ValueError('Flush bounded evidence')
 r['raw_sha256']=hashlib.sha256(raw).hexdigest();return r
def require_programmed(r,generation):
 if type(r) is not dict or any(not integer(r.get(n)) for n in FIELDS):raise ValueError('Missing typed flush proof')
 words=[r[n] for n in FIELDS]+[0]*9;checked=decode(struct.pack('<64Q',*words),generation)
 if checked!=r:raise ValueError('Changed decoded flush snapshot')
 required=FIELDS[11:22]+['begun',*FIELDS[31:38]]
 if any(r[n]!=1 for n in required) or (r['phase'],r['failure'],r['operation'],r['program_generation'])!=(7,0,14,generation):raise ValueError('Flush program/retention proof incomplete')
 if any(r[n] for n in ('cleanup_attempted','cleanup_succeeded','error','complete_error','clear_error','memory_complete_error')):raise ValueError('Flush mapping cleanup/error before dispatch')
 a=r['address']
 if not a or a%4096 or a>(1<<40)-4096 or r['physical']!=a or r['physical_bytes']<4096 or (r['segment_bytes'],r['segments'])!=(4096,1):raise ValueError('Flush private DMA address/extent')
 if r['before_low']==0xffffffff or r['before_high']==0xffffffff:raise ValueError('Flush original registers unreadable')
 if (r['planned_low'],r['planned_high'])!=(a>>8,r['before_high']&0xff000000):raise ValueError('Flush encoded address/reserved bits')
 if (r['after_low'],r['after_high'])!=(r['planned_low'],r['planned_high']):raise ValueError('Flush readback differs from mapping')
 if (r['command_before'],r['command_enabled'],r['command_after'],r['reads'],r['writes'])!=(0,2,0,4,2):raise ValueError('Flush PCI/transaction proof')
 return dict(passed=True,generation=generation,address=a,raw_sha256=r['raw_sha256'],retained=True)
def require_disjoint(r,generation,pages):
 result=require_programmed(r,generation)
 if type(pages) is not dict or pages.keys()!=COUNTS.keys():raise ValueError('Flush bootstrap page resource set')
 seen=set()
 for name,count in COUNTS.items():
  values=pages[name]
  if type(values) is not list or len(values)!=count:raise ValueError('Flush bootstrap page count')
  for address in values:
   if not integer(address) or not address or address%4096 or address>(1<<40)-4096 or address in seen or address==r['address']:raise ValueError('Flush/public DMA page overlap or address')
   seen.add(address)
 raw=json.dumps(pages,sort_keys=True,separators=(',',':')).encode()
 return dict(result,public_pages_checked=len(seen),public_pages_sha256=hashlib.sha256(raw).hexdigest())

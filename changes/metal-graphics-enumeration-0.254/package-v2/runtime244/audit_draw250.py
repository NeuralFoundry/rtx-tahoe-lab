"""Independent read-only verification of a private native graphics transaction."""
from pathlib import Path
import hashlib,json,struct
import root_evidence196,triangle244,draw250
ROOT=Path(__file__).resolve().parent.parent;H=lambda b:hashlib.sha256(b).hexdigest();U=lambda b,o=0:struct.unpack_from('<Q',b,o)[0];V=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
def guards(b,w,h):return b[:256]+b''.join(b[256+y*384+w*4:256+(y+1)*384]for y in range(h))+b[256+h*384:]
def verify(session,serial=1,strict_summary=True):
 session=Path(session);arm=session/'arm';root=root_evidence196.verify(arm);assert root['passed']and root['abi']==242
 generation=U((arm/'owned-root-info-before.bin').read_bytes(),16);handles=list(struct.iter_unpack('<8Q',(arm/'owned-root-handles.bin').read_bytes()));assert len(handles)==9
 begin=session/'graphics-begin243';d=session/('graphics-draw250-'+str(serial));read=lambda name:(d/name).read_bytes()
 app=read('application-request.bin');binding=draw250.decode(app);assert binding['generation']==generation and binding['serial']==serial
 w,h=binding['width'],binding['height'];wire=read('request.bin');assert wire==read('submitted-request.bin')and len(wire)==320
 assert struct.unpack_from('<QIIQQ4I',wire)==(0x5254584752503234,249,320,generation,serial,w,h,384,3)and not any(wire[48:64]+wire[224:256]+wire[304:])
 for role,(off,n)in enumerate(zip((0,0,256,0,0),(4096,48,384*h,16,8192))):assert struct.unpack_from('<QQQII',wire,64+role*32)==(handles[role+4][1],off,n,role,0)
 template=(ROOT/'reference244/draw239.bin').read_bytes();review=json.loads((ROOT/'reference244/kernel-template-review.json').read_bytes());assert H(template)==review['command_sha256']
 words=list(struct.unpack('<1201I',template));values=dict(Program=handles[4][4],Fragment=handles[4][4]+256,Vertex=handles[5][4],Color=handles[6][4]+256,Zero=handles[4][4]+2048,Fence=handles[7][4])
 for row in review['relocations']:
  kind=row['kind']
  if kind.endswith('Hi'):value=values[kind[:-2]]>>32
  elif kind.endswith('Lo'):value=values[kind[:-2]]&0xffffffff
  else:value=dict(Pitch=384,Height=h,ScaleX=V(struct.pack('<f',w/2)),ScaleY=V(struct.pack('<f',h/2)),WidthClip=w<<16,HeightClip=h<<16,Token=serial)[kind]
  words[row['word']]=value|(0x80000000 if row['word']==534 else 0)
 assert read('commands.bin')==struct.pack('<1201I',*words)+bytes(8192-4804)
 assert H(read('program.bin'))==review['program_sha256']
 vertex=app[160+binding['vo']+binding['first']*16:160+binding['vo']+(binding['first']+3)*16];assert read('vertices.bin')==wire[256:304]==vertex
 before=read('before.bin');assert len(before)==36864
 if serial==1:assert before==(begin/'baseline.bin').read_bytes()
 else:assert before==(session/('graphics-draw250-'+str(serial-1))/'after.bin').read_bytes()
 index=V(before,0x888);nxt=(index+1)&31;assert index<32 and V(before,0x88c)==index
 ring=handles[8][4]|(1<<41)|(1201<<42);info=struct.unpack('<32Q',read('graphics-info.bin'));assert read('graphics-info.bin')==read('graphics-stable.bin')
 assert info[:7]==(0x5254584752463234,241,generation,3,0,serial,serial)and info[7]>0 and info[8:10]==(7,1)and 5<=info[11]<=8192 and info[10]==info[11]-4 and info[12]<5000000000
 assert info[13:]==(1,3,1,ring,handles[7][4],1201,serial,index,nxt,24849,w,h,384,5,8192,4804,320,249,0)
 staged=bytearray(before);struct.pack_into('<Q',staged,index*8,ring);assert staged==read('staged.bin');end=handles[8][4]+4804
 for o,v in ((0x888,nxt),(0x88c,nxt),(0x840,end&0xffffffff),(0x844,end&0xffffffff),(0x84c,(V(staged,0x84c)&~255)|(end>>32)),(0x860,(V(staged,0x860)&~255)|(end>>32))):struct.pack_into('<I',staged,o,v)
 assert staged==read('after.bin')
 records=list(struct.iter_unpack('<6Q',read('observations.bin')));assert len(records)==info[11];prior=0
 for i,(stage,time,queue,qmd,host,fence)in enumerate(records):
  assert stage==(i+1 if i<3 else 5 if i+1==len(records)else 4)and prior<=time<=info[12]and qmd==host==0;prior=time;get,put=queue&0xffffffff,queue>>32
  if i<3:
   assert get==put==index and fence<=0xffffffff
   if i==2:assert fence==0
   elif i==1:assert fence==records[0][5]
  else:
   assert get in(index,nxt)and put==nxt and fence in(0,serial);done=get==nxt and fence==serial
   assert done if i+2>=len(records)else not done
 seed=bytearray(triangle244.initial());payload=bytearray(app[160:]);image=read('readback/data-readback.bin');assert len(image)==24849
 for y in range(h):
  at=binding['vb']+binding['co']+y*binding['pitch'];seed[256+y*384:256+y*384+w*4]=payload[at:at+w*4];payload[at:at+w*4]=image[256+y*384:256+y*384+w*4]
 assert read('seed/data-upload.bin')==seed and read('application-result.bin')==payload
 pixels=draw250.pixel_diagnostics(app,bytes(payload))
 assert guards(seed,w,h)==guards(image,w,h)and read('hashes.bin')==b''.join(hashlib.sha256(b).digest()for b in(seed,image,guards(seed,w,h),guards(image,w,h)))
 a,b,c,e=[struct.unpack('<8Q',read(n))for n in('seed/data-info-before.bin','seed/data-info-after.bin','readback/data-info-before.bin','readback/data-info-after.bin')]
 assert b==c and a[:4]==(0x5254584441544131,181,generation,1)and a[7]==b[7]==e[7]==0
 assert b[4:7]==(a[4]+7,a[5]+7,a[6])and e[4:7]==(b[4]+7,b[5],b[6]+7)
 if strict_summary:
  meta=json.loads(read('native-session.json'))
  expected=dict(passed=True,generation=generation,completed=0,failure=0,io_result=0,first_io_error=0,native_iokit=True,probe_version='0.83.1',owned_root_abi=242,graphics_owner_abi=250,graphics_active=True,graphics_completed=serial,graphics_failure=0,owned_dispatch_active=False,owned_data_verified=True,owned_root_verified=True)
  for key,v in expected.items():assert type(meta[key])is type(v)and meta[key]==v,key
 return dict(passed=True,serial=serial,generation=generation,root_abi=242,command_words=1201,program_sha256=H(read('program.bin')),image=pixels,polls=info[10],elapsed_ns=info[12],ticket=info[7],queue_before=index,queue_after=nxt,native_capture_verified=True,hardware_execution_not_inferred_from_files=True)

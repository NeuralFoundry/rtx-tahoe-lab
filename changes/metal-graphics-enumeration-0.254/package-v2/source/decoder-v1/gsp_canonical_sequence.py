"""Read-only Python oracle for the exact075 GA106/570.144 native profile."""
import hashlib,struct
def expected():
 operations=[]
 def op(code,*args):operations.append((code,*args))
 op(2,0x110040,0x80000000,0x80000000,0,3);op(0,0x110040,0);op(5)
 op(0,0x110600,0x114);op(2,0x110118,1,0,0,2)
 for segment,blocks in ((0,64),(1,36)):
  op(0,0x110110,0x173c441 if segment else 0x173c400);op(0,0x110128,0)
  for i in range(blocks):
   op(2,0x110118,1,0,0,2);op(0,0x110114,i*256);op(0,0x11011c,(i+(0 if segment else 1))*256);op(0,0x110118,0x600 if segment else 0x614)
  op(2,0x110118,2,2,0,2)
 for address,value in ((0x111210,0x1f10),(0x11119c,0x400),(0x111198,1),(0x111180,1),(0x110040,0xfe),(0x110104,0x100)):op(0,address,value)
 op(6);op(7);op(8)
 words=[word for item in operations for word in item]
 if len(operations)!=420 or len(words)!=1564:raise ValueError('Canonical sequencer oracle geometry')
 return struct.pack('<10I',16354,1564,*([0]*8))+struct.pack('<1564I',*words)
def decode(payload):
 if type(payload) is not bytes or payload!=expected():raise ValueError('Noncanonical075 GA106 sequencer payload')
 return dict(canonical_profile_verified=True,payload_bytes=6296,capacity_words=16354,used_words=1564,operation_count=420,imem_blocks=64,dmem_blocks=36,payload_sha256=hashlib.sha256(payload).hexdigest(),operations_executed=False,hardware_accessed=False)

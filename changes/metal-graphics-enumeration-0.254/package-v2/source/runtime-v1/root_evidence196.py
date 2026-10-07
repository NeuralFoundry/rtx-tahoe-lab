"""Independent wire/table decoder. Does not load drivers or grant admission."""
import hashlib,json,struct
from pathlib import Path
U=lambda b,o=0:struct.unpack_from('<Q',b,o)[0]
V=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
pack=lambda a:struct.pack('<'+'Q'*len(a),*a)
def initial_requests(root):
    result=[]
    for step,n in enumerate((120,56,4,48,48)):
        b=bytearray(4096);fields={36:14+step,40:1,48:0x3000000,52:0x43505256,56:32+(32 if step<4 else 0)+n,60:103 if step<4 else 54,64:0xffffffff,68:0xffffffff}
        if step<4:
            fields.update({80:0xc1000000,84:0 if step==0 else 0xc1000000 if step==1 else 0xcf000011,88:0xcf000010+step,92:(0,0x80,0x2080,0x90f1)[step],100:n})
            if step==1:fields[116]=0xc1000000
            if step==3:fields[116]=0x48;struct.pack_into('<Q',b,120,0x1fffffb000000);struct.pack_into('<Q',b,152,4096)
        else:
            fields.update({80:0xc1000000,84:0xcf000011,88:0xffffffff,104:4,108:9,112:0xcf000013,120:1,124:0xffffffff});struct.pack_into('<Q',b,96,root)
        for o,v in fields.items():struct.pack_into('<I',b,o,v)
        checksum=0
        for o in range(0,(48+V(b,56)+7)&~7,4):checksum^=V(b,o)
        struct.pack_into('<I',b,32,checksum);result.append(bytes(b))
    return b''.join(result)
def legacy(root,child,gen):
    assert len(root)==12288 and 8192<=len(child)<=45056 and len(child)%4096==0
    assert [U(root,n)for n in range(0,12288,8)]==[{0:0x100322,4096:0x100422,9224:0x100522}.get(n,0)for n in range(0,12288,8)]
    rows=[];used=set()
    for group in range(256):
        lo,hi=struct.unpack_from('<QQ',child,group*16)
        if not(lo or hi):continue
        assert lo==32 and hi&255==2 and hi>>33==0
        at=((hi&0x1ffffff00)<<4)-0x1005000
        assert at>=4096 and at%4096==0 and at+4096<=len(child)and at not in used;used.add(at);count=0
        for p in range(512):
            bits=U(child,at+p*8)
            if not bits:continue
            pa=(bits&0x1ffffff00)<<4
            assert bits==(6<<56)|(pa>>4)|1 and(0x1100000<=pa<0x1101000 or 0x1200000<=pa<0x4000000)
            rows.append((0x1020000000+group*0x200000+p*4096,pa,gen,0,3,1,0));count+=1
        assert count
    assert used==set(range(4096,len(child),4096))and rows
    return rows
def stable(rows,pages,old=None):
    assert len(pages)==64 and len(set(pages))==64 and all(4096<=p<1<<40 and p%4096==0 for p in pages)
    assert rows==sorted(rows)and len({r[0]for r in rows})==len(rows)
    nodes=[(0,0)]if old is None else list(old[0]);shifts=(47,38,29,21)
    if old:
        now={r[0]:r for r in rows};assert all(now.get(r[0])==r for r in old[2])
    for r in rows:
        for d,shift in enumerate(shifts,1):
            key=(d,r[0]>>shift)
            if key not in nodes:nodes.append(key)
    assert len(nodes)<=64
    index={n:i for i,n in enumerate(nodes)};data=bytearray(64*4096)
    for d,key in nodes[1:]:
        parent=(d-1,0 if d==1 else key>>(shifts[d-2]-shifts[d-1]));slot=index[parent]
        word=key&(3 if d==1 else 255 if d==4 else 511)
        at=slot*4096+word*(16 if d==4 else 8)+(8 if d==4 else 0)
        struct.pack_into('<Q',data,at,(pages[index[(d,key)]]>>4)|12|(32 if d==3 else 0))
    for va,pa,owner,aperture,access,cached,reserved in rows:
        assert owner and aperture in(0,2)and access in(1,2,3)and cached in(0,1)and reserved==0
        assert pa%4096==0 and va%4096==0 and 0<=va<1<<49 and 4096<=pa<(1<<40 if aperture==2 else 1<<37)
        assert aperture!=2 or pa not in pages
        bits=(pa>>4)|1|(4 if aperture==2 else 0)|(64 if access==1 else 0)|(128 if access!=3 else 0)|(8 if not cached else 0)
        bits|=6<<56
        struct.pack_into('<Q',data,index[(4,va>>21)]*4096+((va>>12)&511)*8,bits)
    if old:assert all(not U(old[1],off)or U(old[1],off)==U(data,off)for off in range(0,len(data),8))
    return nodes,bytes(data),rows
def stages(root,golden,context,final,gen,data,pages):
    fixed=bytearray(golden);assert U(fixed)==32 and U(fixed,8)==0x100602
    for i,pa in enumerate((0x3402000,0x3403000,0x3400000),1):
        assert U(fixed,4096+i*8)==0;struct.pack_into('<Q',fixed,4096+i*8,(6<<56)|(pa>>4)|1)
    out=[]
    for child in(fixed,context,final):
        rows=legacy(root,child,gen)+data;out.append(stable(rows,pages,out[-1]if out else None))
    return out
def verify(folder):
    p=Path(folder);read=lambda n:(p/('owned-root-'+n+'.bin')).read_bytes()
    before=read('info-before');w=struct.unpack('<64Q',before);assert before==read('info-after')
    assert w[0]==0x525458524f4f5430 and w[1]==242 and 4<w[2]<1<<63 and w[3:7]==(1,6,1,0)
    assert w[10]==4 and w[13:21]==(262144,64,0,0,1,1,1,1)and w[24:32]==(19,0,0,0,0,0,0,0)
    assert w[32:42]==(1,0,1,2,1,0,1,2,0,1)and w[49:55]==(54,0,19,5,5,5)
    assert w[55:]==(w[23],9,1313,5349717,1,w[2],0x2000000000,0x2100000000,0)
    assert 4<=w[21]<w[22]and w[23]==w[22]+6<=5120 and w[7]==w[23]+1313 and w[9]==(w[8]+1)*4096
    root=read('source-root');assert root==read('golden-root')==read('context-root')
    rows=[struct.unpack_from('<QQQIIII',read('rows'),o)for o in range(0,len(read('rows')),40)]
    assert len(rows)==w[7]and rows[:w[55]]==legacy(root,read('source-children'),w[2])
    data=rows[w[55]:];assert len({r[1]for r in data})==1313
    assert len(read('handles'))==576
    va=0x2000000000;off=0
    for b,(n,access) in enumerate(((4097,3),(65537,3),(1048577,3),(4194305,3),(4096,1),(48,1),(24849,3),(16,3),(8192,1)),1):
        mapped=(n+4095)&~4095
        assert struct.unpack_from('<8Q',read('handles'),(b-1)*64)==(w[2],(1<<32)|b,b,b,va,n,mapped,access)
        for page in range(mapped//4096):
            r=data[off];assert r[0]==va+page*4096 and r[2:]==(b,2,2 if access&2 else 1,0,0);off+=1
        va+=mapped+4096
    pageRows=list(struct.iter_unpack('<QQQ',read('pages')));assert len(pageRows)==64
    assert all(i==n and extent>=4096 for n,(i,pa,extent)in enumerate(pageRows));pages=[r[1]for r in pageRows];assert pages[0]==w[11]
    decoded=stages(root,read('golden-children'),read('context-children'),read('source-children'),w[2],data,pages)
    assert [len(t[2])-1313 for t in decoded]==list(w[21:24])and len(decoded[2][0])==w[12]
    assert decoded[2][1]==read('image')and read('digests')==hashlib.sha256(decoded[0][1]).digest()+hashlib.sha256(decoded[2][1]).digest()
    req=read('request');requests=read('initial-requests');assert len(req)==4096 and requests==initial_requests(w[11])and req==requests[16384:]
    expected={36:18,40:1,48:0x3000000,52:0x43505256,56:80,60:54,64:0xffffffff,68:0xffffffff,80:0xc1000000,84:0xcf000011,88:0xffffffff,104:4,108:9,112:0xcf000013,120:1,124:0xffffffff}
    wire=bytearray(4096)
    for o,n in expected.items():struct.pack_into('<I',wire,o,n)
    struct.pack_into('<Q',wire,96,w[11]);checksum=0
    for o in range(0,128,4):checksum^=V(wire,o)
    struct.pack_into('<I',wire,32,checksum);assert req==wire
    # Complete journal framing and object/parameter association, with exactly
    # four ordered allocations followed by the empty SET acknowledgement.
    records=read('records');assert len(records)==w[43]and 5<=w[42]<=16 and w[43]%4096==0 and w[43]<=131072
    off=0;step=0;vas=None
    for n in range(w[42]):
        raw=records[off:];assert len(raw)>=4096
        pagesN=V(raw,40);length=V(raw,56);end=48+length;aligned=(end+7)&~7
        assert 1<=pagesN<=16 and pagesN*4096<=len(raw)and 32<=length<65536-48 and(end+4095)//4096==pagesN
        assert not any(raw[:32])and V(raw,36)==w[45]+n and V(raw,44)==0 and V(raw,48)==0x3000000 and V(raw,52)==0x43505256 and V(raw,64)==0 and V(raw,76)==0
        checksum=0
        for o in range(0,aligned,4):checksum^=V(raw,o)
        assert checksum==0 and not any(raw[end:aligned])and step<5
        fn=V(raw,60)
        if fn==(103 if step<4 else 54):
            assert pagesN==1 and V(raw,68)==0 and V(raw,72)==0
            if step==4:assert length==32
            else:
                request=requests[step*4096:(step+1)*4096];assert V(request,36)==14+step and V(request,60)==103
                assert raw[80:112]==request[80:112]and length==V(request,56)
                if step==0:assert V(raw,112)==0xc1000000 and raw[116:end]==request[116:end]
                elif step==3:
                    assert V(raw,112)==0 and V(raw,116)==0x48 and V(raw,148)==0
                    vas=(U(raw,152),U(raw,120),U(raw,128),U(raw,136),V(raw,144))
                    base,size,lo,hi,big=vas;assert 4096<=base<=0x1000000000 and base%4096==0 and 0x1040000000<=size<=1<<49 and size%4096==0 and big in(0,65536,131072)
                    assert not(lo or hi)or(lo<=hi<size and(hi<0x1000000000 or lo>=0x1040000000))
                else:assert raw[112:end]==request[112:end]
            step+=1
        else:assert fn==0x100c and length>=40 and V(raw,84)==length-40
        off+=pagesN*4096
    assert step==5 and off==len(records)and w[46]==(w[44]+off//4096)%63 and w[48]==w[45]+w[42]
    e=struct.unpack('<64Q',read('external'));g=struct.unpack('<64Q',read('golden-rm'))
    assert e[0:3]==(0x5254584556413237,2,w[2])and e[3:11]==(1,1,1,0,4,5,5,5)
    assert e[11:19]==(w[42],w[43]//4096,w[43],19,19,w[46],w[47],w[48])and e[28:35]==(w[44],w[45],w[42],1,54,0,0)
    assert e[50:63]==(2,1,5,1,*vas,w[11],4,9,20480)
    assert g[0:3]==(0x52545843484e3233,1,w[2])and g[5]==1 and g[6]==0 and g[14:17]==(14,14,w[44])and g[18]==w[45]
    for name,magic in [('golden-info',0x525458534e503233),('context-info',0x5254584558533234)]:
        q=struct.unpack('<64Q',read(name));assert q[:7]==(magic,1,w[2],1,1,0,12288)and q[7]==len(read(name.replace('info','children')))
    return dict(passed=True,abi=242,legacy_stages=list(w[21:24]),table_stages=[len(t[0])for t in decoded],pool_pages=64,data_pages=1313,initial_set_sequence=18,initial_set_ack=True,firmware_executed_by_this_verifier=False)
if __name__=='__main__':
    import sys
    print(json.dumps(verify(sys.argv[1]),sort_keys=True))

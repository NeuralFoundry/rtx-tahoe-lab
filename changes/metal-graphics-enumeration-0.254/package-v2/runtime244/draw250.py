"""Bounded application request and independent diagnostic pixel reference."""
import hashlib,math,struct
from fractions import Fraction as F
PROGRAM=bytes.fromhex('f540da20bbf1d540e80a7b615980c7dd2a9da826f58ac862081723c1168445af')
def request(generation,serial):
 assert generation and serial in(1,2,3)
 width=height=32 if serial==3 else 64;vb,cb,vo,co,pitch=113,33041,16,256,512
 vertices=bytearray((i*3+9)&255 for i in range(vb));color=bytes((i*7+serial)&255 for i in range(cb))
 values=[-.75,-.75,0,0,.75,-.75,1,0,0,.75,.5,1]
 if serial==2:
  for i in(0,4,8):values[i]+=.03125
  for i in(1,5,9):values[i]+=.0625
 struct.pack_into('<12f',vertices,48,*values);out=bytearray(160)
 struct.pack_into('<QII6Q8I',out,0,0x5254584452573438,248,160,generation,serial,vb,cb,vo,co,2,3,width,height,pitch,3,70,1);out[96:128]=PROGRAM
 return bytes(out+vertices+color)
def decode(raw):
 assert type(raw)is bytes and 160<=len(raw)<=160+65536+1048576
 assert struct.unpack_from('<QII',raw)==(0x5254584452573438,248,160)and raw[96:128]==PROGRAM and not any(raw[128:160])
 generation,serial,vb,cb,vo,co=struct.unpack_from('<6Q',raw,16);first,count,w,h,pitch,primitive,format_,flags=struct.unpack_from('<8I',raw,64)
 assert generation and 0<serial<=0xffffffff and 0<vb<=65536 and 0<cb<=1048576 and len(raw)==160+vb+cb and vo%4==0 and co%128==0
 assert count==3 and vo+(first+3)*16<=vb and 0<w<=64 and 0<h<=64 and pitch%128==0 and pitch>=w*4 and co+pitch*h<=cb and (primitive,format_,flags)==(3,70,1)
 values=struct.unpack_from('<12f',raw,160+vo+first*16);assert all(math.isfinite(x)for x in values)and all(-1<=values[i]<=1 for i in(0,1,4,5,8,9))
 return dict(generation=generation,serial=serial,vb=vb,cb=cb,vo=vo,co=co,first=first,width=w,height=h,pitch=pitch,values=values)
def pixel_diagnostics(raw,result):
 p=decode(raw);assert type(result)is bytes and len(result)==p['vb']+p['cb'];w,h=p['width'],p['height'];values=p['values'];vertices=[]
 for i in(0,4,8):vertices.append(((F(values[i])+1)*w/2,(1-F(values[i+1]))*h/2,F(values[i+2]),F(values[i+3])))
 def edge(a,b,x,y):return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])
 area=edge(vertices[0],vertices[1],vertices[2][0],vertices[2][1]);assert area
 inside=outside=edges=outside_changed=bad=0;channels=[0]*4;blue={};expected=bytearray(raw[160:])
 for y in range(h):
  for x in range(w):
   weights=[edge(vertices[(i+1)%3],vertices[(i+2)%3],F(2*x+1,2),F(2*y+1,2))/area for i in range(3)];at=p['vb']+p['co']+y*p['pitch']+x*4
   if min(weights)<0:
    outside+=1;outside_changed+=result[at:at+4]!=expected[at:at+4];continue
   if min(weights)==0:edges+=1;continue
   inside+=1;uv=[sum(weights[i]*vertices[i][2+n]for i in range(3))for n in range(2)]
   # Ideal round-to-nearest diagnostic. Actual hardware conformance is not
   # inferred from this ideal quantizer; retain all exact differences.
   rgba=bytes([max(0,min(255,int(v*255+F(1,2))))for v in uv]+[128,255]);expected[at:at+4]=rgba;blue[result[at+2]]=blue.get(result[at+2],0)+1
   for i in range(4):
    if result[at+i]!=rgba[i]:bad+=1;channels[i]+=1
 return dict(covered_pixels=inside,outside_pixels=outside,edge_pixels_unclassified=edges,outside_changed_pixels=outside_changed,ideal_pixel_byte_differences=bad,channel_differences=channels,covered_blue_histogram=blue,exact_ideal_match=bad==outside_changed==edges==0,metal_conformance_proven=False)

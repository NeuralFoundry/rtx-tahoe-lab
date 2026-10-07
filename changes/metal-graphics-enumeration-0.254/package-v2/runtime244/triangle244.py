"""Independent integer raster reference for the fixed, single-sample triangle."""
import hashlib
WIDTH=HEIGHT=64;PITCH=384;OFFSET=256;BYTES=24849
REFERENCE_SHA256='165398a24402a5fa33fbec25151171b98a4dc9d9bbbb7919d02fb90b60f709e6'
def initial():return bytes((i*29+7)&255 for i in range(BYTES))
def reference():
 result=bytearray(initial());covered=0
 # Doubled framebuffer coordinates. No sample center lies on an edge, so
 # this reference is independent of the top-left tie-breaking convention.
 vertices=((16,16),(112,16),(64,112))
 for y in range(HEIGHT):
  for x in range(WIDTH):
   px,py=2*x+1,2*y+1
   edges=[(b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0])for a,b in zip(vertices,vertices[1:]+vertices[:1])]
   assert 0 not in edges
   if min(edges)>0:
    red=(255*(px-16)+48)//96;green=(255*(py-16)+48)//96
    at=OFFSET+y*PITCH+x*4;result[at:at+4]=bytes((red,green,128,255));covered+=1
 assert covered==1152 and hashlib.sha256(result).hexdigest()==REFERENCE_SHA256
 return bytes(result)
def check(image):
 if type(image)is not bytes or len(image)!=BYTES:raise ValueError('Exact 24849-byte graphics image required')
 wanted=reference();bad=[i for i,(a,b)in enumerate(zip(image,wanted))if a!=b]
 if bad:
  i=bad[0];raise ValueError('Triangle image differs in %d bytes; first offset %d expected %d actual %d'%(len(bad),i,wanted[i],image[i]))
 return dict(passed=True,covered_pixels=1152,total_pixels=4096,whole_backing_bytes=BYTES,sha256=REFERENCE_SHA256,reference_only=True)

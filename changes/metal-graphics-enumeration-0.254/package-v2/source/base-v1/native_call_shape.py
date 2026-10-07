"""Exact frozen Python transport validation, with direct compute forbidden."""
import struct,library_upload
from uploaded_library import need
from gsp_uploaded_client import CHUNK

def validate_call(catalog,selector,scalars,data,output_size):
    if selector==69:raise ValueError("Compute requires standard Metal command submission")
    if type(scalars) not in (tuple,list) or type(data) is not bytes or type(output_size) is not int or not 0<=output_size<=CHUNK:
        raise ValueError('Transport types or output size')
    if any(type(v) is not int or not 0<=v<2**64 for v in scalars):raise ValueError('Scalar uint64 required')
    # All callers are the fixed wrappers above; never accept arbitrary ABI.
    if type(selector) is not int or selector not in range(78) or 56 <= selector <= 59 or len(scalars) > (4 if selector==71 else 3) or len(data) > CHUNK or output_size > CHUNK:
        raise ValueError("Request exceeds restricted transport ABI")
    if selector>=68:
        if type(data) is not bytes or any(type(v) is not int or not 0<=v<2**64 for v in scalars):raise ValueError('Program transport types')
        shape=(len(scalars),len(data),output_size)
        if selector in (68,69,70) and shape!={68:(0,0,512),69:(0,2112,0),70:(1,0,1024)}[selector]:raise ValueError('Program transport shape')
        if selector==71 and (len(scalars)!=4 or data or not 0<output_size<=4096 or output_size!=scalars[3]):raise ValueError('Program capture shape')
    if selector>=72:
        shape=(len(scalars),len(data),output_size)
        if selector in (72,73,75):
            need(shape=={72:(0,0,256),73:(0,128,0),75:(0,0,0)}[selector],'upload transport shape')
        if selector==73:
            gen=struct.unpack_from('<Q',data,16)[0]
            need(data==library_upload.begin_header(gen,catalog.library,catalog.code),'selected upload header')
        if selector==74:
            need(len(scalars)==1 and not output_size and 0<=scalars[0]<=4096 and scalars[0]%1024==0 and len(data)==min(1024,4608-scalars[0]),'upload chunk shape')
            need(data==(catalog.library+catalog.code)[scalars[0]:scalars[0]+len(data)],'selected upload bytes')
        if selector==76:
            need(len(scalars)==3 and not data and scalars[0]<2 and 0<output_size<=1024 and scalars[2]==output_size,'upload read shape')
            total=(512,4096)[scalars[0]];need(scalars[1]<total and scalars[2]<=total-scalars[1],'upload read bounds')
    if selector==65:raise ValueError('Obsolete selector65')
    if selector==77 and (len(scalars),len(data),output_size)!=(1,0,512):raise ValueError('Completion observation shape')
    if selector==69:
        import uploaded_request
        uploaded_request.decode(catalog,data)
    if selector==70 and not 1<=scalars[0]<2**64:raise ValueError('Program serial')
    if selector==71:
        serial,part,offset,length=scalars
        if part>=7 or (part<5 and not serial) or (part>=5 and serial):raise ValueError('Program evidence serial/part')
        maximum=(12288,45056,36864,2112,4096,512,4096)[part]
        if offset>=maximum or length>maximum-offset:raise ValueError('Program evidence range')

import struct
from float_workload import POOL
def pool():
 ties=(1.5,-1.5,2.5,-2.5,3.5,-3.5,0.4999999701976776,-0.4999999701976776,4.5,-4.5,5.5,-5.5,6.5,-6.5,8388607.5,-8388607.5)
 values=tuple(POOL)+tuple(struct.unpack('<I',struct.pack('<f',x))[0] for x in ties)
 assert len(values)==64 and len(set(values))==64
 return values

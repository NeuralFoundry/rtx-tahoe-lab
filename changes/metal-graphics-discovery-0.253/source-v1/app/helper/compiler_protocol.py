"""Bounded AIR-only Unix protocol; no caller-selected executable or file paths."""
import base64,ctypes,hashlib,json,re,socket,struct,sys

MAX_REQUEST=1_450_000
MAX_RESPONSE=65_536
TARGET=b'air64_v28-apple-macosx26.6.0'
sha=lambda b:hashlib.sha256(b).hexdigest()

def pairs(items):
    result={}
    for key,value in items:
        if key in result:raise ValueError('Duplicate compiler field')
        result[key]=value
    return result

def json_data(raw,limit):
    if type(raw) is not bytes or not 0<len(raw)<=limit:raise ValueError('Compiler JSON extent')
    return json.loads(raw,object_pairs_hook=pairs)

def air_request(row):
    if type(row) is not dict or set(row)!={'version','entry','air','air_sha256'} or type(row['version']) is not int or row['version']!=1:raise ValueError('AIR request schema')
    entry=row['entry']
    if type(entry) is not str or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]{0,126}',entry):raise ValueError('AIR entry')
    if type(row['air']) is not str or len(row['air'])>1_398_104:raise ValueError('AIR encoding extent')
    air=base64.b64decode(row['air'],validate=True)
    if not 24<=len(air)<=1_048_576 or row['air_sha256']!=sha(air):raise ValueError('AIR extent or digest')
    magic,version,offset,size,cpu=struct.unpack_from('<5I',air)
    if (magic,version,offset,cpu)!=(0xb17c0de,0,20,0xffffffff) or not 4<=size<=len(air)-20 or not 0<=len(air)-20-size<16 or any(air[20+size:]) or air[20:24]!=b'BC\xc0\xde':raise ValueError('AIR wrapper')
    if re.findall(rb'air64_v[0-9]+-apple-macosx[0-9.]+',air)!=[TARGET]:raise ValueError('AIR target')
    return entry,air

def peer_uid(connection):
    if sys.platform=='darwin':
        library=ctypes.CDLL(None,use_errno=True);fn=library.getpeereid
        fn.argtypes=[ctypes.c_int,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.c_uint)];fn.restype=ctypes.c_int
        uid,gid=ctypes.c_uint(),ctypes.c_uint()
        if fn(connection.fileno(),ctypes.byref(uid),ctypes.byref(gid))!=0:raise OSError(ctypes.get_errno(),'getpeereid')
        return uid.value
    raise OSError('Native compiler peer credentials require macOS')

def receive(connection,maximum):
    def read(n):
        data=bytearray()
        while len(data)<n:
            part=connection.recv(n-len(data))
            if not part:raise ValueError('Truncated compiler frame')
            data.extend(part)
        return bytes(data)
    n=struct.unpack('!I',read(4))[0]
    if not 0<n<=maximum:raise ValueError('Compiler frame extent')
    return json_data(read(n),maximum)

def send(connection,value,maximum):
    raw=json.dumps(value,sort_keys=True,separators=(',',':')).encode()
    if not 0<len(raw)<=maximum:raise ValueError('Compiler response extent')
    connection.sendall(struct.pack('!I',len(raw))+raw)


def peer_identity154(connection):
    # Apple's sys/un.h: SOL_LOCAL=0, LOCAL_PEERPID=2. This identifies the
    # connected helper process, not necessarily the parent Metal application.
    if sys.platform!='darwin':raise OSError('Compiler peer identity requires macOS')
    uid=peer_uid(connection);raw=connection.getsockopt(0,2,4)
    if len(raw)!=4:raise ValueError('Compiler peer PID extent')
    pid=struct.unpack('=i',raw)[0]
    if pid<=0:raise ValueError('Compiler peer PID unavailable')
    return dict(uid=uid,pid=pid)

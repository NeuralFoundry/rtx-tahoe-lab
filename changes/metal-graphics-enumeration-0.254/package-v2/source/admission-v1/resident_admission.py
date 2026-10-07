"""Admit immutable native code from explicitly reviewed compiler transactions.

The owner supplies trusted manifest/review identities, never the XPC peer. This
registry adds no GPU access. Its callback only compares an owned payload with
compiler artifacts already verified by runtime_admission.load.
"""
import collections,ctypes,hashlib,json,os,re,threading
from types import MappingProxyType
import runtime_admission

def digest(data):return hashlib.sha256(data).hexdigest()
def valid_sha(value):return type(value) is str and re.fullmatch('[0-9a-f]{64}',value) and value!='0'*64

class Registry:
    def __init__(self,approved_manifests,max_entries=64,compiler_review_sha256=None):
        if compiler_review_sha256 is not None and not valid_sha(compiler_review_sha256):raise ValueError('Compiler implementation review identity')
        if type(approved_manifests) is not dict or (not approved_manifests and compiler_review_sha256 is None) or len(approved_manifests)>256 or any(not valid_sha(k) or not valid_sha(v) for k,v in approved_manifests.items()):raise ValueError('Reviewed manifest/review identities required')
        if type(max_entries) is not int or not 1<=max_entries<=256:raise ValueError('Registry bound')
        self._approved=MappingProxyType(dict(approved_manifests));self._compiler_review=compiler_review_sha256;self._capacity=max_entries;self._entries={};self._installed={};self._pid=os.getpid();self._closed=False;self._lock=threading.RLock()
    def _process(self):
        if os.getpid()!=self._pid:raise RuntimeError('Admission owner process changed')
    def install(self,directory,manifest_sha256):
        self._process()
        with self._lock:
            if self._closed:raise RuntimeError('Admission registry closed')
            if manifest_sha256 not in self._approved:raise ValueError('Compiler transaction was not approved by the owner')
            if manifest_sha256 in self._installed:return json.loads(self._installed[manifest_sha256])
            if len(self._installed)>=self._capacity:raise ValueError('Admission registry capacity')
            catalog,container,receipt=runtime_admission.load(directory,manifest_sha256)
            if receipt['compiler_review_sha256']!=self._approved[manifest_sha256]:raise ValueError('Compiler review does not match owner receipt')
            payload=bytes(catalog.library+catalog.code)
            if len(payload)!=4608 or type(container) is not bytes or len(container)!=5248 or container[640:]!=payload or digest(payload)!=receipt['payload_sha256']:raise ValueError('Native payload binding')
            key=digest(payload);entry=self._entries.get(key)
            if entry is not None and entry[0]!=payload:raise ValueError('Ambiguous payload identity')
            receipt=dict(receipt,runtime_manifest_sha256=manifest_sha256,admitted_bytes=4608)
            owned=json.dumps(receipt,sort_keys=True).encode()
            self._entries[key]=(payload,owned)
            self._installed[manifest_sha256]=owned
            return json.loads(owned)
    def admit(self,payload):
        self._process()
        if type(payload) is not bytes or len(payload)!=4608:return None
        with self._lock:
            if self._closed:return None
            found=self._entries.get(digest(payload))
            if found is None or found[0]!=payload:return None
            return json.loads(found[1])
    def install_compiled(self,directory,manifest_sha256):
        """Trusted owner API, called after its own compiler transaction.

        No wire protocol exposes this directory/hash operation. The local
        compiler service accepts AIR only, constructs both values itself,
        checks its pinned compiler result, then invokes this method.
        """
        self._process()
        if not hasattr(os,'geteuid') or os.geteuid()!=0:raise PermissionError('Completed compiler admission requires root owner')
        with self._lock:
            if self._closed:raise RuntimeError('Admission registry closed')
            if self._compiler_review is None:raise ValueError('Runtime compiler admission is not configured')
            if not valid_sha(manifest_sha256):raise ValueError('Compiler transaction identity')
            if manifest_sha256 in self._installed:return json.loads(self._installed[manifest_sha256])
            if len(self._installed)>=self._capacity:raise ValueError('Admission registry capacity')
            catalog,container,receipt=runtime_admission.load(directory,manifest_sha256)
            if receipt['compiler_review_sha256']!=self._compiler_review:raise ValueError('Compiler implementation review mismatch')
            payload=bytes(catalog.library+catalog.code)
            if len(payload)!=4608 or type(container) is not bytes or len(container)!=5248 or container[640:]!=payload or digest(payload)!=receipt['payload_sha256']:raise ValueError('Native payload binding')
            key=digest(payload);entry=self._entries.get(key)
            if entry is not None and entry[0]!=payload:raise ValueError('Ambiguous payload identity')
            receipt=dict(receipt,runtime_manifest_sha256=manifest_sha256,admitted_bytes=4608,admission_origin='owner_runtime_compiler')
            owned=json.dumps(receipt,sort_keys=True).encode()
            self._entries[key]=(payload,owned);self._installed[manifest_sha256]=owned
            return json.loads(owned)
    def snapshot(self):
        self._process()
        with self._lock:return dict(closed=self._closed,registered_transactions=len(self._installed),unique_payloads=len(self._entries),receipts=[json.loads(v) for _,v in sorted(self._installed.items())])
    def close(self):
        self._process()
        with self._lock:self._closed=True

Callback=ctypes.CFUNCTYPE(ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t)

class RootCallback:
    """Keep this object alive until the real server has drained all callbacks."""
    def __init__(self,registry):
        if not hasattr(os,'geteuid') or os.geteuid()!=0:raise PermissionError('Root compiler admission requires the owning root process')
        if type(registry) is not Registry:raise TypeError('Registry')
        registry._process();self._registry=registry;self._pid=os.getpid();self._context=ctypes.c_uint64(0x52545841444d3630);self.context=ctypes.c_void_p(ctypes.addressof(self._context));self._lock=threading.RLock();self._retired=False;self._calls=0;self._dropped=0;self._records=collections.deque(maxlen=512);self.function=Callback(self._invoke)
    def _invoke(self,context,payload,size):
        # Exceptions cannot cross the C ABI. Denial is returned before copying
        # unless the actual native caller supplied the exact owner context/size.
        with self._lock:
            status=1;receipt=None;identity=None;error=None
            try:
                if os.getpid()!=self._pid or os.geteuid()!=0 or self._retired or context!=self.context.value or not payload or size!=4608:raise ValueError('Admission callback identity or extent')
                owned=ctypes.string_at(payload,4608);identity=digest(owned);receipt=self._registry.admit(owned);status=0 if receipt is not None else 1
            except BaseException as ex:status=2;error=type(ex).__name__
            self._calls+=1
            if len(self._records)==self._records.maxlen:self._dropped+=1
            self._records.append(dict(call=self._calls,status=status,payload_sha256=identity,runtime_manifest_sha256=receipt['runtime_manifest_sha256'] if receipt else None,error=error))
            return status
    def snapshot(self):
        if os.getpid()!=self._pid:raise RuntimeError('Admission callback process changed')
        with self._lock:return dict(process_id=self._pid,retired=self._retired,calls=self._calls,dropped_records=self._dropped,records=list(self._records),registry=self._registry.snapshot())
    def retire(self):
        if os.getpid()!=self._pid:raise RuntimeError('Admission callback process changed')
        with self._lock:self._retired=True

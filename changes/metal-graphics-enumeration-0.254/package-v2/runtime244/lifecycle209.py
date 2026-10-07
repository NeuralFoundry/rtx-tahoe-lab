"""Python callback boundary and owner close guard for the native208 broker.

The caller retains this object, the broker dylib and native owner for the full
serve lifetime. A failed withdrawal/retirement quarantines the owner; neither
this module nor a worker may treat that condition as permission to exit.
"""
import ctypes, os
from ready209 import need

CALLBACK=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32)
class Callbacks(ctypes.Structure):
    _fields_=[('abi',ctypes.c_uint32),('bytes',ctypes.c_uint32),('context',ctypes.c_void_p),('function',CALLBACK),('reserved',ctypes.c_uint64)]
assert ctypes.sizeof(Callbacks)==32

class Lifecycle:
    def __init__(self, activate, withdraw):
        self.pid=os.getpid();self.activate=activate;self.withdraw=withdraw
        self.ready_attempted=False;self.withdraw_attempted=False;self.withdrawn=False
        self.events=[];self.error_types=[]
        self.function=CALLBACK(self._call);self.callbacks=Callbacks(202,32,None,self.function,0)
    def _call(self, context, event):
        try:
            need(os.getpid()==self.pid and not context,'Original callback owner/context')
            # The native serial dispatch queue may use different worker threads.
            self.events.append(event)
            if event==1:
                need(not self.ready_attempted and not self.withdraw_attempted,'Single activation')
                self.ready_attempted=True;self.activate();return 0
            if event==2:
                need(self.ready_attempted and not self.withdraw_attempted,'Single ordered withdrawal')
                self.withdraw_attempted=True;self.withdraw();self.withdrawn=True;return 0
            raise ValueError('Unknown lifecycle event')
        except BaseException as error:
            # ctypes must never swallow an exception and return an undefined int.
            self.error_types.append(type(error).__name__);return 1

class CloseGuard:
    def __init__(self):self.broker=None;self.pid=os.getpid();self.serve_entered=False;self.serve_returned=False;self.returncode=None
    def attach(self, broker):
        need(os.getpid()==self.pid and self.broker is None,'One broker binding')
        f=broker.rtx_owned_broker_close_permitted208;f.argtypes=[];f.restype=ctypes.c_int
        need(f()==1,'Unused broker close permission');self.broker=broker
    def enter(self):
        need(os.getpid()==self.pid and self.broker is not None and not self.serve_entered,'One broker serve')
        # Conservatively block close even if an FFI call never returns.
        self.serve_entered=True
    def returned(self, code):
        need(os.getpid()==self.pid and self.serve_entered and not self.serve_returned and type(code) is int,'One broker return')
        self.serve_returned=True;self.returncode=code
    def permitted(self):
        need(os.getpid()==self.pid,'Original close guard process')
        if self.broker is None:return True
        if self.serve_entered and not self.serve_returned:return False
        if self.returncode==70:return False
        return self.broker.rtx_owned_broker_close_permitted208()==1
    def require_close(self):need(self.permitted(),'Native owner retained: broker has not granted close permission')

def guarded_backend(base_class):
    class PairedBackend(base_class):
        kind='macOS-native-paired-root203'
        def __init__(self,*args,**kwargs):
            self.close_guard=CloseGuard();super().__init__(*args,**kwargs)
        def _current_binding(self,label):
            from machine209 import observe,require_loaded
            value=observe();self._save_json(label+'-machine.json',value);require_loaded(value,self.binding)
        def close(self):
            # Guard outside RestrictedBackend.close: its finally sets closed=True.
            self.close_guard.require_close();return super().close()
    return PairedBackend

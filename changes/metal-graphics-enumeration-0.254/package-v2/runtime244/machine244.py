"""Exact paired registry contract, retaining the native bootstrap API."""
import machine253
from machine253 import observe,fields,COLD_COUNTERS,COLD_FLAGS,KEYS
def require_loaded(value,binding):
 parent,child,memory=machine253.require_loaded(value,binding);return parent,memory
def require_cold(value):return machine253.require_cold(value)
def require_armed(value,binding):
 parent,child,memory=machine253.require_hidden(value,binding)
 fields(parent,{k:True for k in ('OwnedRootAcknowledged','OwnedRootRuntimeEnabled','GSPDmaProviderOpen','GSPProgramReady','GSPHostFencePassed','GSPInitDoneObserved')})
 fields(parent,dict(OwnedDispatchActive=False,OwnedGraphicsActive=False,OwnedGraphicsRetained=False,OwnedGraphicsCompleted=0,GSPProgramCompleted=0));return memory

#include "MacVectorSubmit.hpp"
extern "C" bool vector_submit(MacVectorSubmit &io,const VectorMemory::Storage &s,const VectorMemory::Result &m,const ExecutionTransactions::Result &e,const HostFence::Result &h,VectorSubmit::Result &r){return VectorSubmit::execute(io,s,m,e,h,r);}

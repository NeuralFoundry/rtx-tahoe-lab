#include "MacBatchSubmit.hpp"
bool batch_submit(MacBatchSubmit &io,unsigned j,const BatchMemory::Storage &s,const BatchMemory::Result &m,
 const ExecutionTransactions::Result &e,const HostFence::Result &h,BatchSubmit::Result &r){
 return BatchSubmit::execute(io,j,s,m,e,h,r)&&io.finish(r);
}

#include "MacComputeSubmit.hpp"
extern "C" bool compute_submit(MacComputeSubmit &io,const ComputeMemory::Storage &storage,const ComputeMemory::Result &memory,
 const ExecutionTransactions::Result &execution,const HostFence::Result &host,ComputeSubmit::Result &result){
 return ComputeSubmit::execute(io,storage,memory,execution,host,result);
}

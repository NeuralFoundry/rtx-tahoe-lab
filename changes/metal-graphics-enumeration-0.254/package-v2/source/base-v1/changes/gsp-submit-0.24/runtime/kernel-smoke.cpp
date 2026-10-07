#include "MacExecutionTransactions.hpp"
extern "C" bool execution_runtime(MacExecutionTransactions &io,const GSPComputePrep::Result &prep,const unsigned char *prepBytes,
 const GSPComputePrep::Result &pd,const unsigned char *pdBytes,const ChannelTransactions::Result &golden,const unsigned char *goldenBytes,
 unsigned char *out,unsigned char *requests,unsigned char *scratch,ExecutionTransactions::Result &result,unsigned char *externalRecords,unsigned char *externalRequests){
  return ExecutionTransactions::execute(io,prep,prepBytes,pd,pdBytes,golden,goldenBytes,out,requests,scratch,result,externalRecords,externalRequests);
}

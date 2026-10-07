#include "MacHostFence.hpp"
extern "C" bool host_fence(MacHostFence &io,const ChannelCodec::Plan &golden,const ExecutionTransactions::Result &execution,
 const unsigned char *requests,const unsigned char *records,unsigned char *scratch,HostFence::Result &result){
  return HostFence::execute(io,golden,execution,requests,records,scratch,result);
}

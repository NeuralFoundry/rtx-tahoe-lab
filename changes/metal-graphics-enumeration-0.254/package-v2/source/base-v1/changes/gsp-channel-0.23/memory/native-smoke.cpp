#include <IOKit/IOUserClient.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include "../../../driver/MacGSPIdentity.hpp"
#include "../../../driver/MacGSPRuntimeDma.hpp"
#include "../../../driver/RuntimeDmaProtocol.hpp"
#include "../../../driver/MacGSPContext.hpp"
#include "../../../driver/GSPExecutionOwner.hpp"
#include "MacChannelMemory.hpp"

// Compile-only instantiation of the actual Kernel SDK adapter and templates.
// There is deliberately no kext entry point, userclient or executable harness.
extern "C" bool rtx_channel_mapping_compile(MacChannelMemoryMapping *mapping){return mapping->map();}
extern "C" bool rtx_channel_ring_compile(MacChannelMemory *memory){return memory->stageRing();}
extern "C" bool rtx_channel_context_compile(MacChannelMemory *memory,const ChannelCodec::Plan *plan){return memory->stageContexts(*plan);}
extern "C" void rtx_channel_restore_compile(MacChannelMemory *memory){memory->restoreWindow();}
extern "C" void rtx_channel_transactions_compile(MacChannelTransactions *io,const GSPComputePrep::Result *prep,const unsigned char *prepBytes,
  const GSPComputePrep::Result *pd,const unsigned char *pdBytes,unsigned char *out,unsigned char *requests,unsigned char *scratch,ChannelTransactions::Result *result){
  ChannelTransactions::execute(*io,*prep,prepBytes,*pd,pdBytes,out,requests,scratch,*result);
}

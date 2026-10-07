#include "MacApplicationRuntime.hpp"
bool application_memory(MacApplicationMemory &io,const ChannelCodec::Plan &golden){return io.stage(golden);}
bool application_prepare(MacApplicationRuntime &io,MacApplicationMemory &memory,uint64_t gen,uint64_t client){return io.prepare(memory,gen,client);}
bool application_open(MacApplicationRuntime &io){return io.open();}
RtxApplication032::Error application_submit(MacApplicationRuntime &io,uint64_t caller,const unsigned char *p,size_t n){return io.submit(caller,p,n);}
void application_close(MacApplicationRuntime &io,uint64_t caller){io.close(caller);}

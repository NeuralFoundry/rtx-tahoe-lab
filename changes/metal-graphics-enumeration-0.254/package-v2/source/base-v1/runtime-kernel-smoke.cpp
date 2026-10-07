#include "MacReusableRuntime.hpp"
extern "C" bool rtx_reusable_prepare035(MacReusableRuntime035 *runtime,uint64_t generation,uint64_t client,const RtxReusableRuntime035::Storage *storage){return runtime&&storage&&runtime->prepare(generation,client,*storage);}
extern "C" bool rtx_reusable_open035(MacReusableRuntime035 *runtime){return runtime&&runtime->open();}
extern "C" unsigned rtx_reusable_submit035(MacReusableRuntime035 *runtime,uint64_t client,const uint8_t *wire,size_t bytes){return unsigned(runtime->submit(client,wire,bytes));}
extern "C" bool rtx_reusable_close035(MacReusableRuntime035 *runtime,uint64_t client){return runtime&&runtime->close(client);}

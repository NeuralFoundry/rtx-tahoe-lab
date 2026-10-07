#include "MacProgramRuntime.hpp"
extern "C" bool rtx_program_runtime_prepare(MacProgramRuntime *r,MacProgramMemory *m,uint64_t g,uint64_t client){return r&&m&&r->prepare(*m,g,client);}
extern "C" bool rtx_program_runtime_open(MacProgramRuntime *r){return r&&r->open();}
extern "C" unsigned rtx_program_runtime_submit(MacProgramRuntime *r,uint64_t caller,const uint8_t *wire,size_t bytes){
 return r?unsigned(r->submit(caller,wire,bytes)):unsigned(RtxProgramRuntime033::Error::State);
}
extern "C" bool rtx_program_runtime_close(MacProgramRuntime *r,uint64_t caller){return r&&r->close(caller);}

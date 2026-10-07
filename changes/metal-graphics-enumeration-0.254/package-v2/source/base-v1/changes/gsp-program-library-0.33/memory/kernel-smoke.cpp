#include "MacProgramMemory.hpp"
extern "C" bool rtx_program_memory(MacProgramMemory *io,const ChannelCodec::Plan *golden){return io&&golden&&io->stage(*golden);}

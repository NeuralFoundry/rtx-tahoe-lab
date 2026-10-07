#include "MacComputeMemory.hpp"
extern "C" bool compute_memory(MacComputeMemory &io,const ChannelCodec::Plan &golden){return io.stage(golden);}

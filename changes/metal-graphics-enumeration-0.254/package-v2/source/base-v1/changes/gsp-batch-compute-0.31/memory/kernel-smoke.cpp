#include "MacBatchMemory.hpp"
extern "C" bool batch_memory(MacBatchMemory &io,const ChannelCodec::Plan &golden){return io.stage(golden);}

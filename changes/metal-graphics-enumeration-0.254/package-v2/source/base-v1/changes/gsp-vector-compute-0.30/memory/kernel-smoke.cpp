#include "MacVectorMemory.hpp"
extern "C" bool vector_memory(MacVectorMemory &io,const ChannelCodec::Plan &golden){return io.stage(golden);}

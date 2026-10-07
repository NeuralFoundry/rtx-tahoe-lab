#include "probe/kernel/gpu183/OwnedDispatch183.hpp"
extern "C" uint64_t select_program205(RTXOwnedDispatch183::State*state,uint64_t generation,uint64_t completed,uint64_t revision,const void*input,size_t n){return uint64_t(state->select205(generation,completed,revision,input,n));}
extern "C" void program_info205(const RTXOwnedDispatch183::State*state,uint64_t generation,uint64_t*out){state->programInfo205(generation,out);}

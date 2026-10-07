#include "ProgramLibrary.hpp"
extern "C" bool rtx_program_profile(const uint8_t *wire,const uint8_t *code,const RtxProgram033::Dispatch *dispatch,
 uint8_t *qmd,uint8_t *constant,uint8_t *command,RtxProgram033::Launch *launch){
 return dispatch&&launch&&RtxProgram033::build(wire,512,code,4096,*dispatch,qmd,256,constant,4096,command,32,*launch);
}

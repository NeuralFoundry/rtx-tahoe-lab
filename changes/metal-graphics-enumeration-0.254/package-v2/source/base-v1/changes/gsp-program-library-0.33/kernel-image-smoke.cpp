#include "ProgramImage.hpp"
#include "ProgramSession.hpp"
extern "C" bool rtx_program_image(const uint8_t *wire,const uint8_t *code,uint8_t *image){return RtxProgramImage033::initial(wire,512,code,4096,image,24576);}
extern "C" bool rtx_program_plan(const uint8_t *wire,const uint8_t *code,const RtxProgramRequest033::Request *request,uint8_t *scratch,RtxProgramImage033::Plan *out){
 return request&&out&&RtxProgramImage033::plan(wire,512,code,4096,*request,scratch,4096,*out);
}
extern "C" unsigned rtx_program_accept(RtxProgramSession033::Session *s,uint64_t caller,const uint8_t *request,const RtxProgram033::Library *lib){
 return s&&lib?unsigned(s->accept(caller,request,2112,*lib)):99;
}
extern "C" bool rtx_program_capture(const uint8_t *actual,const uint8_t *canonical,const RtxProgram033::Library *lib,const RtxProgramRequest033::Request *history,unsigned staged,unsigned completed){
 return lib&&RtxProgramImage033::capture(actual,24576,canonical,24576,*lib,history,staged,completed);
}

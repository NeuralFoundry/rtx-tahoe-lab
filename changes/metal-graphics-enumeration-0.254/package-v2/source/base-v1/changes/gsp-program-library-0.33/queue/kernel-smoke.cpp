#include "MacProgramSubmit.hpp"
#include "MacProgramCapture.hpp"
extern "C" bool rtx_program_submit(MacProgramSubmit *io){return io&&io->submit();}
extern "C" bool rtx_program_capture(MacProgramCapture *io){return io&&io->capture();}
extern "C" bool rtx_program_capture_verify(const ProgramCapture::Storage *c,const ProgramSubmit::Storage *s,
 const HostFence::Result *host,unsigned staged,unsigned completed){
 return c&&s&&host&&ProgramCapture::verifyFull(c->result,c->root,c->children,c->device,*s,*host,staged,completed);
}

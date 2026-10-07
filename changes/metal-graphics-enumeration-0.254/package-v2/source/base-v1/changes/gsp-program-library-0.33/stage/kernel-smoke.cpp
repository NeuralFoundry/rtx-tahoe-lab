#include "MacProgramStage.hpp"
extern "C" bool rtx_program_stage(MacProgramStage *io){return io&&io->stage();}

#include "MacExecutionMemory.hpp"
extern "C" bool execution_memory_fixed(MacExecutionMemory &io){return io.stageFixed();}
extern "C" bool execution_memory_context(MacExecutionMemory &io,const ExecutionPlan::Plan &plan){return io.stageContexts(plan);}
extern "C" void execution_memory_cleanup(MacExecutionMemory &io){io.restoreWindow();}

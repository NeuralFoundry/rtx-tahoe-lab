#include "ExecutionTables.hpp"
// Compile-only entry points keep new serializers visible to -mkernel and stack
// analysis. Caller-owned scratch remains outside the kernel stack.
extern "C" bool execution_layout_make(const unsigned char *gr,unsigned size,const ChannelCodec::Plan &golden,ExecutionPlan::Plan &out){
  return ExecutionPlan::make(gr,size,golden,out);
}
extern "C" bool execution_layout_merge(const unsigned char *root,unsigned rootBytes,const unsigned char *child,unsigned childBytes,
 const ChannelCodec::Plan &golden,const ExecutionPlan::Plan &plan,unsigned char *scratch,unsigned char *expected,unsigned char *out,
 unsigned capacity,ExecutionTables::Result &result){
  return ExecutionTables::merge(root,rootBytes,child,childBytes,golden,plan,scratch,expected,out,capacity,result);
}

#include "ExecutionTranscript.hpp"
extern "C" bool execution_protocol_request(unsigned step,unsigned sequence,const ChannelCodec::Plan &golden,unsigned cid,const ExecutionPlan::Plan &plan,unsigned char *out,unsigned capacity){
  return ExecutionCodec::request(step,sequence,golden,cid,plan,out,capacity);
}
extern "C" bool execution_protocol_reply(const unsigned char *bytes,unsigned count,unsigned sequence,unsigned step,const ChannelCodec::Plan &golden,unsigned cid,const ExecutionPlan::Plan &plan,ExecutionCodec::Reply &out){
  return ExecutionCodec::reply(bytes,count,sequence,step,golden,cid,plan,out);
}
extern "C" bool execution_protocol_transcript(const unsigned char *requests,unsigned requestBytes,const unsigned char *records,unsigned recordBytes,unsigned sequence,const ChannelCodec::Plan &golden,unsigned cid,unsigned char *scratch,ExecutionTranscript::Result &out){
  return ExecutionTranscript::verify(requests,requestBytes,records,recordBytes,sequence,golden,cid,scratch,out);
}

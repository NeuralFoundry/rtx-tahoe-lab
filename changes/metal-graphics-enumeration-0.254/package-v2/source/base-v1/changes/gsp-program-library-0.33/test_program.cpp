#include "ProgramLibrary.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
namespace P=RtxProgram033;
static unsigned checks=0,rejected=0,plans=0;
static void must(bool b){++checks;if(!b){fprintf(stderr,"check %u failed\n",checks);exit(1);}}
static std::vector<uint8_t> load(const std::string &name,unsigned bytes){
 FILE *f=fopen(name.c_str(),"rb");must(f!=nullptr);std::vector<uint8_t> v(bytes);must(fread(v.data(),1,bytes,f)==bytes);must(fgetc(f)==EOF);must(fclose(f)==0);return v;
}
static void save(const std::string &name,const uint8_t *data,size_t bytes){FILE *f=fopen(name.c_str(),"wb");must(f!=nullptr);must(fwrite(data,1,bytes,f)==bytes);must(fclose(f)==0);}
static void encodeLaunch(const P::Launch &p,uint8_t *out){
 memset(out,0,256);const unsigned words[]={p.program,p.slot,p.invocations,p.codeOffset,p.codeBytes,p.registers,p.parameters};
 for(unsigned i=0;i<7;++i)P::Q::put32(out+i*4,words[i]);
 const uint64_t addresses[]={p.programVA,p.constantVA,p.qmdVA,p.fenceVA,p.entry};
 for(unsigned i=0;i<5;++i)P::Q::put64(out+32+i*8,addresses[i]);
 for(unsigned i=0;i<P::MaxBindings;++i)P::Q::put64(out+72+i*8,p.buffers[i]);
}
static void reject(const std::vector<uint8_t> &wire,const std::vector<uint8_t> &code,const P::Dispatch &d){
 std::vector<uint8_t> packet(4480,0xa5);P::Launch launch;memset(&launch,0x5a,sizeof(launch));
 unsigned char before[sizeof(launch)];memcpy(before,&launch,sizeof(launch));
 must(!P::build(wire.data(),wire.size(),code.data(),code.size(),d,packet.data(),256,packet.data()+256,4096,packet.data()+4352,32,launch));
 must(memcmp(before,&launch,sizeof(launch))==0);for(uint8_t b:packet)must(b==0xa5);++rejected;
}
int main(int argc,char **argv){
 must(argc==3);const std::string input=argv[1],output=argv[2];
 const auto wire=load(input+"/library.bin",512),code=load(input+"/code.bin",4096);
 P::Library library;must(P::decode(wire.data(),wire.size(),code.data(),code.size(),library));
 must(library.count==3&&library.usedCodeBytes==2304);
 std::vector<uint8_t> packet(4640);
 for(unsigned p=0;p<library.count;++p)for(unsigned slot=0;slot<4;++slot){
  P::Launch launch;const P::Dispatch d={p,slot,1};
  must(P::build(wire.data(),wire.size(),code.data(),code.size(),d,packet.data(),256,packet.data()+256,4096,packet.data()+4352,32,launch));
  must(launch.programVA==P::Q::ProgramVA+library.programs[p].offset&&launch.parameters==3&&launch.invocations==64);
  for(unsigned n=0;n<launch.parameters;++n){
   must(launch.buffers[n]==P::bufferVA(slot,n));
   for(unsigned prev=0;prev<slot;++prev)for(unsigned j=0;j<P::MaxBindings;++j)
    must(launch.buffers[n]+256<=P::bufferVA(prev,j)||P::bufferVA(prev,j)+256<=launch.buffers[n]);
  }
  encodeLaunch(launch,packet.data()+4384);
  save(output+"/plan-"+std::to_string(p)+"-"+std::to_string(slot)+".bin",packet.data(),packet.size());++plans;
 }
 const struct Change{unsigned offset,value;} changes[]={
  {8,2},{12,511},{16,0},{16,5},{20,4096},{24,0x89},{28,1},{64,0},{68,128},{72,0},{72,0xffffffff},
  {76,0},{76,256},{80,0},{80,1025},{84,2},{88,2},{92,0},{92,9},{96,0x80000000},{100,0},{104,375},
  {108,1},{112,32},{116,0},{124,1},{144,1},{400,1}};
 for(const auto &c:changes){auto bad=wire;P::Q::put32(bad.data()+c.offset,c.value);reject(bad,code,{0,0,1});}
 {auto bad=code;bad[640]=1;reject(wire,bad,{0,0,1});}
 {auto bad=code;bad[3000]=1;reject(wire,bad,{0,0,1});}
 {auto bad=code;memset(bad.data(),0,640);reject(wire,bad,{0,0,1});}
 for(const P::Dispatch &d:std::vector<P::Dispatch>{{3,0,1},{0,4,1},{0,0,0},{0,0,2},{0,0,0xffffffff}})reject(wire,code,d);
 {auto bad=wire;bad.pop_back();reject(bad,code,{0,0,1});}
 {auto bad=code;bad.pop_back();reject(wire,bad,{0,0,1});}
 // Inputs and outputs must remain disjoint even when the container is valid.
 {auto mutableWire=wire;P::Launch launch;auto original=mutableWire;
  must(!P::build(mutableWire.data(),512,code.data(),4096,{0,0,1},mutableWire.data(),256,packet.data(),4096,packet.data()+4096,32,launch));
  must(mutableWire==original);++rejected;}
 // Failure leaves a previously initialized decoded object untouched.
 {auto bad=wire;bad[0]^=1;P::Library copy=library;unsigned char before[sizeof(copy)];memcpy(before,&copy,sizeof(copy));
  must(!P::decode(bad.data(),512,code.data(),4096,copy));must(memcmp(before,&copy,sizeof(copy))==0);++rejected;}
 printf("{\"passed\":true,\"checks\":%u,\"rejected\":%u,\"plans\":%u,\"gpu_commands_submitted\":false}\n",checks,rejected,plans);
 return 0;
}

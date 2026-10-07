#include "ProgramImage.hpp"
#include "ProgramSession.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <memory>
namespace P=RtxProgram033;namespace R=RtxProgramRequest033;namespace I=RtxProgramImage033;namespace S=RtxProgramSession033;
static unsigned checks=0,rejections=0;
static void must(bool b){++checks;if(!b){fprintf(stderr,"image check %u failed\n",checks);exit(1);}}
static std::vector<uint8_t> load(const std::string &name,unsigned bytes){FILE *f=fopen(name.c_str(),"rb");must(f!=nullptr);std::vector<uint8_t> b(bytes);must(fread(b.data(),1,bytes,f)==bytes);must(fgetc(f)==EOF);must(fclose(f)==0);return b;}
static void save(const std::string &name,const uint8_t *b,size_t bytes){FILE *f=fopen(name.c_str(),"wb");must(f!=nullptr);must(fwrite(b,1,bytes,f)==bytes);must(fclose(f)==0);}
static S::Bootstrap bootstrap(){return {0x30603301,0x73003301,true,true,true,true,true,true};}
static S::Completion completion(unsigned slot){return {0x30603301,uint64_t(slot)+1,slot+2,slot+2,P::completion(slot),true,true,true};}
static uint32_t operation(unsigned program,uint32_t a,uint32_t b){return program==0?a+b:program==1?a*b:a^b;}
static void corruptReject(const std::vector<uint8_t> &actual,const std::vector<uint8_t> &canonical,const P::Library &lib,
 const R::Request *history,unsigned count,unsigned offset){auto bad=actual;bad[offset]^=1;must(!I::capture(bad.data(),bad.size(),canonical.data(),canonical.size(),lib,history,count,count));++rejections;}
int main(int argc,char **argv){
 must(argc==2);const std::string out=argv[1];const auto wire=load("fixtures/library.bin",512),code=load("fixtures/code.bin",4096);
 P::Library lib;must(P::decode(wire.data(),wire.size(),code.data(),code.size(),lib));
 std::vector<uint8_t> canonical(I::ImageBytes),actual(I::ImageBytes),scratch(4096),planWire(4096);
 std::vector<R::Request> history(4);auto plan=std::unique_ptr<I::Plan>(new I::Plan);auto session=std::unique_ptr<S::Session>(new S::Session);
 must(I::initial(wire.data(),wire.size(),code.data(),code.size(),canonical.data(),canonical.size()));actual=canonical;
 save(out+"/initial.bin",canonical.data(),canonical.size());
 must(I::capture(actual.data(),actual.size(),canonical.data(),canonical.size(),lib,history.data(),0,0));
 auto evidence=bootstrap();must(session->open(evidence));must(!session->open(evidence));++rejections;
 for(unsigned slot=0;slot<4;++slot){
  auto request=load("image-fixtures/request-"+std::to_string(slot)+".bin",R::WireBytes);const auto original=request;
  must(session->accept(evidence.client+1,request.data(),request.size(),lib)==S::Error::Identity);++rejections;
  must(session->accept(evidence.client,request.data(),request.size(),lib)==S::Error::Ok);
  request[64]^=1; // External bytes may change after the service snapshot.
  must(session->active().data[0]==original[64]);request=original;
  must(session->accept(evidence.client,request.data(),request.size(),lib)==S::Error::State);++rejections;
  history[slot]=session->active();std::vector<uint8_t> encoded(R::WireBytes);
  must(R::encode(history[slot],lib,encoded.data(),encoded.size()));must(encoded==original);
  save(out+"/request-"+std::to_string(slot)+".bin",encoded.data(),encoded.size());
  must(I::plan(wire.data(),wire.size(),code.data(),code.size(),session->active(),scratch.data(),scratch.size(),*plan));
  must(I::encodePlan(*plan,planWire.data(),planWire.size()));save(out+"/plan-"+std::to_string(slot)+".bin",planWire.data(),planWire.size());
  must(session->beginMutation(evidence.client));
  // These copies simulate staging and readback; no native I/O adapter is used.
  must(I::commit(*plan,canonical.data(),canonical.size()));
  for(unsigned i=0;i<2048;++i)actual[I::dataOffset(slot)+i]=plan->data[i];
  for(unsigned i=0;i<1024;++i)actual[I::constantOffset(slot)+i]=plan->constant[i];
  for(unsigned i=0;i<256;++i)actual[I::qmdOffset(slot)+i]=plan->qmd[i];
  save(out+"/canonical-"+std::to_string(slot)+".bin",canonical.data(),canonical.size());
  must(I::capture(actual.data(),actual.size(),canonical.data(),canonical.size(),lib,history.data(),slot+1,slot));
  must(session->submitted(evidence.client));
  const auto &r=history[slot];
  for(unsigned n=0;n<64;++n)P::Q::put32(actual.data()+I::dataOffset(slot)+512+n*4,operation(r.program,P::get32(r.data+n*4),P::get32(r.data+256+n*4)));
  P::Q::put32(actual.data()+I::fenceOffset(slot),P::completion(slot));
  must(I::capture(actual.data(),actual.size(),canonical.data(),canonical.size(),lib,history.data(),slot+1,slot+1));
  auto done=completion(slot);must(!session->finish(evidence.client+1,done));must(session->phase()==S::Phase::Submitted);++rejections;
  must(session->finish(evidence.client,done));must(session->completed()==slot+1);
  save(out+"/simulated-"+std::to_string(slot)+".bin",actual.data(),actual.size());
  for(unsigned offset:std::vector<unsigned>{0,I::constantOffset(slot)+0x160,I::dataOffset(slot),I::dataOffset(slot)+256,I::dataOffset(slot)+768,I::fenceOffset(slot)+4})
   corruptReject(actual,canonical,lib,history.data(),slot+1,offset);
  if(slot<3)corruptReject(actual,canonical,lib,history.data(),slot+1,I::qmdOffset(slot+1));
 }
 must(session->phase()==S::Phase::Exhausted);
 // Semantic errors within writable output are the application's responsibility.
 auto wrong=actual;wrong[I::dataOffset(3)+512]^=1;
 must(I::capture(wrong.data(),wrong.size(),canonical.data(),canonical.size(),lib,history.data(),4,4));
 save(out+"/semantic-error-accepted-by-guard.bin",wrong.data(),wrong.size());
 auto qmdChanged=actual;qmdChanged[I::qmdOffset(0)]^=1;must(I::capture(qmdChanged.data(),qmdChanged.size(),canonical.data(),canonical.size(),lib,history.data(),4,4));
 const auto first=load("image-fixtures/request-0.bin",R::WireBytes);
 const struct Change{unsigned offset,value;} changes[]={{0,0},{8,2},{12,2111},{16,0},{20,0},{24,0},{24,5},{32,3},{36,0},{36,2},{40,1},{64+768,1}};
 for(const auto &c:changes){auto bad=first;P::Q::put32(bad.data()+c.offset,c.value);if(bad==first)continue;
  R::Request request;unsigned char before[sizeof(request)];memcpy(before,&request,sizeof(request));
  must(!R::decode(bad.data(),bad.size(),lib,request));must(memcmp(before,&request,sizeof(request))==0);++rejections;
 }
 {P::Library bad=lib;bad.count=5;R::Request r;must(!R::decode(first.data(),first.size(),bad,r));++rejections;}
 for(unsigned field=0;field<8;++field){auto s=std::unique_ptr<S::Session>(new S::Session);must(s->open(evidence));
  must(s->accept(evidence.client,first.data(),first.size(),lib)==S::Error::Ok);must(s->beginMutation(evidence.client));must(s->submitted(evidence.client));
  auto done=completion(0);switch(field){case 0:++done.generation;break;case 1:++done.id;break;case 2:++done.get;break;case 3:++done.put;break;case 4:++done.marker;break;case 5:done.owner=false;break;case 6:done.capture=false;break;default:done.windowRestored=false;break;}
  must(!s->finish(evidence.client,done));must(s->phase()==S::Phase::Retained);must(s->accept(evidence.client,first.data(),first.size(),lib)==S::Error::State);++rejections;
 }
 {auto s=std::unique_ptr<S::Session>(new S::Session);must(s->open(evidence));auto bad=first;P::Q::put64(bad.data()+16,evidence.generation+1);
  must(s->accept(evidence.client,bad.data(),bad.size(),lib)==S::Error::Identity);bad=first;P::Q::put64(bad.data()+24,2);
  must(s->accept(evidence.client,bad.data(),bad.size(),lib)==S::Error::Order);s->ownershipLost();must(s->phase()==S::Phase::Retained);rejections+=2;}
 {auto s=std::unique_ptr<S::Session>(new S::Session);must(s->open(evidence));must(!s->close(evidence.client+1));must(s->close(evidence.client));must(s->phase()==S::Phase::Retained);++rejections;}
 printf("{\"passed\":true,\"checks\":%u,\"rejected\":%u,\"requests\":4,\"programs\":3,\"cpu_simulation_only\":true,\"semantic_guard_separation_verified\":true,\"session_bytes\":%zu,\"plan_bytes\":%zu,\"gpu_commands_submitted\":false}\n",checks,rejections,sizeof(S::Session),sizeof(I::Plan));
 return 0;
}

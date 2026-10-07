#include "ExecutionTranscript.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace E=ExecutionCodec;namespace P=ExecutionPlan;namespace C=ChannelCodec;namespace R=GSPComputePrep;namespace W=WorkSubmitToken;
using Bytes=std::vector<unsigned char>;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %u: %s\n",unsigned(__LINE__),#x);std::exit(1);}}while(0)
static Bytes load(const char *path){std::ifstream f(path,std::ios::binary);CHECK(bool(f));return Bytes(std::istreambuf_iterator<char>(f),{});}
static void save(const std::string &path,const Bytes &b){std::ofstream f(path,std::ios::binary);CHECK(bool(f));f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();CHECK(bool(f));}
static void checksum(Bytes &b){R::put32(b.data()+32,0);unsigned sum=0;const unsigned end=(48+R::get32(b.data()+56)+7)&~7U;CHECK(end<=b.size());for(unsigned i=0;i<end;i+=4)sum^=R::get32(b.data()+i);R::put32(b.data()+32,sum);}
int main(int argc,char **argv){
  CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);CHECK(gr.size()==4096&&fifo.size()==4096);
  GSPInitEvents::Record record;CHECK(GSPInitEvents::decode(gr.data(),4096,11,record));R::Result prep;prep.step=5;CHECK(R::reply(gr.data(),record,prep));
  W::Runlist historical;CHECK(W::fifo(fifo.data(),4096,10,historical));
  C::Plan golden;P::Plan plan;CHECK(C::plan(gr.data()+104,1664,golden));CHECK(P::make(gr.data()+104,1664,golden,plan));
  Bytes requests(E::RequestBytes),responses(E::RequestBytes);E::Reply accepted;
  for(unsigned step=0;step<E::Steps;++step){
    Bytes request(4096);CHECK(E::request(step,E::FirstSequence+step,golden,3,plan,request.data(),4096));
    CHECK(GSPInitEvents::decode(request.data(),4096,E::FirstSequence+step,record));CHECK(record.function==E::function(step)&&record.payloadBytes==E::header(step)+E::params(step));
    std::memcpy(requests.data()+step*4096,request.data(),4096);
    Bytes response=request;R::put32(response.data()+36,100+step);R::put32(response.data()+64,0);R::put32(response.data()+68,0);
    if(step==E::ShareStep)R::put32(response.data()+112+8,1);
    if(step==E::ChannelStep)R::put32(response.data()+112+136,1);
    if(step==E::SizeStep)std::memcpy(response.data()+104,gr.data()+104,1664);
    if(step==E::FifoStep)std::memcpy(response.data()+104,fifo.data()+104,3212);
    if(step==E::TokenStep)R::put32(response.data()+104,4);
    checksum(response);CHECK(E::reply(response.data(),4096,100+step,step,golden,3,plan,accepted));CHECK(accepted.accepted);
    if(step==E::ChannelStep)CHECK(accepted.channelId==4&&accepted.subdeviceMask==1);
    if(step==E::SizeStep)CHECK(P::valid(accepted.privatePlan,golden)&&accepted.privatePlan.backingBytes==1003520);
    if(step==E::FifoStep)CHECK(accepted.runlist.valid&&accepted.runlist.id==0);
    if(step==E::TokenStep){unsigned candidate=0;CHECK(W::compose(historical,4,accepted.rawToken,candidate));CHECK(candidate==4);}
    std::memcpy(responses.data()+step*4096,response.data(),4096);
    for(unsigned off:{36U,48U,52U,60U,64U,68U,72U,80U,84U,88U,92U,96U,100U}){
      Bytes bad=response;bad[off]^=1;checksum(bad);E::Reply denied;CHECK(!E::reply(bad.data(),4096,100+step,step,golden,3,plan,denied));CHECK(!denied.accepted);
    }
    // Padding and checksum checks remain independent of semantic identity.
    Bytes bad=response;bad[32]^=1;CHECK(!E::reply(bad.data(),4096,100+step,step,golden,3,plan,accepted));
    if(step==E::EnableStep||step==E::ScheduleStep){bad=response;bad[106]=1;checksum(bad);CHECK(!E::reply(bad.data(),4096,100+step,step,golden,3,plan,accepted));}
    for(unsigned size:{0U,1U,79U,4095U})CHECK(!E::reply(response.data(),size,100+step,step,golden,3,plan,accepted));
    CHECK(!E::reply(response.data(),4096,100+step,step,golden,4,plan,accepted));
    CHECK(!E::reply(response.data(),4096,101+step,step,golden,3,plan,accepted));
    CHECK(!E::reply(nullptr,4096,100+step,step,golden,3,plan,accepted));
    // Existing echo bytes cannot be silently changed even with a new checksum.
    if(step!=E::SizeStep&&step!=E::FifoStep&&step!=E::TokenStep){
      for(unsigned i=0;i<E::params(step);++i){if(step==E::ChannelStep&&i==136)continue;bad=response;bad[80+E::header(step)+i]^=1;checksum(bad);
        const bool output=(step==E::ChannelStep&&((i>=132&&i<136)||(i>=240&&i<244)))||(step==E::ShareStep&&i==8);
        CHECK(E::reply(bad.data(),4096,100+step,step,golden,3,plan,accepted)==output);CHECK(accepted.accepted==output);
      }
    }
    Bytes rejected(4096,0x5a);const auto before=rejected;
    for(unsigned cid:{0U,2U,4U,~0U}){CHECK(!E::request(step,E::FirstSequence+step,golden,cid,plan,rejected.data(),4096));CHECK(rejected==before);}
    CHECK(!E::request(step,E::FirstSequence+step,golden,3,plan,rejected.data(),4095));CHECK(rejected==before);
    CHECK(!E::request(step,~0U,golden,3,plan,rejected.data(),4096));CHECK(rejected==before);
  }
  save(std::string(argv[3])+"/requests.bin",requests);save(std::string(argv[3])+"/replies.bin",responses);
  // Async allocation may fall back to VEID zero. It never supplies the work-submit token.
  for(unsigned veid:{0U,1U,63U,64U,~0U}){
    Bytes share(responses.begin()+E::ShareStep*4096,responses.begin()+(E::ShareStep+1)*4096);
    R::put32(share.data()+120,veid);checksum(share);
    CHECK(E::reply(share.data(),4096,100+E::ShareStep,E::ShareStep,golden,3,plan,accepted)==(veid<64));
    CHECK(accepted.accepted==(veid<64));
    if(veid<64)CHECK(accepted.subcontextId==veid&&accepted.rawToken==~0U&&accepted.channelId==~0U);
  }
  // Context requests must be derived from the fresh report, not reused golden
  // sizes. Change main and patch sizes, then retain that plan for both phases.
  Bytes fresh(gr.begin()+104,gr.begin()+104+1664);R::put32(fresh.data(),R::get32(fresh.data())+0x20000);R::put32(fresh.data()+16*8,32768);
  P::Plan changed;CHECK(P::make(fresh.data(),1664,golden,changed));CHECK(changed.backingBytes==1167360);
  Bytes changedRequests(8192);for(unsigned i=0;i<2;++i)CHECK(E::request(E::PhysicalStep+i,E::FirstSequence+E::PhysicalStep+i,golden,3,changed,changedRequests.data()+4096*i,4096));
  CHECK(std::memcmp(changedRequests.data(),requests.data()+E::PhysicalStep*4096,8192)!=0);save(std::string(argv[3])+"/changed-promotions.bin",changedRequests);
  for(unsigned i=0;i<2;++i){E::Reply denied;CHECK(!E::reply(responses.data()+(E::PhysicalStep+i)*4096,4096,100+E::PhysicalStep+i,E::PhysicalStep+i,golden,3,changed,denied));CHECK(!denied.accepted);}
  for(unsigned token:{0U,3U,5U,4095U,0x10004U,~0U}){Bytes bad(responses.begin()+E::TokenStep*4096,responses.end());R::put32(bad.data()+104,token);checksum(bad);CHECK(!E::reply(bad.data(),4096,100+E::TokenStep,E::TokenStep,golden,3,plan,accepted));}
  for(unsigned runlist:{128U,~0U}){Bytes bad(responses.begin()+E::FifoStep*4096,responses.begin()+(E::FifoStep+1)*4096);R::put32(bad.data()+104+12+12,runlist);checksum(bad);CHECK(!E::reply(bad.data(),4096,100+E::FifoStep,E::FifoStep,golden,3,plan,accepted));}
  Bytes badGR(responses.begin()+E::SizeStep*4096,responses.begin()+(E::SizeStep+1)*4096);R::put32(badGR.data()+104,0);checksum(badGR);CHECK(!E::reply(badGR.data(),4096,100+E::SizeStep,E::SizeStep,golden,3,plan,accepted));
  Bytes unprepared(4096,0x5a);const auto before=unprepared;P::Plan empty;
  CHECK(E::request(0,E::FirstSequence,golden,3,empty,unprepared.data(),4096));
  unprepared=before;CHECK(!E::request(E::PhysicalStep,E::FirstSequence+E::PhysicalStep,golden,3,empty,unprepared.data(),4096));CHECK(unprepared==before);
  CHECK(!E::request(E::VirtualStep,E::FirstSequence+E::VirtualStep,golden,3,empty,unprepared.data(),4096));CHECK(unprepared==before);
  CHECK(!E::request(E::Steps,E::FirstSequence+E::Steps,golden,3,plan,unprepared.data(),4096));CHECK(unprepared==before);
  auto badGolden=golden;badGolden.backingBytes++;CHECK(!E::request(0,E::FirstSequence,badGolden,3,plan,unprepared.data(),4096));CHECK(unprepared==before);
  auto savedPlan=plan;CHECK(!E::request(E::PhysicalStep,E::FirstSequence+E::PhysicalStep,golden,3,plan,reinterpret_cast<unsigned char*>(&plan),4096));CHECK(std::memcmp(&savedPlan,&plan,sizeof(plan))==0);
  E::Reply alias;alias.rawToken=23;const auto aliasBefore=alias;
  CHECK(!E::reply(reinterpret_cast<const unsigned char*>(&alias),4096,100,0,golden,3,plan,alias));CHECK(std::memcmp(&aliasBefore,&alias,sizeof(alias))==0);
  CHECK(!E::reply(responses.data(),4096,100,E::Steps,golden,3,plan,accepted));
  Bytes scratch(4096);ExecutionTranscript::Result transcript;
  auto verify=[&](const Bytes &req,const Bytes &replies){return ExecutionTranscript::verify(req.data(),unsigned(req.size()),replies.data(),unsigned(replies.size()),100,golden,3,scratch.data(),transcript);};
  CHECK(verify(requests,responses));CHECK(transcript.passed&&transcript.completed==E::Steps&&transcript.records==E::Steps&&transcript.pages==E::Steps&&transcript.events==0&&transcript.candidate==4&&transcript.nextSequence==100+E::Steps);
  for(unsigned step=0;step<E::Steps;++step){auto altered=requests;altered[step*4096+84]^=1;CHECK(!verify(altered,responses));CHECK(!transcript.passed);}
  for(unsigned step=0;step+1<E::Steps;++step){auto altered=responses;std::swap_ranges(altered.begin()+step*4096,altered.begin()+(step+1)*4096,altered.begin()+(step+1)*4096);
    // Repair transport sequence/checksums; semantic ordering must still fail.
    for(unsigned i=step;i<=step+1;++i){Bytes page(altered.begin()+i*4096,altered.begin()+(i+1)*4096);R::put32(page.data()+36,100+i);checksum(page);std::copy(page.begin(),page.end(),altered.begin()+i*4096);}
    CHECK(!verify(requests,altered));CHECK(!transcript.passed);
  }
  CHECK(!verify(requests,Bytes(responses.begin(),responses.end()-4096)));CHECK(!transcript.passed);
  auto changedWithoutRequest=responses;
  std::memcpy(changedWithoutRequest.data()+E::SizeStep*4096+104,fresh.data(),1664);
  {Bytes page(changedWithoutRequest.begin()+E::SizeStep*4096,changedWithoutRequest.begin()+(E::SizeStep+1)*4096);checksum(page);std::copy(page.begin(),page.end(),changedWithoutRequest.begin()+E::SizeStep*4096);}
  CHECK(!verify(requests,changedWithoutRequest));CHECK(!transcript.passed);
  auto withEvent=[&](unsigned repeats){Bytes journal;
    for(unsigned i=0;i<repeats;++i){Bytes event(4096);R::put32(event.data()+36,100+i);R::put32(event.data()+40,1);R::put32(event.data()+48,0x03000000);R::put32(event.data()+52,0x43505256);R::put32(event.data()+56,40);R::put32(event.data()+60,0x100c);checksum(event);journal.insert(journal.end(),event.begin(),event.end());}
    for(unsigned i=0;i<E::Steps;++i){Bytes page(responses.begin()+i*4096,responses.begin()+(i+1)*4096);R::put32(page.data()+36,100+repeats+i);checksum(page);journal.insert(journal.end(),page.begin(),page.end());}return journal;};
  CHECK(verify(requests,withEvent(1)));CHECK(transcript.events==1&&transcript.records==E::Steps+1&&transcript.nextSequence==101+E::Steps);
  CHECK(verify(requests,withEvent(3)));CHECK(transcript.events==3&&transcript.records==16);
  CHECK(!verify(requests,withEvent(4)));CHECK(!transcript.passed);
  auto trailing=responses;trailing.insert(trailing.end(),responses.begin(),responses.begin()+4096);CHECK(!verify(requests,trailing));CHECK(!transcript.passed);
  CHECK(!ExecutionTranscript::verify(requests.data(),E::RequestBytes,responses.data(),E::RequestBytes,~0U,golden,3,scratch.data(),transcript));
  const auto requestsBefore=requests;
  CHECK(!ExecutionTranscript::verify(requests.data(),E::RequestBytes,responses.data(),E::RequestBytes,100,golden,3,requests.data(),transcript));CHECK(requests==requestsBefore);
  std::printf("{\"passed\":true,\"checks\":%u,\"request_count\":13,\"tx_first\":19,\"tx_last\":31,\"synthetic_rx_first\":100,\"synthetic_rx_last\":112,\"fresh_changed_private_bytes\":1167360,\"hardware_accessed\":false,\"doorbell_ready\":false,\"compute_verified\":false,\"metal_verified\":false}\n",checks);
}

#include "changes/gsp-compute-0.25/memory/HostSimulation.hpp"
#include "changes/gsp-compute-0.25/memory/ComputeMemory.hpp"
// Legacy allocation evidence is rejected by the grouped protocol. All new replies are synthetic.
struct IdentitySim:RuntimeSim {
  unsigned cid,token;
  IdentitySim(const Bytes &gr,const Bytes &fifo,unsigned id,unsigned raw=4):RuntimeSim(gr,fifo),cid(id),token(raw){}
  bool doorbell(){
    const unsigned slot=R::get32(q.data()+0x41010),step=qBells>=XV::Steps?qBells-XV::Steps:~0U;
    if(!RuntimeSim::doorbell())return false;
    if(step==EC::ChannelStep||step==EC::TokenStep){
      const unsigned off=0x42000+slot*4096;Bytes reply(q.begin()+off,q.begin()+off+4096);
      if(step==EC::ChannelStep){put(reply,244,cid);put(reply,352,0xc9f00007U);}else put(reply,104,token);
      fix(reply);std::memcpy(q.data()+off,reply.data(),4096);
    }return true;
  }
};
int main(int argc,char **argv){
  CHECK(argc==5);const auto gr=load(argv[1]),fifo=load(argv[2]),actual=load(argv[3]);
  // Exercise the old helper too, retaining warning-clean fixture extraction.
  {RuntimeSim old(gr,fifo);CHECK(run(old).rpc.passed);}
  C::Plan golden;CHECK(C::plan(gr.data()+104,1664,golden));P::Plan empty;EC::Reply decoded;
  CHECK(!EC::reply(actual.data(),unsigned(actual.size()),21,EC::ChannelStep,golden,3,empty,decoded));
  // Convert a copy to the current synthetic user-channel contract; keep actual evidence immutable.
  Bytes grouped=actual;put(grouped,80,P::Client);put(grouped,84,P::Group);put(grouped,136,P::Share);put(grouped,140,0);
  put(grouped,240,0);put(grouped,356,0x14);fix(grouped);
  CHECK(EC::reply(grouped.data(),unsigned(grouped.size()),21,EC::ChannelStep,golden,3,empty,decoded));
  CHECK(decoded.channelId==5&&decoded.subdeviceMask<=1);
  unsigned cases=0;
  for(unsigned cid:{4U,5U,37U,4096U,0xfffffffeU}){
    IdentitySim io(gr,fifo,cid);Bytes records(R::MaxBytes),requests(EC::RequestBytes),scratch(4096),externalRecords(R::MaxBytes),externalRequests(XV::RequestBytes);TX::Result result;
    CHECK(TX::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),io.goldenResult,io.goldenBytes.data(),records.data(),requests.data(),scratch.data(),result,externalRecords.data(),externalRequests.data()));
    records.resize(result.rpc.bytes);CHECK(result.channelId==cid&&result.rawToken==4&&result.candidate==4&&io.consumed==EC::Steps);
    const L::Range ring={0x1020003000ULL,0x03400000,4096};walk(io,&ring,1,io.contexts.childBytes);
    if(cid==5){save(std::string(argv[4])+"/cid5-requests.bin",requests);save(std::string(argv[4])+"/cid5-records.bin",records);}
    CHECK(H::proof(io.golden,result,requests.data(),records.data(),scratch.data()));
    FenceSim host;const auto fence=fenceRun(host,io.golden,result,requests,records);
    CHECK(fence.passed&&fence.token==4&&ComputeMemory::hostReady(fence,result));
    auto bad=result;bad.channelId=cid==5?4:5;CHECK(!H::proof(io.golden,bad,requests.data(),records.data(),scratch.data()));
    bad=result;bad.rawToken=5;CHECK(!ComputeMemory::hostReady(fence,bad));
    bad=result;bad.candidate=5;CHECK(!ComputeMemory::hostReady(fence,bad));++cases;
  }
  for(unsigned cid:{0U,3U,0xffffffffU}){
    IdentitySim io(gr,fifo,cid);Bytes records(R::MaxBytes),requests(EC::RequestBytes),scratch(4096),externalRecords(R::MaxBytes),externalRequests(XV::RequestBytes);TX::Result result;
    CHECK(!TX::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),io.goldenResult,io.goldenBytes.data(),records.data(),requests.data(),scratch.data(),result,externalRecords.data(),externalRequests.data()));
    CHECK(result.rpc.completed==EC::ChannelStep&&io.consumed==EC::ChannelStep&&result.rpc.sent==EC::ChannelStep+1);++cases;
  }
  {IdentitySim io(gr,fifo,5,5);Bytes records(R::MaxBytes),requests(EC::RequestBytes),scratch(4096),externalRecords(R::MaxBytes),externalRequests(XV::RequestBytes);TX::Result result;
    CHECK(!TX::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),io.goldenResult,io.goldenBytes.data(),records.data(),requests.data(),scratch.data(),result,externalRecords.data(),externalRequests.data()));
    CHECK(result.rpc.completed==EC::TokenStep&&io.consumed==EC::TokenStep&&result.rpc.sent==EC::Steps);++cases;}
  std::printf("{\"passed\":true,\"scenarios\":%u,\"hardware_accessed\":false,\"legacy_allocation_rejected\":true,\"grouped_allocation_synthetic\":true,\"compute_verified\":false,\"metal_verified\":false}\n",cases);
}

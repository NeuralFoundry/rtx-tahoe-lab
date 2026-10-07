// Exact simulator prefix extracted from the already reviewed memory test;
// the Mac runner independently verifies extraction against the source.
#include "MemorySimulation.hpp"
#include "ExecutionTransactions.hpp"
namespace TX=ExecutionTransactions;namespace EC=ExecutionCodec;namespace G=ChannelTransactions;
namespace XV=ExternalVAS;
static void put(Bytes &b,unsigned off,unsigned value){CHECK(off+4<=b.size());R::put32(b.data()+off,value);}
static void fix(Bytes &b){put(b,32,0);unsigned sum=0,end=(48+R::get32(b.data()+56)+7)&~7U;CHECK(end<=b.size());for(unsigned i=0;i<end;i+=4)sum^=R::get32(b.data()+i);put(b,32,sum);}
struct RuntimeSim:Sim {
  Bytes q=Bytes(0x81000),prepBytes=Bytes(6*4096),pdBytes=Bytes(4096),goldenBytes=Bytes(5*4096),freshGR,fifo;
  R::Result prep,pd;G::Result goldenResult;
  bool queueClaimed=false,preparedFixed=false,preparedContexts=false;unsigned consumed=0,fwSequence=18;
  bool externalClaimed=false,externalComplete=false;unsigned externalConsumedCount=0,directoryChecks=0,externalMode=0,externalTarget=0;
  Bytes externalRequestCapture,externalRecordCapture;
  unsigned qReads=0,qWrites=0,qImports=0,qPublishes=0,qBells=0,responseReads=0,fixedCalls=0,contextCalls=0;
  unsigned failRead=0,failWrite=0,failImport=0,failPublish=0,failBell=0,mode=0,targetStep=0,leadingEvents=0,eventPages=1;
  unsigned long long excluded=0;
  RuntimeSim(const Bytes &gr,const Bytes &fifoRecord,unsigned start=19):Sim(gr),freshGR(gr.begin()+104,gr.begin()+104+1664),fifo(fifoRecord.begin()+104,fifoRecord.begin()+104+3212){
    const unsigned pdEnd=(start+58)%63,prepEnd=(pdEnd+62)%63;
    prep.passed=true;prep.completed=prep.sent=prep.count=prep.pages=6;prep.bytes=6*4096;prep.txWriter=prep.txReader=8;
    prep.initialReader=(prepEnd+57)%63;prep.initialSequence=6;prep.rxReader=prep.rxProducer=prepEnd;prep.rxSequence=12;
    for(unsigned step=0;step<6;++step){Bytes packet(4096);CHECK(R::request(step,packet.data()));put(packet,36,step+6);put(packet,64,0);put(packet,68,0);
      if(step==3){GSPPageTables::put64(packet.data()+120,(1ULL<<49)-0x4000000);GSPPageTables::put64(packet.data()+152,0x4000000);}
      fix(packet);std::memcpy(prepBytes.data()+step*4096,packet.data(),4096);
      prep.records[step]={step*4096,4096,R::function(step),0,step+6,R::headerBytes(step)+R::paramSize(step),step,(prep.initialReader+step)%63,0};
    }
    CHECK(GSPPageTables::request(pdBytes.data(),4096));put(pdBytes,36,12);put(pdBytes,64,0);put(pdBytes,68,0);fix(pdBytes);
    pd.passed=true;pd.completed=pd.sent=pd.count=pd.pages=1;pd.bytes=4096;pd.txWriter=pd.txReader=9;pd.initialReader=prepEnd;pd.initialSequence=12;
    pd.rxReader=pd.rxProducer=pdEnd;pd.rxSequence=13;pd.records[0]={0,4096,76,0,12,208,0,prepEnd,0};
    auto &g=goldenResult;g.context=golden;g.channelId=3;g.subdeviceMask=1;g.contextPrepared=g.contextPreparationAttempted=true;
    auto &r=g.rpc;r.passed=true;r.completed=r.sent=r.count=r.pages=5;r.bytes=5*4096;r.txWriter=r.txReader=14;
    r.initialReader=pdEnd;r.initialSequence=13;r.rxReader=r.rxProducer=start;r.rxSequence=18;
    for(unsigned step=0;step<5;++step){Bytes packet(4096);CHECK(C::request(step,golden,packet.data(),4096));put(packet,36,13+step);put(packet,64,0);put(packet,68,0);
      if(step==0)put(packet,112+136,1);if(step==1)std::memcpy(packet.data()+104,gr.data()+104,1664);
      fix(packet);std::memcpy(goldenBytes.data()+step*4096,packet.data(),4096);
      r.records[step]={step*4096,4096,C::function(step),0,13+step,C::header(step)+C::params(step),step,(pdEnd+step)%63,0};
    }
    unsigned header[]={0,0x40000,4096,63,14,1,32,4096};for(unsigned i=0;i<8;++i)put(q,0x1000+i*4,header[i]);
    header[4]=start;for(unsigned i=0;i<8;++i)put(q,0x41000+i*4,header[i]);put(q,0x1020,start);put(q,0x41020,14);
  }
  bool ready(){return owned;}
  bool ringReady(){return owned&&preparedFixed&&E::fixedReady(fixed,storage.goldenBytes);}
  bool claim(){if(queueClaimed)return false;queueClaimed=true;return true;}
  bool externalClaim(){
    CHECK(queueClaimed&&preparedFixed&&qBells==0&&consumed==0);
    if(externalClaimed||externalMode==1)return false;externalClaimed=true;return true;
  }
  bool externalDirectory(){
    CHECK(externalClaimed&&!externalComplete&&directoryChecks<2&&externalConsumedCount==(directoryChecks?XV::Steps:0));
    if(externalMode==2+directoryChecks)return false;++directoryChecks;return true;
  }
  bool externalConsumed(unsigned step){
    CHECK(step==externalConsumedCount&&qBells==step+1&&R::get32(q.data()+0x41020)==XV::FirstSequence+1+step&&directoryChecks==1);
    if(externalMode==4&&step==externalTarget)return false;++externalConsumedCount;return true;
  }
  bool externalFinish(){
    CHECK(externalClaimed&&!externalComplete&&externalConsumedCount==XV::Steps&&directoryChecks==2);
    if(externalMode==5)return false;externalComplete=true;return true;
  }
  unsigned long long nowNs(){const auto raw=Sim::nowNs();return raw>=excluded?raw-excluded:0;}
  void delayUs(unsigned us){CHECK(us==100);time+=100000000;}
  bool prepareFixed(){
    ++fixedCalls;CHECK(queueClaimed&&fixedCalls==1&&qBells==0&&consumed==0);if(mode==21)return false;
    const auto start=Sim::nowNs();const bool success=Sim::runFixed();const auto end=Sim::nowNs();
    if(!success||end<start||end-start>=M::BudgetNs)return false;excluded+=end-start;preparedFixed=true;
    if(mode==22)time+=R::BudgetNs;return true;
  }
  bool prepareContext(const P::Plan &p){
    ++contextCalls;CHECK(externalComplete&&contextCalls==1&&qBells==XV::Steps+EC::PhysicalStep&&consumed==EC::PhysicalStep&&R::get32(q.data()+0x41020)==EC::FirstSequence+EC::PhysicalStep);
    if(mode==23)return false;plan=p;const Bytes before(backing.begin(),backing.begin()+E::FixedBytes);
    const auto start=Sim::nowNs();const bool success=Sim::runContexts();const auto end=Sim::nowNs();
    CHECK(std::equal(before.begin(),before.end(),backing.begin()));
    if(!success||end<start||end-start>=M::BudgetNs)return false;excluded+=end-start;preparedContexts=true;
    if(mode==24)time+=R::BudgetNs;return true;
  }
  bool contextReady(const P::Plan &p){return mode!=25&&preparedContexts&&contexts.passed&&P::valid(p,golden)&&p.backingBytes==plan.backingBytes;}
  bool replyConsumed(unsigned step){
    CHECK(externalComplete&&step==consumed&&qBells==XV::Steps+step+1&&R::get32(q.data()+0x41020)==EC::FirstSequence+1+step);
    if(mode==26&&step==targetStep)return false;++consumed;
    if(step==EC::ChannelStep){backing[0x800+0x8c]=0x73;backing[0x1000+16]=0x29;}return true;
  }
  bool import(){++qImports;return qImports!=failImport;}
  bool publish(){CHECK(queueClaimed&&preparedFixed);++qPublishes;return qPublishes!=failPublish;}
  bool read(unsigned off,unsigned char *out,unsigned bytes){
    CHECK(bytes<=4096&&off+bytes<=q.size());++qReads;if(qReads==failRead)return false;std::memcpy(out,q.data()+off,bytes);
    if(off>=0x42000&&bytes==4096){++responseReads;if(mode==11&&responseReads%2==0)out[128]^=1;}
    if(mode==12&&qReads==8)owned=false;return true;
  }
  bool write(unsigned off,const unsigned char *data,unsigned bytes){
    CHECK(queueClaimed&&preparedFixed);CHECK((off==0x1020||off==0x1010)?bytes==4:(off>=0x10000&&off<=0x2000+(EC::FinalProducer-1)*4096&&off%4096==0&&bytes==4096));
    ++qWrites;std::memcpy(q.data()+off,data,bytes);return qWrites!=failWrite;
  }
  void append(const Bytes &p){
    CHECK(p.size()%4096==0);for(unsigned off=0;off<p.size();off+=4096){const unsigned slot=R::get32(q.data()+0x41010);CHECK(slot<63);
      std::memcpy(q.data()+0x42000+slot*4096,p.data()+off,4096);put(q,0x41010,(slot+1)%63);}
  }
  bool doorbell(){
    ++qBells;if(qBells==failBell)return false;CHECK(qBells<=XV::Steps+EC::Steps);
    if(qBells<=XV::Steps){
      const unsigned step=qBells-1;CHECK(externalClaimed&&!externalComplete&&directoryChecks==1);
      CHECK(R::get32(q.data()+0x1010)==XV::FirstSequence+1+step);Bytes reply(4096);CHECK(XV::request(step,reply.data(),4096));
      CHECK(std::memcmp(q.data()+0x10000+step*4096,reply.data(),4096)==0);
      if(externalMode==6&&step==externalTarget)return true;
      put(reply,36,fwSequence++);put(reply,64,0);put(reply,68,0);if(step==0)put(reply,112,XV::Client);
      if(step==4)put(reply,56,32); // Real SET_PAGE_DIRECTORY reply has no payload.
      if(step==externalTarget){
        if(externalMode==7)put(reply,64,0x56);if(externalMode==8)put(reply,step==4?60:80,step==4?103:R::Client);
        if(externalMode==9)put(reply,72,1);if(externalMode==10)put(reply,36,999);
        if(externalMode==11)put(reply,step==4?56:96,step==4?36:0x56);
      }
      fix(reply);append(reply);if(externalMode!=12||step!=externalTarget)put(q,0x41020,XV::FirstSequence+1+step);return true;
    }
    const unsigned step=qBells-XV::Steps-1;CHECK(externalComplete);
    CHECK(R::get32(q.data()+0x1010)==EC::FirstSequence+1+step);Bytes canonical(4096);CHECK(EC::request(step,EC::FirstSequence+step,golden,3,plan,canonical.data(),4096));
    CHECK(std::memcmp(q.data()+0x2000+(EC::FirstSequence+step)*4096,canonical.data(),4096)==0);if(step>=EC::PhysicalStep)CHECK(preparedContexts);
    if(mode==9&&step==targetStep)return true;
    if(step==0)for(unsigned i=0;i<leadingEvents;++i){Bytes event(eventPages*4096);put(event,36,fwSequence++);put(event,40,eventPages);put(event,48,0x03000000);put(event,52,0x43505256);
      const unsigned payload=eventPages==1?8:5008;put(event,56,32+payload);put(event,60,0x100c);put(event,84,payload-8);fix(event);append(event);}
    Bytes reply=canonical;put(reply,36,fwSequence++);put(reply,64,0);put(reply,68,0);
    if(step==EC::ShareStep)put(reply,120,1);if(step==EC::ChannelStep)put(reply,112+136,1);if(step==EC::SizeStep)std::memcpy(reply.data()+104,freshGR.data(),1664);
    if(step==EC::FifoStep)std::memcpy(reply.data()+104,fifo.data(),3212);if(step==EC::TokenStep)put(reply,104,4);
    if(step==targetStep){
      if(mode==1)put(reply,64,0x56);if(mode==2)put(reply,EC::allocation(step)?96:92,0x56);
      if(mode==3)put(reply,80,0xbad);if(mode==4)put(reply,84,0xbad);if(mode==5)put(reply,88,0xbad);
      if(mode==6)put(reply,36,999);if(mode==7)put(reply,60,0x1003);if(mode==8)put(reply,68,1);
      if(mode==13)put(reply,40,2);if(mode==14)put(reply,72,1);
      if(mode==15&&step==EC::ChannelStep)put(reply,112+132,3);
      if(mode==16&&step==EC::SizeStep)put(reply,104,0);
      if(mode==17&&step==EC::TokenStep)put(reply,104,0x10004);
      if(mode==18&&step==EC::FifoStep)put(reply,104+12+12,128);
    }
    fix(reply);if(mode==19&&step==targetStep)reply[80]^=1;append(reply);
    if(mode!=10||step!=targetStep)put(q,0x41020,EC::FirstSequence+1+step);return true;
  }
};
static TX::Result run(RuntimeSim &io,Bytes *requestsOut=nullptr,Bytes *recordsOut=nullptr){
  Bytes records(R::MaxBytes),requests(EC::RequestBytes),scratch(4096),externalRecords(R::MaxBytes),externalRequests(XV::RequestBytes);TX::Result result;
  const bool passed=TX::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),io.goldenResult,io.goldenBytes.data(),records.data(),requests.data(),scratch.data(),result,externalRecords.data(),externalRequests.data());
  io.externalRequestCapture=externalRequests;externalRecords.resize(result.external.rpc.bytes);io.externalRecordCapture=externalRecords;
  if(!result.external.rpc.passed)CHECK(result.rpc.sent==0&&io.consumed==0&&io.contextCalls==0);
  if(passed)CHECK(ExternalSetup::verify(io.goldenResult.rpc,externalRequests.data(),externalRecords.data(),result.external,scratch.data())&&io.externalComplete);
  const auto &r=result.rpc;CHECK(passed==r.passed);CHECK(r.sent<=EC::Steps&&r.completed<=EC::Steps&&r.count<=16&&r.pages<=32&&r.ticks<=R::MaxTicks+1);
  if(!result.fixedPrepared)CHECK(r.sent==0);if(!result.contextPrepared)CHECK(r.sent<=EC::PhysicalStep);
  if(passed){CHECK(r.completed==EC::Steps&&r.sent==EC::Steps&&r.txWriter==EC::FinalProducer&&r.txReader==EC::FinalProducer&&io.consumed==EC::Steps&&result.channelId==4&&result.rawToken==4&&result.candidate==4&&result.contextPrepared);}
  if(requestsOut)*requestsOut=requests;if(recordsOut){records.resize(r.bytes);*recordsOut=records;}return result;
}
int main(int argc,char **argv){
  CHECK(argc==4);const auto gr=load(argv[1]),fifo=load(argv[2]);CHECK(gr.size()==4096&&fifo.size()==4096);
  RuntimeSim baseline(gr,fifo);CHECK(TX::prefix(baseline.prep,baseline.prepBytes.data(),baseline.pd,baseline.pdBytes.data(),baseline.goldenResult,baseline.goldenBytes.data()));
  Bytes requests,records;const auto result=run(baseline,&requests,&records);CHECK(result.rpc.passed);
  L::Range oldRanges[10],newRanges[6];CHECK(C::mappingRanges(baseline.golden,oldRanges)&&P::mappings(baseline.plan,baseline.golden,newRanges));
  walk(baseline,oldRanges,10,baseline.contexts.childBytes);walk(baseline,newRanges,6,baseline.contexts.childBytes);
  save(std::string(argv[3])+"/runtime-requests.bin",requests);save(std::string(argv[3])+"/runtime-records.bin",records);
  save(std::string(argv[3])+"/runtime-children.bin",Bytes(baseline.fullImage.begin(),baseline.fullImage.begin()+baseline.contexts.childBytes));
  for(unsigned start:{0U,1U,59U,60U,61U,62U}){RuntimeSim io(gr,fifo,start);CHECK(run(io).rpc.passed);}
  {RuntimeSim io(gr,fifo,62);io.leadingEvents=1;io.eventPages=2;CHECK(run(io).rpc.passed);}
  {RuntimeSim io(gr,fifo);io.leadingEvents=3;CHECK(run(io).rpc.passed);}
  {RuntimeSim io(gr,fifo);io.leadingEvents=4;CHECK(!run(io).rpc.passed);}
  for(unsigned step=0;step<EC::Steps;++step)for(unsigned mode:{1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,13U,14U,19U,26U}){RuntimeSim io(gr,fifo);io.targetStep=step;io.mode=mode;CHECK(!run(io).rpc.passed);}
  for(unsigned mode:{11U,12U,21U,22U,23U,24U,25U}){RuntimeSim io(gr,fifo);io.mode=mode;CHECK(!run(io).rpc.passed);}
  for(unsigned mode:{15U,16U,17U,18U}){RuntimeSim io(gr,fifo);io.mode=mode;io.targetStep=mode==15?EC::ChannelStep:mode==16?EC::SizeStep:mode==17?EC::TokenStep:EC::FifoStep;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.qReads;++i){RuntimeSim io(gr,fifo);io.failRead=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.qWrites;++i){RuntimeSim io(gr,fifo);io.failWrite=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.qImports;++i){RuntimeSim io(gr,fifo);io.failImport=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=baseline.qPublishes;++i){RuntimeSim io(gr,fifo);io.failPublish=i;CHECK(!run(io).rpc.passed);}
  for(unsigned i=1;i<=XV::Steps+EC::Steps;++i){RuntimeSim io(gr,fifo);io.failBell=i;CHECK(!run(io).rpc.passed);}
  for(unsigned mode=1;mode<=12;++mode)for(unsigned step=0;step<XV::Steps;++step){RuntimeSim io(gr,fifo);io.externalMode=mode;io.externalTarget=step;CHECK(!run(io).rpc.passed&&io.consumed==0&&io.contextCalls==0);}
  for(unsigned step=0;step<5;++step){RuntimeSim io(gr,fifo);io.goldenBytes[step*4096+80]^=1;CHECK(!run(io).rpc.passed);CHECK(!io.queueClaimed&&io.qWrites==0&&io.fixedCalls==0);}
  for(unsigned field=0;field<7;++field){RuntimeSim io(gr,fifo);auto &g=io.goldenResult;
    if(field==0)g.channelId=4;if(field==1)g.context.buffers[0].physical+=4096;if(field==2)g.rpc.txReader=13;if(field==3)g.rpc.records[0].slot=63;
    if(field==4)g.rpc.records[2].step=1;if(field==5)g.rpc.records[4].payload=1;if(field==6)g.contextPrepared=false;
    CHECK(!run(io).rpc.passed);CHECK(!io.queueClaimed&&io.qWrites==0&&io.fixedCalls==0);
  }
  {RuntimeSim io(gr,fifo);io.prep.failure=R::Read;CHECK(!run(io).rpc.passed);CHECK(!io.queueClaimed);}
  {RuntimeSim io(gr,fifo);io.pd.failure=R::Read;CHECK(!run(io).rpc.passed);CHECK(!io.queueClaimed);}
  {RuntimeSim io(gr,fifo);io.owned=false;CHECK(!run(io).rpc.passed);CHECK(!io.queueClaimed);}
  {RuntimeSim io(gr,fifo);io.queueClaimed=true;CHECK(!run(io).rpc.passed);CHECK(io.qWrites==0&&io.fixedCalls==0);}
  {RuntimeSim io(gr,fifo);R::put32(io.freshGR.data(),R::get32(io.freshGR.data())+0x20000);R::put32(io.freshGR.data()+16*8,32768);
    Bytes changed;const auto r=run(io,&changed);CHECK(r.rpc.passed&&r.context.backingBytes==1167360);CHECK(changed!=requests);save(std::string(argv[3])+"/runtime-changed-requests.bin",changed);
  }
  {RuntimeSim io(gr,fifo);Bytes out(R::MaxBytes),scratch(4096),externalRecords(R::MaxBytes),externalRequests(XV::RequestBytes);TX::Result r;
    CHECK(!TX::execute(io,io.prep,io.prepBytes.data(),io.pd,io.pdBytes.data(),io.goldenResult,io.goldenBytes.data(),out.data(),out.data(),scratch.data(),r,externalRecords.data(),externalRequests.data()));CHECK(!r.rpc.passed&&io.qWrites==0);
  }
  std::printf("{\"passed\":true,\"scenarios\":%u,\"checks\":%u,\"queue_reads\":%u,\"queue_writes\":%u,\"queue_imports\":%u,\"queue_publishes\":%u,\"queue_doorbells\":%u,\"completed\":13,\"tx_writer\":32,\"initial_rx_sequence\":23,\"final_rx_sequence\":36,\"memory_stages_integrated_in_simulation\":true,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false}\n",
    scenarios,checks,baseline.qReads,baseline.qWrites,baseline.qImports,baseline.qPublishes,baseline.qBells);
}

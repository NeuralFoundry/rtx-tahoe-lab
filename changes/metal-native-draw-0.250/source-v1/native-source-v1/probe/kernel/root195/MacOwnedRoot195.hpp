#pragma once
#include "../root170/LegacyPageInventory169.hpp"
#include "MacOwnedDataArena195.hpp"
#include "../root181/OwnedDataABI181.hpp"
#include "../gpu183/OwnedDispatchABI183.hpp"
#include "../root194/OwnedRootInvalidate194.hpp"
#include "../driver/GSPDigest.hpp"
#include "../MacReusableRuntime.hpp"
#include "../gpu242/GraphicsLayout242.hpp"
#include "../gpu242/GraphicsABI242.hpp"

// One native owner, one externally-owned VAS, one initial PDB bind. No late
// SET/UNSET_PAGE_DIRECTORY operation exists in this class. All table/data DMA
// backing is retained after exposure, including any ambiguous failed append.
class MacOwnedRoot195 {
 MacOwnedDataArena195 arena;
 RTXSpans165::Owner<64,16>*spanOwner=nullptr;
 RTXDataAccess181::State dataAccess;RTXOwnedDispatch183::State gpu;
 RTXGraphicsSubmit241::State*graphics=nullptr;
 static constexpr uint64_t DataBegin=RTXGraphicsLayout242::Begin,DataEnd=RTXGraphicsLayout242::End;
 static constexpr uint32_t DataBuffers=RTXGraphicsLayout242::Buffers;
 enum class Phase:uint32_t {Idle,Preparing,Prepared,Bound,Contexts,Program,Ready,Failed};
 Phase phase=Phase::Idle;
 uint64_t generation=0,client=0,boundPhysical=0;
 RTXPageTree167::Mapping*rows=nullptr;
 uint8_t*sourceRoot=nullptr,*sourceChildren=nullptr,*scratch=nullptr;
 uint32_t childBytes=0,rowCount=0,initialRows=0,contextRows=0;
 RTXLegacy169::Result inventory;
 uint8_t initialDigest[32]{},preparedDigest[32]{},observedDigest[32]{};
 GMMUInvalidate::Result invalidations[2];
 RTXStableTree194::PublishInfo publications[2];
 const GSPComputePrep::Result*initialPrior=nullptr;
 const ExternalSetup::Result*initialResult=nullptr;
 const uint8_t*initialRequests=nullptr,*initialRecords=nullptr;
 uint64_t handles[DataBuffers][8]{};
 bool initialAck=false,postChecked=false;
 static const RTXDataArena178::Request*dataRequests(){
  static const RTXDataArena178::Request r[9]={{4097,3},{65537,3},{1048577,3},{4194305,3},{4096,1},{48,1},{24849,3},{16,3},{8192,1}};return r;
 }
 bool fail(){phase=Phase::Failed;return false;}
 bool imageDigest(uint8_t out[32]){
  const auto t=arena.treeInfo();if(!t.capacity||t.bytes!=uint64_t(t.capacity)*4096||!scratch)return false;
  GSPDigest::SHA256 hash;
  for(uint64_t off=0;off<t.bytes;off+=4096){if(!arena.readTree(off,scratch,4096))return false;hash.update(scratch,4096);}
  hash.finish(out);return true;
 }
 bool pagesStable(){return arena.exposedStable()&&arena.treeInfo().root==boundPhysical;}
 template<class Reader>bool captureLegacy(Reader&reader,const uint8_t*root,const uint8_t*children,uint32_t bytes){
  if(!root||!children||bytes<8192||bytes>RTXLegacy169::MaxChildBytes||bytes%4096||!sourceRoot||!sourceChildren||!rows||!scratch)return false;
  const auto start=reader.nowNs();
  for(unsigned which=0;which<2;++which){const auto n=which?bytes:12288U,base=which?0x1005000U:0x1002000U;
   const auto*expected=which?children:root;auto*copy=which?sourceChildren:sourceRoot;
   for(unsigned off=0;off<n;off+=4096){const auto now=reader.nowNs();
    if(now<start||now-start>=GSPComputePrep::BudgetNs||!reader.readMemory(base+off,scratch,4096))return false;
    for(unsigned i=0;i<4096;++i)if(scratch[i]!=expected[off+i])return false;
    bcopy(scratch,copy+off,4096);
   }
  }
  inventory=RTXLegacy169::decode(sourceRoot,12288,sourceChildren,bytes,generation,rows,RTXLegacy169::MaxPages);
  if(inventory.error!=RTXLegacy169::Error::None)return false;
  childBytes=bytes;rowCount=inventory.pages;return true;
 }
 template<class Ready>bool invalidate(MacGSPContext&context,uint64_t root,Ready&ready,unsigned index){
  if(index>=2||root!=boundPhysical||!root||invalidations[index].attempted)return false;
  struct IO {
   MacGSPContext&context;Ready&ready;uint64_t root;
   uint64_t now(){uint64_t a=0,n=0;clock_get_uptime(&a);absolutetime_to_nanoseconds(a,&n);return n;}
   bool read(uint32_t a,uint32_t&v){return ready()&&(a==0x30a0||a==0x30a4||a==0x30b0)&&GSPVirtualRegisters::read(context,a,v);}
   bool write(uint32_t a,uint32_t v){
    return ready()&&((a==0x30a0&&v==RTXRootInvalidate194::low(root))||(a==0x30a4&&v==RTXRootInvalidate194::high(root))||
      (a==0x30b0&&v==RTXRootInvalidate194::Command))&&GSPVirtualRegisters::write(context,a,v);
   }
  }io{context,ready,root};
  const GMMUInvalidate::Preconditions p{true,true,true,true};
  return RTXRootInvalidate194::run(io,root,p,invalidations[index]);
 }
 bool initialJournal(uint8_t*verifyScratch)const{
  return initialAck&&initialPrior&&initialResult&&initialRequests&&initialRecords&&
   initialResult->directory.physical==boundPhysical&&initialResult->directory.flags==9&&
   ExternalSetup::verify(*initialPrior,initialRequests,initialRecords,*initialResult,verifyScratch);
 }
 bool admitData(){
  if(spanOwner||phase!=Phase::Program||!postChecked||!pagesStable()||!initialJournal(scratch))return false;
  for(const auto&r:invalidations)if(!r.passed||!r.completed||r.failure!=GMMUInvalidate::Failure::None)return false;
  for(const auto&p:publications)if(p.phase!=RTXStableTree194::PublishPhase::Committed||!p.invalidateAttempted||!p.invalidated||p.root!=boundPhysical)return false;
  spanOwner=new RTXSpans165::Owner<64,16>(generation,DataBegin,DataEnd);if(!spanOwner)return false;
  for(uint32_t i=0;i<DataBuffers;++i){uint64_t session=0,handle=0;RTXSpans165::Mapping m{};
   if(!arena.preparedSpan(i,session,m)||session!=generation||m.allocation!=uint64_t(i)+1||m.mapping!=uint64_t(i)+1||!spanOwner->admitMapping(m,handle)||!handle)return false;
   RTXSpans165::Mapping observed{};uint32_t refs=0;
   if(!spanOwner->inspect(handle,observed,refs)||refs||observed.allocation!=m.allocation||observed.mapping!=m.mapping||observed.gpuVA!=m.gpuVA||
      observed.logicalBytes!=m.logicalBytes||observed.mappedBytes!=m.mappedBytes||observed.access!=m.access)return false;
   handles[i][0]=generation;handles[i][1]=handle;handles[i][2]=m.allocation;handles[i][3]=m.mapping;
   handles[i][4]=m.gpuVA;handles[i][5]=m.logicalBytes;handles[i][6]=m.mappedBytes;handles[i][7]=m.access;
  }
  return pagesStable()&&dataAccess.activate(generation);
 }
 MacOwnedRoot195(const MacOwnedRoot195&)=delete;MacOwnedRoot195&operator=(const MacOwnedRoot195&)=delete;
public:
 MacOwnedRoot195(IOPCIDevice*p,uint64_t gen,uint64_t nativeClient):arena(p),generation(gen),client(nativeClient){}
 ~MacOwnedRoot195()=default;
 bool prepare(MacExecutionMemory&memory,MacGSPContext&context,MacGSPRuntimeDma&dma){
  if(phase!=Phase::Idle)return false;phase=Phase::Preparing;
  auto ready=[&](){return generation>DataBuffers&&client&&memory.fixedReady()&&context.owned()&&context.read(ChannelMemory::Window)==0&&dma.ready()&&dma.directlyMapped();};
  if(!ready())return fail();
  for(unsigned n=0;n<DataBuffers;++n)if(dataRequests()[n].bytes!=RTXGraphicsLayout242::Description[n].bytes||dataRequests()[n].access!=RTXGraphicsLayout242::Description[n].access)return fail();
  graphics=new RTXGraphicsSubmit241::State();if(!graphics)return fail();
  rows=static_cast<RTXPageTree167::Mapping*>(IOMalloc(RTXLegacy169::MaxPages*sizeof(*rows)));
  sourceRoot=static_cast<uint8_t*>(IOMalloc(12288));sourceChildren=static_cast<uint8_t*>(IOMalloc(RTXLegacy169::MaxChildBytes));scratch=static_cast<uint8_t*>(IOMalloc(4096));
  if(!rows||!sourceRoot||!sourceChildren||!scratch)return fail();
  const auto&s=memory.state.storage;
  if(!captureLegacy(memory,s.root,s.fixedChildren,s.goldenBytes)||!ready()||
     !arena.prepare(generation,dataRequests(),DataBuffers,DataBegin,DataEnd,rows,rowCount,ready)||!ready()||!arena.expose())return fail();
  boundPhysical=arena.treeInfo().root;initialRows=rowCount;
  if(!boundPhysical||!pagesStable()||!imageDigest(initialDigest))return fail();phase=Phase::Prepared;return true;
 }
 ExternalVAS::Directory directory()const{return{boundPhysical,9};}
 bool initialStable(){return phase==Phase::Prepared&&pagesStable()&&arena.importTreeForObservation()&&imageDigest(observedDigest)&&RTXOwnedDispatch183::equal(initialDigest,observedDigest,32);}
 bool retainJournal(const GSPComputePrep::Result&prior,const uint8_t*requests,const uint8_t*records,const ExternalSetup::Result&result){
  if(phase!=Phase::Prepared||initialPrior||initialResult||initialRequests||initialRecords||!requests||!records||!ExternalSetup::origin(prior))return fail();
  initialPrior=&prior;initialResult=&result;initialRequests=requests;initialRecords=records;return true;
 }
 bool confirmInitial(const GSPComputePrep::Result&prior,const uint8_t*requests,const uint8_t*records,const ExternalSetup::Result&result,uint8_t*verifyScratch){
  if(phase!=Phase::Prepared||initialAck||initialPrior!=&prior||initialResult!=&result||initialRequests!=requests||initialRecords!=records||result.directory.physical!=boundPhysical||result.directory.flags!=9||
     !initialStable()||!ExternalSetup::verify(prior,requests,records,result,verifyScratch))return fail();
  initialPrior=&prior;initialResult=&result;initialRequests=requests;initialRecords=records;initialAck=true;phase=Phase::Bound;return true;
 }
 bool appendContexts(MacExecutionMemory&memory,MacGSPContext&context,MacGSPRuntimeDma&dma,const ExecutionPlan::Plan&plan){
  if(phase!=Phase::Bound||!initialJournal(scratch))return fail();
  auto ready=[&](){return phase==Phase::Bound&&initialAck&&memory.contextReady(plan)&&context.owned()&&context.read(ChannelMemory::Window)==0&&dma.ready()&&dma.directlyMapped();};
  const auto&s=memory.state.storage;
  if(!ready()||!captureLegacy(memory,s.root,s.fullChildren,memory.state.contexts.childBytes)||rowCount<=initialRows)return fail();
  auto flush=[&](uint64_t root){return invalidate(context,root,ready,0);};
  const bool appended=arena.appendPrefix(rows,rowCount,ready,flush);publications[0]=arena.lastPublication();if(!appended)return fail();contextRows=rowCount;
  if(!invalidations[0].passed||!publications[0].invalidated||!pagesStable())return fail();phase=Phase::Contexts;return true;
 }
 bool finalize(MacReusableBackend035&owner,MacGSPContext&context,MacGSPRuntimeDma&dma,RtxReusableRuntime035::State&runtime){
  if(phase!=Phase::Contexts||!initialJournal(scratch)||!runtime.prepared||runtime.opened||runtime.closed||!runtime.preparation.passed||runtime.generation!=generation||runtime.client!=client)return fail();
  auto ready=[&](){return phase==Phase::Contexts&&owner.bootReady(generation,client,false)&&context.read(ChannelMemory::Window)==0&&dma.ready()&&dma.directlyMapped();};
  const auto&s=runtime.storage;
  if(!ready()||!captureLegacy(owner,s.captureRoot,s.captureChildren,s.childBytes)||rowCount!=contextRows+6)return fail();
  auto flush=[&](uint64_t root){return invalidate(context,root,ready,1);};
  const bool appended=arena.appendPrefix(rows,rowCount,ready,flush);publications[1]=arena.lastPublication();if(!appended)return fail();
  if(!invalidations[1].passed||!publications[1].invalidated||!pagesStable()||!imageDigest(preparedDigest)||
     !arena.importTreeForObservation()||!imageDigest(observedDigest)||!RTXOwnedDispatch183::equal(preparedDigest,observedDigest,32)||!ready())return fail();
  phase=Phase::Program;postChecked=true;if(!admitData())return fail();phase=Phase::Ready;return true;
 }
 bool acknowledged()const{return phase==Phase::Ready&&initialAck&&postChecked&&spanOwner&&graphics&&!graphics->retained()&&!dataAccess.retained()&&!gpu.retained();}
 void info(uint64_t*out)const{
  bzero(out,512);out[0]=0x525458524f4f5430ULL;out[1]=RTXGraphicsLayout242::ABI;out[2]=generation;out[3]=phase!=Phase::Idle;out[4]=unsigned(phase);
  out[5]=rowCount!=0;out[6]=unsigned(inventory.error);out[7]=arena.info().totalRows;out[8]=inventory.leaves;out[9]=childBytes;
  const auto t=arena.treeInfo();out[10]=unsigned(arena.info().phase);out[11]=boundPhysical;out[12]=t.tables;out[13]=t.bytes;out[14]=t.capacity;
  out[17]=initialAck;out[18]=initialResult!=nullptr;out[19]=postChecked;out[20]=acknowledged();
  out[21]=initialRows;out[22]=contextRows;out[23]=rowCount;out[24]=initialResult?initialResult->rpc.txWriter:0;
  for(unsigned i=0;i<2;++i){out[32+i*4]=invalidations[i].passed;out[33+i*4]=unsigned(invalidations[i].failure);out[34+i*4]=invalidations[i].commandAttempted;out[35+i*4]=unsigned(publications[i].phase);}
  if(initialResult){const auto&r=initialResult->rpc;
   out[40]=r.failure;out[41]=r.attempted;out[42]=r.count;out[43]=r.bytes;out[44]=r.initialReader;out[45]=r.initialSequence;
   out[46]=r.rxReader;out[47]=r.rxProducer;out[48]=r.rxSequence;out[49]=r.lastFunction;out[50]=r.lastResult;
   out[51]=r.txReader;out[52]=r.sent;out[53]=r.completed;out[54]=r.doorbells;
  }
  out[55]=rowCount;out[56]=arena.info().buffers;out[57]=arena.info().dataPages;out[58]=arena.info().logicalBytes;out[59]=acknowledged();out[60]=generation;out[61]=DataBegin;out[62]=DataEnd;
 }
 bool capture(uint64_t part,uint64_t off,void*out,uint64_t n)const{
  if(!out||!n||n>4096)return false;
  if(part==2)return arena.readTree(off,out,n);if(part==3)return arena.captureRows(off,out,n);
  const uint8_t*p=nullptr;uint64_t size=0;
  if(part==0&&initialRequests&&initialResult&&initialResult->rpc.attempted&&initialResult->rpc.step==4){p=initialRequests+4*4096;size=4096;}
  else if(part==1&&initialResult){p=initialRecords;size=initialResult->rpc.bytes;}
  else if(part==4&&rowCount){p=sourceRoot;size=12288;}else if(part==5&&rowCount){p=sourceChildren;size=childBytes;}
  else if(part==6&&acknowledged()){p=reinterpret_cast<const uint8_t*>(handles);size=sizeof(handles);}
  else if(part==7){if(off>64||n>64-off)return false;auto*q=static_cast<uint8_t*>(out);for(uint64_t i=0;i<n;++i){auto at=off+i;q[i]=at<32?initialDigest[at]:preparedDigest[at-32];}return true;}
  if(!p||off>size||n>size-off)return false;bcopy(p+off,out,size_t(n));return true;
 }
 bool page(uint64_t i,uint64_t*out)const{uint64_t pa=0,extent=0;if(i>=arena.treeInfo().capacity||!arena.tablePage(uint32_t(i),pa,extent))return false;out[0]=i;out[1]=pa;out[2]=extent;return true;}
 void dataInfo(uint64_t registry,uint64_t*out)const{dataAccess.info(registry,out);}
 bool dataRetained()const{return dataAccess.retained();}
 template<class Ready>RTXDataAccess181::Error dataCall(unsigned selector,const RTXDataABI181::Call&call,Ready&ready){
  if(!acknowledged()||!spanOwner)return RTXDataAccess181::Error::State;
  struct IO {
   MacOwnedRoot195&s;Ready&live;
   bool ready(){return s.acknowledged()&&!s.dataRetained()&&s.gpu.idle()&&s.graphics&&s.graphics->idle()&&live();}
   bool mapping(uint32_t i,uint64_t&scope,RTXSpans165::Mapping&m){return s.arena.preparedSpan(i,scope,m);}
   bool stable(uint32_t i,uint64_t off,uint64_t n){return s.arena.mappedStable(i,off,n);}
   bool write(uint32_t i,uint64_t off,const void*p,uint64_t n){return ready()&&s.arena.writeMapped(i,off,p,n);}
   bool publish(uint32_t i){return ready()&&s.arena.publishMapped(i);}
   bool import(uint32_t i){return ready()&&s.arena.importMapped(i);}
   bool read(uint32_t i,uint64_t off,void*p,uint64_t n){return ready()&&s.arena.readMapped(i,off,p,n);}
   bool page(uint32_t i,uint32_t p,uint64_t&cached,uint64_t&physical,uint64_t&extent){return ready()&&s.arena.mappedPage(i,p,cached,physical,extent);}
  }io{*this,ready};
  return RTXDataABI181::dispatch(dataAccess,*spanOwner,io,selector,call);
 }

 bool gpuActive()const{return gpu.active()||(graphics&&graphics->active());}
 uint64_t gpuCompleted()const{return gpu.completed();}
 bool gpuIdle()const{return gpu.idle()&&(!graphics||graphics->idle());}
 bool gpuRetained()const{return gpu.retained()||(graphics&&graphics->retained());}
 void gpuInfo(uint64_t registry,uint64_t*out)const{gpu.info(registry,out);}
 void gpuProgramInfo205(uint64_t registry,uint64_t*out)const{gpu.programInfo205(registry,out);}
 bool gpuCapture(unsigned part,uint64_t off,void*out,uint64_t n)const{return gpu.captureBytes(part,off,out,n);}
 bool gpuRootStable(MacReusableBackend035&owner){
  if(!acknowledged()||!owner.runtimeReady(generation,client)||!pagesStable()||!arena.importTreeForObservation()||!imageDigest(observedDigest)||
     !RTXOwnedDispatch183::equal(observedDigest,preparedDigest,32))return false;
  const auto start=owner.nowNs();
  for(unsigned which=0;which<2;++which){const unsigned bytes=which?childBytes:12288,base=which?0x1005000:0x1002000;const auto*golden=which?sourceChildren:sourceRoot;
   for(unsigned off=0;off<bytes;off+=4096){const auto now=owner.nowNs();
    if(now<start||now-start>=RTXOwnedDispatch183::BudgetNs||!owner.runtimeReady(generation,client)||!owner.readMemory(base+off,scratch,4096)||
       !RTXOwnedDispatch183::equal(scratch,golden+off,4096))return false;
   }
  }
  return true;
 }
 template<class Ready>RTXOwnedDispatch183::Error gpuCall(unsigned selector,const RTXOwnedDispatchABI183::Call&call,
   Ready&ready,MacReusableBackend035&native,MacGSPContext&context,MacChannelMemoryMapping&map,
   RtxReusableRuntime035::State&runtime,const ExecutionTransactions::Result&execution){
  using E=RTXOwnedDispatch183::Error;
  if(!spanOwner||!acknowledged()||(graphics&&graphics->active()))return E::State;
  // Existing ABI1 jobs and resident replacements are excluded once Begin is
  // accepted. The legacy bootstrap owner remains alive as the native lease.
  if(selector==RTXOwnedDispatchABI183::Begin&&runtime.core.completed()!=0)return E::State;
  struct IO {
   MacOwnedRoot195&s;Ready&live;MacReusableBackend035&native;MacGSPContext&context;MacChannelMemoryMapping&map;
   RtxReusableRuntime035::State&runtime;const ExecutionTransactions::Result&execution;
   bool saved=false,attempted=false,selected=false,restoreAttempted=false;unsigned original=~0u;
   bool ready(){return s.acknowledged()&&!s.dataRetained()&&live()&&native.runtimeReady(s.generation,s.client);}
   uint64_t nowNs(){return native.nowNs();}void delayUs(unsigned us){native.delayUs(us);}
   bool mapping(unsigned i,uint64_t&scope,RTXSpans165::Mapping&m){return ready()&&s.arena.preparedSpan(i,scope,m);}
   bool stable(unsigned i,uint64_t off,uint64_t n){return ready()&&s.arena.mappedStable(i,off,n);}
   bool import(unsigned i){return ready()&&s.arena.importMapped(i);}
   bool selectWindow(){
    if(!ready()||!s.gpu.running()||!native.readWindow(original)||RtxReusableRuntime035::badWindow(original)||original!=native.originalWindow())return false;
    saved=true;attempted=true;unsigned after=~0u;
    if(!native.writeWindow(0)||!ready()||!native.readWindow(after)||after)return false;selected=true;return true;
   }
   bool restoreWindow(){
    if(restoreAttempted)return false;restoreAttempted=true;if(!attempted)return false;
    const auto start=nowNs();unsigned after=~0u;
    if(!saved||!ready()||!native.writeWindow(original)||!ready()||!native.readWindow(after)||after!=original)return false;
    const auto end=nowNs();selected=false;return end>=start&&end-start<RTXOwnedDispatch183::BudgetNs;
   }
   bool read(unsigned a,uint8_t*out,unsigned n){return ready()&&selected&&context.read(ChannelMemory::Window)==0&&native.readMemory(a,out,n);}
   bool write(unsigned a,const uint8_t*p,unsigned n){
    return ready()&&selected&&s.gpu.allowedWrite(a,p,n)&&context.read(ChannelMemory::Window)==0&&
     RtxReusableRuntime035::readable(runtime,a,n)&&map.span(a,n)&&map.write(a,p,n);
   }
   bool notify(){
    unsigned token=0;return ready()&&selected&&s.gpu.mayNotify()&&context.read(ChannelMemory::Window)==0&&
     WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,token)&&token==execution.candidate&&context.write(HostFence::Doorbell,token);
   }
   bool rootStable(){return ready()&&selected&&s.gpuRootStable(native);}
   bool baseline(const uint8_t*device){
    return ready()&&runtime.core.completed()==0&&runtime.backing.replacementBefore(0,
     {runtime.storage.captureRoot,runtime.storage.captureChildren,device,runtime.storage.childBytes});
   }
  }io{*this,ready,native,context,map,runtime,execution};
  return RTXOwnedDispatchABI183::dispatch(gpu,*spanOwner,io,generation,selector,call);
 }

 bool graphicsActive()const{return graphics&&graphics->active();}
 uint64_t graphicsCompleted()const{return graphics?graphics->completed():0;}
 bool graphicsRetained()const{return graphics&&graphics->retained();}
 void graphicsInfo(uint64_t registry,uint64_t*out)const{if(graphics)graphics->info(registry,out);else RTXGraphicsABI242::coldInfo(registry,out);}
 bool graphicsCapture(unsigned part,uint64_t off,void*out,uint64_t n)const{return graphics&&graphics->capture(part,off,out,n);}
 template<class Ready>RTXGraphicsSubmit241::Error graphicsCall(const RTXGraphicsABI242::Call&call,Ready&ready,
   MacReusableBackend035&native,MacGSPContext&context,MacChannelMemoryMapping&map,RtxReusableRuntime035::State&runtime,
   const ExecutionTransactions::Result&execution){
  using E=RTXGraphicsSubmit241::Error;
  if(RTXGraphicsLayout242::ABI!=242||!spanOwner||!graphics||!acknowledged()||gpu.active()||runtime.core.completed()!=0)return E::State;
  // All fourteen verified replies include successful C797 allocation on the
  // same synchronous VEID0 channel used by this retained runtime connection.
  const auto&r=execution.rpc;
  if(!r.passed||r.failure||r.completed!=ExecutionCodec::Steps||r.sent!=ExecutionCodec::Steps||
     r.txWriter!=ExecutionCodec::FinalProducer||r.txReader!=ExecutionCodec::FinalProducer||!execution.contextPrepared||execution.graphicsCaps!=0)return E::State;
  struct IO {
   MacOwnedRoot195&s;Ready&live;MacReusableBackend035&native;MacGSPContext&context;MacChannelMemoryMapping&map;
   RtxReusableRuntime035::State&runtime;const ExecutionTransactions::Result&execution;
   bool saved=false,attempted=false,selected=false,restoreAttempted=false;unsigned original=~0u;
   bool ready(){return s.acknowledged()&&!s.dataRetained()&&!s.gpu.active()&&live()&&native.runtimeReady(s.generation,s.client)&&runtime.core.completed()==0;}
   uint64_t nowNs(){return native.nowNs();}void delayUs(unsigned us){native.delayUs(us);}
   bool roleSlot(unsigned role,unsigned&slot){RTXSpans165::Mapping m{};
    if(!s.graphics||!s.graphics->roleMapping(role,m)||role>=5)return false;slot=RTXGraphicsLayout242::graphicsSlot(role);
    const auto&d=RTXGraphicsLayout242::Description[slot];
    return slot<RTXGraphicsLayout242::Buffers&&m.allocation==slot+1&&m.mapping==slot+1&&m.gpuVA==RTXGraphicsLayout242::address(slot)&&m.logicalBytes==d.bytes&&m.access==d.access;
   }
   bool mapping(unsigned slot,uint64_t&scope,RTXSpans165::Mapping&m){return ready()&&slot>=4&&slot<9&&s.arena.preparedSpan(slot,scope,m);}
   bool stable(unsigned role,uint64_t off,uint64_t n){unsigned slot=0;return ready()&&roleSlot(role,slot)&&s.arena.mappedStable(slot,off,n);}
   bool importBuffer(unsigned role){unsigned slot=0;return ready()&&roleSlot(role,slot)&&s.arena.importMapped(slot);}
   bool publishBuffer(unsigned role){unsigned slot=0;return ready()&&roleSlot(role,slot)&&s.arena.publishMapped(slot);}
   bool readBuffer(unsigned role,uint64_t off,void*out,unsigned n){unsigned slot=0;return ready()&&roleSlot(role,slot)&&s.arena.readMapped(slot,off,out,n);}
   bool writeBuffer(unsigned role,uint64_t off,const void*p,unsigned n){unsigned slot=0;
    return ready()&&s.graphics->allowsData(role,off,p,n)&&roleSlot(role,slot)&&s.arena.writeMapped(slot,off,p,n);
   }
   bool selectWindow(){
    if(!ready()||!s.graphics->busy()||!native.readWindow(original)||RtxReusableRuntime035::badWindow(original)||original!=native.originalWindow())return false;
    saved=true;attempted=true;unsigned after=~0u;
    if(!native.writeWindow(0)||!ready()||!native.readWindow(after)||after)return false;selected=true;return true;
   }
   bool restoreWindow(){
    if(restoreAttempted)return false;restoreAttempted=true;if(!attempted)return false;
    const auto start=nowNs();unsigned after=~0u;
    if(!saved||!ready()||!native.writeWindow(original)||!ready()||!native.readWindow(after)||after!=original)return false;
    const auto end=nowNs();selected=false;return end>=start&&end-start<RTXGraphicsSubmit241::BudgetNs;
   }
   bool readControl(unsigned a,void*out,unsigned n){return ready()&&selected&&context.read(ChannelMemory::Window)==0&&native.readMemory(a,static_cast<uint8_t*>(out),n);}
   bool writeControl(unsigned a,const void*p,unsigned n){
    return ready()&&selected&&s.graphics->allowsControl(a,p,n)&&context.read(ChannelMemory::Window)==0&&
     RtxReusableRuntime035::readable(runtime,a,n)&&map.span(a,n)&&map.write(a,static_cast<const uint8_t*>(p),n);
   }
   bool notify(){unsigned token=0;
    return ready()&&selected&&s.graphics->mayNotify()&&context.read(ChannelMemory::Window)==0&&
     WorkSubmitToken::compose(execution.runlist,ExecutionPlan::HardwareChannelId,execution.rawToken,token)&&token==execution.candidate&&context.write(HostFence::Doorbell,token);
   }
   bool rootStable(){return ready()&&selected&&s.gpuRootStable(native);}
   bool baseline(const uint8_t*device){return ready()&&runtime.backing.replacementBefore(0,{runtime.storage.captureRoot,runtime.storage.captureChildren,device,runtime.storage.childBytes});}
  }io{*this,ready,native,context,map,runtime,execution};
  return RTXGraphicsABI242::dispatch(*graphics,*spanOwner,io,generation,RTXGraphicsABI242::Submit,call);
 }

};

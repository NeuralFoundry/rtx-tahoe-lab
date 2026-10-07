#pragma once
#include "ResidentProgram.hpp"
#include "ReusableRuntime.hpp"

namespace RtxResident058 {
namespace R=RtxReusableRuntime035;namespace N=RtxReusable035;namespace B=RtxReusableBacking035;
// Called under the same sleepable native service mutex as dispatch/capture.
// Every existing job has completed both QMD release and HOST retirement. The
// next dispatch uses the existing instruction/global/constant invalidations.
template<class Backend>bool replace(Backend &native,R::State &runtime,State &resident,const Scope &scope){
 if(!scope.ready||!runtime.opened||runtime.closed||runtime.core.phase()!=N::Phase::Ready||runtime.backing.phase()!=B::Phase::Ready||
    scope.generation!=runtime.generation||scope.client!=runtime.client||scope.completed!=runtime.core.completed()||
    scope.completed!=runtime.backing.completed()||runtime.storage.library!=resident.library()||runtime.storage.code!=resident.code()||
    !R::bound(runtime,runtime.storage)||!P::separate(&resident,sizeof(resident),&runtime,sizeof(runtime))||!resident.enter(scope))return false;
 auto &result=resident.applyingResult();result.started=native.nowNs();
 const auto live=[&](){return !runtime.closed&&runtime.core.phase()==N::Phase::Ready&&runtime.backing.phase()==B::Phase::Ready&&native.runtimeReady(scope.generation,scope.client);};
 const auto tick=[&](){
  if(!live())return false;const auto now=native.nowNs();if(now<result.started||now-result.started<result.elapsed)return false;
  result.elapsed=now-result.started;return result.elapsed<N::BudgetNs;
 };
 const auto capture=[&](){
  runtime.activeCapture=5;
  for(unsigned part=0;part<3;++part){
   const unsigned size=part==0?12288:part==1?runtime.storage.childBytes:B::DeviceBytes;
   auto *out=part==0?runtime.storage.captureRoot:part==1?runtime.storage.captureChildren:runtime.storage.captureDevice;
   for(unsigned off=0;off<size;off+=4096){
    const unsigned address=part==0?R::RootPhysical+off:part==1?R::ChildrenPhysical+off:R::Pages[off/4096];
    if(!tick())return false;++result.reads;
    if(!native.readMemory(address,out+off,4096)||!tick())return false;
   }
  }
  return true;
 };
 const auto work=[&](){
  if(!tick()||!native.readWindow(result.windowBefore)||!tick()||R::badWindow(result.windowBefore)||result.windowBefore!=native.originalWindow())return false;
  result.saved=true;result.windowAttempted=true;
  if(!native.writeWindow(0)||!tick())return false;unsigned selected=~0U;
  if(!native.readWindow(selected)||!tick()||selected)return false;
  if(!capture()||!runtime.backing.replacementBefore(scope.completed,R::view(runtime)))return false;
  const auto *device=runtime.storage.captureDevice;
  const unsigned next=N::nextIndex(scope.completed);
  if(P::get32(device+0x888)!=next||P::get32(device+0x88c)!=next||P::get64(device+B::Fence)!=scope.completed||P::get64(device+B::Fence+16)!=scope.completed)return false;
  if(!tick())return false;result.exposed=true;++result.writes;
  if(!native.writeResidentMemory(P::Q::ProgramPhysical,resident.candidateCode(),4096)||!tick())return false;
  return capture()&&runtime.backing.replacementStaged(scope.completed,R::view(runtime),resident.candidateCode())&&tick();
 };
 const bool done=work();
 if(result.windowAttempted){
  result.restoreAttempted=true;const auto started=native.nowNs();uint64_t elapsed=0;
  const auto cleanupTick=[&](){
   if(!live())return false;const auto now=native.nowNs();if(now<started||now-started<elapsed)return false;elapsed=now-started;return elapsed<N::BudgetNs;
  };
  if(result.saved&&started>=result.started&&started-result.started>=result.elapsed&&cleanupTick()&&native.writeWindow(result.windowBefore)&&cleanupTick()&&native.readWindow(result.windowAfter)&&cleanupTick()&&result.windowAfter==result.windowBefore)result.restored=true;
 }
 if(done&&result.restored&&tick()&&runtime.backing.replacementCommit(scope.completed,R::view(runtime),resident.candidateCode())&&resident.commit(scope))return true;
 resident.retain();runtime.closed=true;runtime.core.ownershipLost();runtime.backing.retain();native.retain();return false;
}
}

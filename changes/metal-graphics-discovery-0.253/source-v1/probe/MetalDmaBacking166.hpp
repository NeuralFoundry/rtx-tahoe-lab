#pragma once
#include <stdint.h>
#include <stddef.h>
namespace RTXBacking166 {
constexpr uint64_t Page=4096,AddressLimit=1ULL<<40,MaxBufferBytes=64ULL<<20;
enum class Step:uint32_t {None,Shape,Mapper,Pages,Scratch,Memory,Zero,Dma,MemoryPrepare,Attach,DmaPrepare,Enumerate,Validate,Sync,Ready,Complete,Clear,MemoryComplete,Released};
struct Info {
 uint64_t logical=0,mapped=0,writeEpoch=0,publishedEpoch=0;
 uint32_t pages=0,error=0,cleanupError=0;Step step=Step::None;
 bool started=false,pagesAllocated=false,scratchAllocated=false,memoryAllocated=false,dmaAllocated=false;
 bool memoryPrepared=false,attached=false,dmaPrepared=false,ready=false,exposed=false,cleanupAttempted=false,cleanupSucceeded=false;
};
inline bool range(uint64_t offset,uint64_t bytes,uint64_t limit){return bytes&&offset<=limit&&bytes<=limit-offset;}
inline void sift(uint64_t *p,uint32_t at,uint32_t count){while(at*2+1<count){uint32_t child=at*2+1;if(child+1<count&&p[child]<p[child+1])++child;if(p[at]>=p[child])break;uint64_t t=p[at];p[at]=p[child];p[child]=t;at=child;}}
inline bool validPages(const uint64_t *p,uint64_t *scratch,uint32_t count){
 if(!p||!scratch||!count||count>MaxBufferBytes/Page)return false;
 const uintptr_t a=reinterpret_cast<uintptr_t>(p),b=reinterpret_cast<uintptr_t>(scratch);const size_t bytes=size_t(count)*8;
 if(a>UINTPTR_MAX-bytes||b>UINTPTR_MAX-bytes||(a<b+bytes&&b<a+bytes))return false;
 for(uint32_t i=0;i<count;++i){if(p[i]<Page||p[i]%Page||p[i]>AddressLimit-Page)return false;scratch[i]=p[i];}
 for(uint32_t i=count/2;i;--i)sift(scratch,i-1,count);
 for(uint32_t n=count;n>1;--n){uint64_t t=scratch[0];scratch[0]=scratch[n-1];scratch[n-1]=t;sift(scratch,0,n-1);}
 for(uint32_t i=1;i<count;++i)if(scratch[i]==scratch[i-1])return false;return true;
}
// The native adapter supplies actual IOKit operations. A CPU backend tests
// failure paths but cannot establish physical DMA ownership or GPU access.
class Lifetime {
 Info m;
 bool fail(Step step,uint32_t error){m.step=step;m.error=error?error:1;m.ready=false;return false;}
public:
 const Info&info()const{return m;}
 bool held()const{return m.pagesAllocated||m.scratchAllocated||m.memoryAllocated||m.dmaAllocated;}
 template<class B> bool prepare(B &b,uint64_t logical){
  if(m.started||m.cleanupAttempted)return false;m.started=true;
  if(!logical||logical>MaxBufferBytes)return fail(Step::Shape,1);
  m.logical=logical;m.mapped=(logical+Page-1)&~(Page-1);m.pages=uint32_t(m.mapped/Page);uint32_t e=0;
  if((e=b.mapperReady()))return fail(Step::Mapper,e);
  if((e=b.allocatePages(m.pages)))return fail(Step::Pages,e);m.pagesAllocated=true;
  if((e=b.allocateScratch(m.pages)))return fail(Step::Scratch,e);m.scratchAllocated=true;
  if((e=b.allocateMemory(m.mapped)))return fail(Step::Memory,e);m.memoryAllocated=true;
  if((e=b.zero(m.mapped)))return fail(Step::Zero,e);
  if((e=b.createDma(m.mapped)))return fail(Step::Dma,e);m.dmaAllocated=true;
  if((e=b.prepareMemory()))return fail(Step::MemoryPrepare,e);m.memoryPrepared=true;
  if((e=b.attach()))return fail(Step::Attach,e);m.attached=true;
  if((e=b.prepareDma(m.mapped)))return fail(Step::DmaPrepare,e);m.dmaPrepared=true;
  if((e=b.enumerate(m.pages)))return fail(Step::Enumerate,e);
  if(!validPages(b.pages(),b.scratch(),m.pages))return fail(Step::Validate,1);
  if((e=b.synchronizeOut()))return fail(Step::Sync,e);
  m.writeEpoch=m.publishedEpoch=1;m.ready=true;m.step=Step::Ready;return true;
 }
 bool copiedIn(){if(!m.ready||m.cleanupAttempted||m.writeEpoch==UINT64_MAX)return false;++m.writeEpoch;return true;}
 bool canWrite()const{return m.ready&&!m.cleanupAttempted&&m.writeEpoch!=UINT64_MAX;}
 template<class B> bool publish(B&b){if(!m.ready||m.cleanupAttempted)return false;uint32_t e=b.synchronizeOut();if(e)return fail(Step::Sync,e);m.publishedEpoch=m.writeEpoch;return true;}
 bool expose(){if(!m.ready||m.cleanupAttempted||m.publishedEpoch!=m.writeEpoch)return false;m.exposed=true;return true;}
 // There is deliberately no unexpose/reset shortcut. Once pages may be used
 // by a device they cannot be released by this host-only preparation API.
 template<class B> bool cleanup(B &b){
  if(m.exposed)return false;if(m.cleanupAttempted)return m.cleanupSucceeded;m.cleanupAttempted=true;m.ready=false;uint32_t e=0;
  if(m.dmaPrepared){if((e=b.completeDma())){m.cleanupError=e;m.step=Step::Complete;return false;}m.dmaPrepared=false;}
  // clear also handles partial setMemoryDescriptor/prepare failure.
  if(m.dmaAllocated){if((e=b.clear())){m.cleanupError=e;m.step=Step::Clear;return false;}m.attached=false;}
  if(m.memoryPrepared){if((e=b.completeMemory())){m.cleanupError=e;m.step=Step::MemoryComplete;return false;}m.memoryPrepared=false;}
  if(m.dmaAllocated){b.releaseDma();m.dmaAllocated=false;}
  if(m.memoryAllocated){b.releaseMemory();m.memoryAllocated=false;}
  if(m.scratchAllocated){b.releaseScratch(m.pages);m.scratchAllocated=false;}
  if(m.pagesAllocated){b.releasePages(m.pages);m.pagesAllocated=false;}
  m.cleanupSucceeded=true;m.step=Step::Released;return true;
 }
};
}

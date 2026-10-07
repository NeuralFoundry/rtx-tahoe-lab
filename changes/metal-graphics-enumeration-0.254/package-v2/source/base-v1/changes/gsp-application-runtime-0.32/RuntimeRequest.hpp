#pragma once
#include <stdint.h>
#include <stddef.h>

// Portable request codec shared by an application and a future KEXT or DEXT
// adapter. No IOKit, DriverKit, allocation, device addresses or device I/O.
namespace RtxApplication032 {
constexpr size_t RequestBytes=576;
constexpr unsigned Elements=64,Slots=4;
constexpr uint64_t RequestMagic=UINT64_C(0x5254584a4f423332);
constexpr uint32_t Version=1,AddU32=1;
struct Request {
 uint64_t generation=0,requestId=0;
 uint32_t count=0,a[Elements]={},b[Elements]={};
};
inline uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(8*i);return v;}
inline uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline bool separate(const void *a,size_t an,const void *b,size_t bn){
 if(!a||!b)return false;const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 return an<=UINTPTR_MAX-x&&bn<=UINTPTR_MAX-y&&!(x<y+bn&&y<x+an);
}
inline bool valid(const Request &r){
 if(!r.generation||!r.requestId||r.requestId>Slots||r.count>Elements)return false;
 // A single canonical encoding prevents unused application bytes from becoming
 // untracked future shader inputs. A zero-count job still has a completion.
 for(unsigned i=r.count;i<Elements;++i)if(r.a[i]||r.b[i])return false;
 return true;
}
inline bool encode(const Request &r,uint8_t *out,size_t bytes){
 if(bytes!=RequestBytes||!valid(r)||!separate(&r,sizeof(r),out,bytes))return false;
 for(size_t i=0;i<bytes;++i)out[i]=0;
 put64(out,RequestMagic);put32(out+8,Version);put32(out+12,uint32_t(RequestBytes));
 put64(out+16,r.generation);put64(out+24,r.requestId);put32(out+32,r.count);put32(out+36,AddU32);
 for(unsigned i=0;i<Elements;++i){put32(out+64+i*4,r.a[i]);put32(out+320+i*4,r.b[i]);}
 return true;
}
inline bool decode(const uint8_t *in,size_t bytes,Request &out){
 if(bytes!=RequestBytes||!separate(in,bytes,&out,sizeof(out))||get64(in)!=RequestMagic||
    get32(in+8)!=Version||get32(in+12)!=RequestBytes||get32(in+36)!=AddU32)return false;
 const uint64_t gen=get64(in+16),id=get64(in+24);const uint32_t count=get32(in+32);
 if(!gen||!id||id>Slots||count>Elements)return false;
 for(unsigned i=40;i<64;++i)if(in[i])return false;
 for(unsigned i=count;i<Elements;++i)if(get32(in+64+i*4)||get32(in+320+i*4))return false;
 // Validate first; a rejected call leaves the caller-owned destination intact.
 out.generation=gen;out.requestId=id;out.count=count;
 for(unsigned i=0;i<Elements;++i){out.a[i]=get32(in+64+i*4);out.b[i]=get32(in+320+i*4);}
 return true;
}
constexpr uint32_t completion(unsigned slot){return 0x306032f0u+slot;}

// Serialized service-side state. This is not a hardware ownership oracle.
// Only an adapter with fresh bootstrap/provider/mapping evidence may open it.
// The caller identity comes from the authenticated service connection, never
// from the serialized request. No transition authorizes buffer release.
enum class Phase : uint32_t { Empty,Ready,Accepted,Exposed,Submitted,Exhausted,Retained };
enum class Error : uint32_t { Ok,Shape,Identity,Order,State,Evidence };
struct BootstrapEvidence {
 uint64_t generation=0,client=0;
 bool ownerHeld=false,firmwareReady=false,hostQueueVerified=false,storageReady=false,windowRestored=false;
};
struct CompletionEvidence {
 uint64_t generation=0,requestId=0;
 uint32_t gpGet=0,gpPut=0,marker=0;
 bool ownerHeld=false,immutableVerified=false,guardsVerified=false,windowRestored=false;
};
class Session {
 Phase phase_=Phase::Empty;
 uint64_t generation_=0,client_=0;
 unsigned completed_=0;
 Request active_;
public:
 Session()=default;
 Session(const Session &)=delete;
 Session &operator=(const Session &)=delete;
 Session(Session &&)=delete;
 Session &operator=(Session &&)=delete;
 Phase phase()const{return phase_;}
 unsigned completed()const{return completed_;}
 uint64_t generation()const{return generation_;}
 uint64_t client()const{return client_;}
 const Request &active()const{return active_;}
 bool open(const BootstrapEvidence &e){
  if(phase_!=Phase::Empty||!e.generation||!e.client||!e.ownerHeld||!e.firmwareReady||
     !e.hostQueueVerified||!e.storageReady||!e.windowRestored)return false;
  generation_=e.generation;client_=e.client;phase_=Phase::Ready;return true;
 }
 Error accept(uint64_t caller,const uint8_t *bytes,size_t size){
  if(phase_==Phase::Empty||caller!=client_)return Error::Identity;
  if(phase_!=Phase::Ready)return Error::State;
  if(!bytes||size!=RequestBytes)return Error::Shape;
  // The service must provide a readable span, never an application address.
  // Snapshot before validation so subsequent parsing uses only our own bytes.
  uint8_t snapshot[RequestBytes];for(size_t i=0;i<RequestBytes;++i)snapshot[i]=bytes[i];
  Request parsed;
  if(!decode(snapshot,sizeof(snapshot),parsed))return Error::Shape;
  if(parsed.generation!=generation_)return Error::Identity;
  if(parsed.requestId!=uint64_t(completed_)+1)return Error::Order;
  active_=parsed;phase_=Phase::Accepted;return Error::Ok;
 }
 // Invoke before the first possibly effective BAR/queue/register mutation.
 bool beginMutation(uint64_t caller){
  if(caller!=client_||phase_!=Phase::Accepted)return false;
  phase_=Phase::Exposed;return true;
 }
 bool submitted(uint64_t caller){
  if(caller!=client_||phase_!=Phase::Exposed)return false;
  phase_=Phase::Submitted;return true;
 }
 bool finish(uint64_t caller,const CompletionEvidence &e,const uint32_t *outputs,size_t count){
  if(caller!=client_)return false;
  if(phase_!=Phase::Submitted)return false;
  bool passed=outputs&&count==Elements&&e.generation==generation_&&e.requestId==active_.requestId&&
   e.gpGet==completed_+2&&e.gpPut==completed_+2&&e.marker==completion(completed_)&&
   e.ownerHeld&&e.immutableVerified&&e.guardsVerified&&e.windowRestored;
  if(passed)for(unsigned i=0;i<Elements;++i){
   const uint32_t sum=active_.a[i]+active_.b[i];
   if(outputs[i]!=(i<active_.count?sum:~sum)){passed=false;break;}
  }
  if(!passed){phase_=Phase::Retained;return false;}
  ++completed_;phase_=completed_==Slots?Phase::Exhausted:Phase::Ready;return true;
 }
 // Client death/owner loss/backend error after acceptance retires the session.
 // An unrelated caller cannot close the active owner's session.
 bool close(uint64_t caller){
  if(phase_==Phase::Empty||caller!=client_)return false;
  phase_=Phase::Retained;return true;
 }
 void ownershipLost(){if(phase_!=Phase::Empty)phase_=Phase::Retained;}
};
}

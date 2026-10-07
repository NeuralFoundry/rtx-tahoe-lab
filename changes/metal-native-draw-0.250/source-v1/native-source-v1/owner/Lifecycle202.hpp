#pragma once
#include <cstdint>
// All transitions occur on the broker's serial queue. Ready may publish the
// Metal child. Withdrawal must remove that publication and readiness before
// any native retirement, including failures inside Core.receive.
namespace RTXOwnedLifecycle202 {
enum Event: uint32_t {Ready=1,Withdraw=2};
using Function=int(*)(void*,uint32_t);
struct Callbacks {uint32_t abi=202,bytes=sizeof(Callbacks);void*context=nullptr;Function function=nullptr;uint64_t reserved=0;};
static_assert(sizeof(Callbacks)==32,"64-bit lifecycle ABI");
inline bool valid(const Callbacks&c){return c.abi==202&&c.bytes==sizeof(Callbacks)&&c.function&&!c.reserved;}
class State {
 Callbacks callbacks_{};bool readyAttempted_=false,activated_=false,withdrawAttempted_=false,withdrawn_=false,blocked_=false;
 bool invoke(Event e)noexcept{try{return callbacks_.function&&callbacks_.function(callbacks_.context,e)==0;}catch(...){return false;}}
public:
 State()=default;explicit State(Callbacks c):callbacks_(c){}
 bool activate()noexcept{if(!valid(callbacks_)||readyAttempted_||withdrawAttempted_||withdrawn_||blocked_)return false;readyAttempted_=true;activated_=invoke(Ready);return activated_;}
 bool withdraw()noexcept{
  if(blocked_)return false;if(withdrawn_)return true;if(withdrawAttempted_){blocked_=true;return false;}
  if(!readyAttempted_){withdrawn_=true;return true;}
  withdrawAttempted_=true;withdrawn_=invoke(Withdraw);if(!withdrawn_)blocked_=true;return withdrawn_;
 }
 void quarantine()noexcept{blocked_=true;}
 bool blocked()const{return blocked_;}bool readyAttempted()const{return readyAttempted_;}bool activated()const{return activated_;}
 bool withdrawAttempted()const{return withdrawAttempted_;}bool withdrawn()const{return withdrawn_;}
};
}
extern "C" int rtx_owned_broker_serve202(const char*,uint32_t,void*,void*,const char*,uint32_t,uint32_t,const RTXOwnedLifecycle202::Callbacks*);
extern "C" int rtx_owned_broker_stop202();
extern "C" int rtx_owned_broker_close_permitted202();

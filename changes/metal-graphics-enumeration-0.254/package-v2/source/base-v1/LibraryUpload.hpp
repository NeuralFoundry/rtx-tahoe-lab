#pragma once
#include "changes/gsp-program-library-0.33/ProgramLibrary.hpp"
#include "driver/GSPDigest.hpp"

// CPU-only admission for a compiler-reviewed sm_86 library. All bytes are
// service-owned before bootstrap; no caller pointers, allocations or device I/O.
// SHA-256 binds the upload to the caller's chosen bytes, not to a trusted signer.
// Container validity is NOT a SASS validator or proof of shader memory safety.
// The native entry must supply a fresh Scope under its service mutex on every
// operation, and retain this object while any bootstrap/runtime pointer exists.
namespace RtxLibraryUpload036 {
namespace P=RtxProgram033;
constexpr unsigned HeaderBytes=128,PayloadBytes=P::WireBytes+P::CodeBytes,ChunkBytes=1024,Chunks=5,InfoBytes=256;
constexpr uint64_t HeaderMagic=UINT64_C(0x52545855504c3336),InfoMagic=UINT64_C(0x52545855494e3336);
enum class Phase:unsigned {Empty,Uploading,Ready,Consumed,Rejected,Closed};
enum class Error:unsigned {None,Identity,State,Scope,Shape,Order,Incomplete,Digest,Library,Profile};
struct Scope {uint64_t generation=0,client=0;bool preparationAllowed=false;};
inline bool equal(const uint8_t *a,const uint8_t *b,unsigned n){for(unsigned i=0;i<n;++i)if(a[i]!=b[i])return false;return true;}
inline void copy(uint8_t *a,const uint8_t *b,unsigned n){for(unsigned i=0;i<n;++i)a[i]=b[i];}
// Reject libraries this current 64-element runtime cannot launch, even if the
// general descriptor format can express a larger local size.
inline bool profile(const P::Library &lib){
 for(unsigned i=0;i<lib.count;++i)if(lib.programs[i].localX>64)return false;
 return true;
}
class State {
 uint8_t header_[HeaderBytes]={},payload_[PayloadBytes]={},actualDigest_[32]={};
 uint64_t generation_=0,client_=0;unsigned written_=0,chunks_=0;
 Phase phase_=Phase::Empty;Error terminalError_=Error::None;bool sealed_=false;
 bool sameOwner(const Scope &s)const{return s.generation&&s.client&&s.generation==generation_&&s.client==client_;}
 Error reject(Error e){phase_=Phase::Rejected;terminalError_=e;return e;}
 Error editable(const Scope &s)const{
  if(!sameOwner(s))return Error::Identity;
  if(phase_!=Phase::Uploading)return Error::State;
  return s.preparationAllowed?Error::None:Error::Scope;
 }
public:
 State()=default;State(const State &)=delete;State &operator=(const State &)=delete;
 Phase phase()const{return phase_;}unsigned written()const{return written_;}unsigned chunks()const{return chunks_;}
 uint64_t generation()const{return generation_;}Error terminalError()const{return terminalError_;}
 Error begin(const Scope &s,const uint8_t *header,size_t n){
  if(!s.generation||!s.client)return Error::Identity;
  if(phase_!=Phase::Empty)return Error::State;
  if(!s.preparationAllowed)return Error::Scope;
  if(n!=HeaderBytes||!P::separate(header,n,this,sizeof(*this))||!P::separate(&s,sizeof(s),this,sizeof(*this)))return Error::Shape;
  // No state mutation on a malformed begin. Native inline IPC has already
  // copied caller bytes; this local copy removes dependencies on that storage.
  uint8_t h[HeaderBytes];copy(h,header,HeaderBytes);
  if(P::get64(h)!=HeaderMagic||P::get32(h+8)!=1||P::get32(h+12)!=HeaderBytes||P::get64(h+16)!=s.generation||
     P::get32(h+24)!=PayloadBytes||P::get32(h+28)!=ChunkBytes||P::get32(h+32)!=Chunks||P::get32(h+36)!=0x86||
     !P::zero(h,72,HeaderBytes))return Error::Shape;
  copy(header_,h,HeaderBytes);generation_=s.generation;client_=s.client;phase_=Phase::Uploading;return Error::None;
 }
 Error append(const Scope &s,uint64_t offset,const uint8_t *bytes,size_t n){
  const auto e=editable(s);if(e!=Error::None)return e;
  if(offset!=written_)return Error::Order;
  const unsigned remaining=PayloadBytes-written_,expected=remaining<ChunkBytes?remaining:ChunkBytes;
  if(!expected||n!=expected||!P::separate(bytes,n,this,sizeof(*this)))return Error::Shape;
  copy(payload_+written_,bytes,unsigned(n));written_+=unsigned(n);++chunks_;return Error::None;
 }
 Error seal(const Scope &s){
  const auto e=editable(s);if(e!=Error::None)return e;
  if(written_!=PayloadBytes||chunks_!=Chunks)return Error::Incomplete;
  GSPDigest::SHA256 digest;digest.update(payload_,PayloadBytes);digest.finish(actualDigest_);
  if(!equal(actualDigest_,header_+40,32))return reject(Error::Digest);
  P::Library library;if(!P::decode(payload_,P::WireBytes,payload_+P::WireBytes,P::CodeBytes,library))return reject(Error::Library);
  if(!profile(library))return reject(Error::Profile);
  sealed_=true;phase_=Phase::Ready;return Error::None;
 }
 // Consume BEFORE taking the native provider/DMA/bootstrap path. There is no
 // replay, replacement or fallback to built-in shaders after this transition.
 Error consume(const Scope &s){
  if(!sameOwner(s))return Error::Identity;
  if(phase_!=Phase::Ready)return Error::State;
  if(!s.preparationAllowed)return Error::Scope;
  phase_=Phase::Consumed;return Error::None;
 }
 // Close retains the owned bytes for evidence and any retained GPU mappings.
 // Pointers already lent to the native runtime stay valid until State dies.
 bool close(const Scope &s){
  if(!sameOwner(s)||phase_==Phase::Closed)return false;
  phase_=Phase::Closed;return true;
 }
 bool data(const Scope &s,unsigned part,const uint8_t *&out,unsigned &bytes)const{
  out=nullptr;bytes=0;
  if(!sameOwner(s)||(phase_!=Phase::Ready&&phase_!=Phase::Consumed)||part>1)return false;
  out=payload_+(part?P::WireBytes:0);bytes=part?P::CodeBytes:P::WireBytes;return true;
 }
 bool info(uint8_t *out,size_t n)const{
  if(n!=InfoBytes||!P::separate(out,n,this,sizeof(*this)))return false;
  for(unsigned i=0;i<InfoBytes;++i)out[i]=0;
  P::Q::put64(out,InfoMagic);P::Q::put32(out+8,1);P::Q::put32(out+12,InfoBytes);P::Q::put64(out+16,generation_);
  P::Q::put32(out+24,unsigned(phase_));P::Q::put32(out+28,written_);P::Q::put32(out+32,chunks_);P::Q::put32(out+36,unsigned(terminalError_));
  P::Q::put32(out+40,PayloadBytes);P::Q::put32(out+44,ChunkBytes);P::Q::put32(out+48,Chunks);P::Q::put32(out+52,64);
  copy(out+64,header_+40,32);copy(out+96,actualDigest_,32);
  P::Q::put32(out+56,sealed_);
  if(sealed_){
   // Closed may follow an incomplete upload: only a digest-validated container
   // is described as usable, even though all evidence bytes remain retained.
   P::Library lib;if(written_==PayloadBytes&&terminalError_==Error::None&&equal(actualDigest_,header_+40,32)&&
     P::decode(payload_,512,payload_+512,4096,lib)&&profile(lib)){
    P::Q::put32(out+128,lib.count);P::Q::put32(out+132,lib.usedCodeBytes);
   }
  }
  return true;
 }
};
}

#pragma once
#include "Program205.hpp"

// Applies to the original IOExternalMethodArguments before any routing.
// Only Select205 accepts an input descriptor. Output descriptors and async
// forms remain excluded. A zero-length inband address may still be nonnull
// because MIG supplies an empty inband array alongside the OOL descriptor.
namespace RTXProgramInput206 {
enum class Error {None,Shape,Scope,State,Prepare,Read,Complete};
template<class Args>bool envelope(const Args*a,unsigned selector,unsigned version){
 return a&&a->version==version&&!a->asyncWakePort&&!a->asyncReference&&!a->asyncReferenceCount&&
  (!a->structureInputDescriptor||selector==RTXProgram205::Select)&&
  !a->structureOutputDescriptor&&!a->structureOutputDescriptorSize&&!a->scalarOutputCount&&!a->structureVariableOutputData&&
  (!a->scalarInputCount||a->scalarInput)&&(!a->structureInputSize||a->structureInput)&&(!a->structureOutputSize||a->structureOutput);
}
inline bool current(const uint64_t*state,const uint64_t*scalars,unsigned count){
 return state&&count==3&&scalars&&state[4]&&state[4]!=UINT64_MAX&&state[6]==2&&
  state[4]==scalars[2]&&state[5]==scalars[1];
}
class Snapshot {
 uint8_t bytes_[RTXProgram205::PayloadBytes]{};bool valid_=false;
public:
 const uint8_t*data()const{return valid_?bytes_:nullptr;}
 // No descriptor reference is retained. A successful prepare is always
 // paired with exactly one complete, including a short read or length drift.
 // These are temporary CPU input pages, never mapped into the GPU VAS.
 template<class Args,class Direction>Error take(const Args&a,uint64_t generation,bool ready,Direction outDirection){
  valid_=false;
  if(a.scalarInputCount!=3||!a.scalarInput||a.structureInputSize||!a.structureInputDescriptor||
     a.structureOutputSize!=64||!a.structureOutput)return Error::Shape;
  if(!generation||a.scalarInput[0]!=generation)return Error::Scope;
  if(!ready)return Error::State;
  auto*d=a.structureInputDescriptor;
  if(d->getLength()!=sizeof(bytes_)||d->getDirection()!=outDirection)return Error::Shape;
  if(d->prepare()!=0)return Error::Prepare;
  const bool extent=d->getLength()==sizeof(bytes_);
  const bool copied=extent&&d->readBytes(0,bytes_,sizeof(bytes_))==sizeof(bytes_);
  const bool released=d->complete()==0;
  if(!released)return Error::Complete;
  if(!copied)return Error::Read;
  valid_=true;return Error::None;
 }
};
}

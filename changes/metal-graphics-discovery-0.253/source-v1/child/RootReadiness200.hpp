#pragma once
#include <stdint.h>
namespace RTXRootReady200 {
constexpr uint64_t RootABI=242,HostABI=2,DataABI=181,DispatchABI=183,GraphicsABI=242,PortBindingABI=4;
struct State {
 uint64_t rootABI=0,hostABI=0,dataABI=0,dispatchABI=0;
 bool acknowledged=false,runtimeEnabled=false,dispatchActive=false;
 uint64_t graphicsABI=0;bool graphicsActive=false,graphicsRetained=true;
};
// Graphics ownership must already be armed before exposing the Metal plugin.
// Missing or incorrectly typed retained=false is a failure, never readiness.
inline bool valid(const State&s){
 return s.rootABI==RootABI&&s.hostABI==HostABI&&s.dataABI==DataABI&&s.dispatchABI==DispatchABI&&
  s.acknowledged&&s.runtimeEnabled&&s.dispatchActive&&s.graphicsABI==GraphicsABI&&s.graphicsActive&&!s.graphicsRetained;
}
}

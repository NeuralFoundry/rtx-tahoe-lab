#pragma once
// 78..84 is the existing resident shader ABI. 166's isolated host-only probe
// reused that range; combined GPU builds must preserve the shader contract.
namespace RTXSelector171 {
enum class Route {Legacy,Resident,Root,Host,Data,Dispatch,Graphics};
constexpr unsigned ResidentFirst=78,ResidentLast=84,RootFirst=85,RootLast=87,HostFirst=90,HostLast=96,DataFirst=97,DataLast=100,DispatchFirst=101,DispatchLast=108;
static_assert(ResidentLast<RootFirst&&RootLast<HostFirst&&HostLast<DataFirst&&DataLast<DispatchFirst,"extension selector ranges overlap");
constexpr unsigned GraphicsFirst=109,GraphicsLast=111;
static_assert(DispatchLast<GraphicsFirst,"graphics selector range overlaps compute");
constexpr Route route(unsigned n){return n>=ResidentFirst&&n<=ResidentLast?Route::Resident:
 n>=RootFirst&&n<=RootLast?Route::Root:n>=HostFirst&&n<=HostLast?Route::Host:n>=DataFirst&&n<=DataLast?Route::Data:n>=DispatchFirst&&n<=DispatchLast?Route::Dispatch:n>=GraphicsFirst&&n<=GraphicsLast?Route::Graphics:Route::Legacy;}
}

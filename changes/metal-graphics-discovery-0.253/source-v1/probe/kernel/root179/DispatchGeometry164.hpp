#pragma once
#include <stdint.h>
#include <stddef.h>
#include "changes/gsp-compute-0.25/qmd3/QmdBits.hpp"

// Geometry only: the native owner must separately prove program admission,
// buffer lengths/mappings, ownership and completion before any device write.
// This header does not make the legacy 2KiB submission window any larger.
namespace RTXGeometry164 {
struct Size {uint64_t x=0,y=0,z=0;};
struct Shape {Size grid;uint64_t invocations=0;};
inline bool local(Size s){return s.x&&s.x<=1024&&s.y&&s.y<=1024&&s.z&&s.z<=64&&s.x*s.y*s.z<=1024;}
inline bool dispatch(Size groups,Size threads,Shape &out){
 if(!local(threads)||!groups.x||groups.x>INT32_MAX||!groups.y||groups.y>UINT16_MAX||!groups.z||groups.z>UINT16_MAX)return false;
 Shape next;next.grid={groups.x*threads.x,groups.y*threads.y,groups.z*threads.z};
 if(next.grid.x>UINT32_MAX||next.grid.y>UINT32_MAX||next.grid.z>UINT32_MAX)return false;
 if(next.grid.x>UINT64_MAX/next.grid.y||next.grid.x*next.grid.y>UINT64_MAX/next.grid.z)return false;
 next.invocations=next.grid.x*next.grid.y*next.grid.z;out=next;return true;
}
// Patch a caller-owned QMD copy only after every dimension is valid. The bit
// positions come from NVIDIA NVC7C0_QMDV03_00_CTA_RASTER_* / THREAD_DIMENSION*.
inline bool patch(uint8_t *qmd,size_t bytes,Size groups,Size threads,Size compiled){
 Shape shape;if(!qmd||bytes!=256||threads.x!=compiled.x||threads.y!=compiled.y||threads.z!=compiled.z||!dispatch(groups,threads,shape))return false;
 using namespace RtxQmd3Bits;
 const struct Update {Field field;uint32_t value;} updates[]={
  {{384,32},uint32_t(groups.x)},{{416,16},uint32_t(groups.y)},{{448,16},uint32_t(groups.z)},
  {{592,16},uint32_t(threads.x)},{{608,16},uint32_t(threads.y)},{{624,16},uint32_t(threads.z)}
 };
 // All field/value ranges are proven above, so no failure follows mutation.
 for(const auto &u:updates)if(!put(qmd,bytes,u.field,u.value))return false;
 return true;
}
}

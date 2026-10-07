#pragma once
#include "QmdBits.hpp"
// Fixed CPU proposal. Actual mappings, queue ownership and a successful same-run
// execution channel are prerequisites for a future native backend.
namespace QmdProfile {
namespace B=RtxQmd3Bits;
constexpr uint64_t ProgramVA=UINT64_C(0x1020004000),ConstantVA=UINT64_C(0x1020006000),QmdVA=UINT64_C(0x1020007000);
constexpr uint64_t OutputVA=UINT64_C(0x1020008000),FenceVA=UINT64_C(0x1020009000);
constexpr uint32_t ProgramPhysical=0x03409000,ConstantPhysical=0x0340b000,QmdPhysical=0x0340c000;
constexpr uint32_t OutputPhysical=0x0340d000,FencePhysical=0x0340e000;
constexpr uint32_t OutputValue=0x30602501,FenceValue=0x306025f0,ProgramBytes=256,ConstantBytes=368;
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline bool separate(const void *a,size_t an,const void *b,size_t bn){
 if(!a||!b)return false;const auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 return x<=UINTPTR_MAX-an&&y<=UINTPTR_MAX-bn&&!(x<y+bn&&y<x+an);
}
inline bool build(uint8_t *qmd,size_t qmdBytes,uint8_t *constant,size_t constantBytes,uint8_t *command,size_t commandBytes){
 if(qmdBytes!=256||constantBytes!=4096||commandBytes!=32||!separate(qmd,qmdBytes,constant,constantBytes)||
    !separate(qmd,qmdBytes,command,commandBytes)||!separate(constant,constantBytes,command,commandBytes))return false;
 for(size_t i=0;i<qmdBytes;++i)qmd[i]=0;
 for(size_t i=0;i<constantBytes;++i)constant[i]=0;
 // Compiler reports no stack/spill/shared/barrier use. R1's ABI stack value is
 // initialized, although the fixed shader never uses R1 after its prologue.
 put64(constant+0x28,0xfffdc0);put64(constant+0x160,OutputVA);
 struct Value{B::Field field;uint32_t value;};
 const Value values[]={
  {{128,6},0x3f},{{134,1},1},{{186,1},1},{{187,1},1},{{188,1},1},{{189,1},1},{{190,1},1},{{191,1},1},
  {{256,32},uint32_t(ProgramVA>>8)},{{368,2},1},{{378,1},1},{{382,1},1},
  {{384,32},1},{{416,16},1},{{448,16},1},{{544,18},0},{{562,6},9},{{569,6},0x1a},
  {{576,4},0},{{580,4},3},{{592,16},1},{{608,16},1},{{624,16},1},{{640,1},1},{{648,9},8},{{657,6},9},
  {{736,24},0},{{763,5},0},{{768,32},uint32_t(FenceVA&UINT64_C(0xffffffff))},{{800,8},uint32_t(FenceVA>>32)},
  {{819,1},1},{{823,1},1},{{830,2},1},{{832,32},FenceValue},
  {{1024,32},uint32_t(ConstantVA&UINT64_C(0xffffffff))},{{1056,17},uint32_t(ConstantVA>>32)},{{1074,1},1},{{1075,13},ConstantBytes/16},
  {{1536,32},uint32_t(ProgramVA&UINT64_C(0xffffffff))},{{1568,17},uint32_t(ProgramVA>>32)},{{1600,24},0},
  {{1632,9},uint32_t(ProgramVA>>40)},{{1641,9},1},{{1656,8},0x86}
 };
 // Every constant is checked by the independent NVIDIA-header schema oracle.
 for(const auto &v:values)if(!B::put(qmd,qmdBytes,v.field,v.value))return false;
 const uint32_t methods[]={0x0000,0x1698,0x02b4,0x02c0};
 const uint32_t data[]={0xc7c0,0x1011,uint32_t(QmdVA>>8),9};
 for(unsigned i=0;i<4;++i){put32(command+i*8,0x20012000|(methods[i]>>2));put32(command+i*8+4,data[i]);}
 return true;
}
}

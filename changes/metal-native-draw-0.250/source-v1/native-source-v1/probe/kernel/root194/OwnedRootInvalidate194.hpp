#pragma once
#include "../root170/OwnedPageTree167.hpp"
#include "../changes/gsp-channel-0.23/GMMUInvalidate.hpp"

namespace RTXRootInvalidate194 {
// NVIDIA 570.144 TU102 dev_vm.h + kgmmuSetPdbToInvalidate_TU102.
// PDB aperture bit 1 selects system memory. Address bits [39:12] occupy
// register bits [31:4]; physical bits [59:40] occupy UPPER_PDB [19:0].
// Only this retained root is invalidated. GLOBAL ACK + SYS_MEMBAR is 0xc0;
// this does not request ALL_PDB, replay, or cancellation.
constexpr uint32_t Command=0x800000c1;
inline bool valid(uint64_t root){return root>=4096&&root%4096==0&&root<=RTXPageTree167::SysLimit-4096;}
inline uint32_t low(uint64_t root){return uint32_t((root>>8)&0xfffffff0ULL)|2U;}
inline uint32_t high(uint64_t root){return uint32_t(root>>40);}
template<class IO>bool run(IO&io,uint64_t root,const GMMUInvalidate::Preconditions&p,GMMUInvalidate::Result&r){
 namespace G=GMMUInvalidate;r={};
 if(!valid(root)||!p.pageTablesAccepted||!p.childrenVerified||!p.parentVerified||!p.exclusiveOwner){r.failure=G::Failure::Precondition;return false;}
 r.start=r.lastTime=io.now();r.attempted=true;
 if(!G::idle(io,r)||!G::write(io,r,G::PDBRegister,low(root))||!G::write(io,r,G::UpperRegister,high(root)))return false;
 uint32_t lo=0,hi=0;
 if(!G::read(io,r,G::PDBRegister,lo)||!G::read(io,r,G::UpperRegister,hi))return false;
 if(lo!=low(root)||hi!=high(root)){r.failure=G::Failure::PDBReadback;return false;}
 if(!G::write(io,r,G::CommandRegister,Command)||!G::idle(io,r))return false;
 const auto now=io.now();if(now<r.lastTime){r.failure=G::Failure::Clock;return false;}
 r.lastTime=now;r.elapsed=now-r.start;if(r.elapsed>=G::MaxNanoseconds){r.failure=G::Failure::Deadline;return false;}
 r.completed=r.passed=true;return true;
}
}

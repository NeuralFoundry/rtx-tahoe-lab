#pragma once

// The matched GA106 physical function uses FULL_PHYS_OFFSET for dev_vm
// register definitions (NVIDIA GPU_VREG_RD32/WR32), not a zero-based BAR0
// offset. The BAR1 window register is already an absolute BAR0 offset.
// This helper only serves the four registers admitted by the memory adapters.
namespace GSPVirtualRegisters {
constexpr unsigned Base=0xb80000;
inline bool physical(unsigned logical,unsigned &address){
  switch(logical){
    case 0x1704: address=logical;return true;
    case 0x30a0: case 0x30a4: case 0x30b0: address=Base+logical;return true;
    default:return false;
  }
}
template<class Context> bool read(Context &context,unsigned logical,unsigned &value){
  unsigned address=0;if(!physical(logical,address))return false;
  value=context.read(address);return true;
}
template<class Context> bool write(Context &context,unsigned logical,unsigned value){
  unsigned address=0;if(!physical(logical,address))return false;
  return context.write(address,value);
}
}

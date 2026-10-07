#pragma once
#import <Foundation/Foundation.h>
#include "RTXMemoryEvidence107.hpp"
// Internal immutable association. Production calls only after validating the
// real accelerator/parent/PCI chain. CPU fixtures may exercise it explicitly.
extern "C" BOOL RTXAttachDeviceMemory107(id device,const RTXMemory107::Evidence &e,uint64_t generation);
uint64_t RTXDedicatedMemory107(id device,SEL selector);

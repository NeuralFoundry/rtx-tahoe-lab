#pragma once
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include "RTXAcceleratorIdentity103.hpp"
// The property decoder is separate so typed framework values can be checked
// without constructing a registry service or replacing native IOKit functions.
void RTXDecodeAcceleratorProperties103(NSDictionary *properties,RTXAccelerator103::Node &node);
BOOL RTXReadAcceleratorBinding103(io_service_t port,RTXAccelerator103::Binding &binding);

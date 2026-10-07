#pragma once
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include "RTXColdParent108.hpp"
void RTXDecodeColdParent108(NSDictionary *properties,RTXColdParent108::Node &node);
BOOL RTXReadColdParent108(io_service_t service,uint64_t &generation);

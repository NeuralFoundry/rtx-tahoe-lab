#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import "RTXNativeOwner.h"
// Native applications use standard Metal APIs on the owning connection object.
// The retained device may outlive close, but cannot submit further work.
id<MTLDevice> _Nullable RTXCopyNativeCommandDevice(void) NS_RETURNS_RETAINED;
id<MTLLibrary> _Nullable RTXCopyNativeCommandLibrary(void) NS_RETURNS_RETAINED;

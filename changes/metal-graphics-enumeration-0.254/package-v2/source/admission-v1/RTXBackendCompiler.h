#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
// Development backend configuration is per device and immutable once installed.
// The helper compiles owned AIR; no GPU allocation or upload occurs here.
extern "C" BOOL RTXConfigureBackendCompiler056(id<MTLDevice> device,NSString *helper,NSString *configuration,NSString *diagnostics,NSError **error);
extern "C" NSDictionary *RTXCopyBackendCompilerInfo056(void) NS_RETURNS_RETAINED;
NSData *RTXCopyCompiledNativeLibrary056(id<MTLDevice> device,id<MTLFunction> function,NSError **error) NS_RETURNS_RETAINED;

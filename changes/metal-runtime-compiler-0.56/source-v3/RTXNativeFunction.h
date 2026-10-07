#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
// Owned snapshot for the backend compiler. Does not upload or execute code.
extern "C" NSData *RTXCopyNativeFunctionAIR055(id<MTLDevice> device,
 id<MTLFunction> function,NSError **error) NS_RETURNS_RETAINED;

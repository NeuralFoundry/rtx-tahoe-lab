#import "RTXNativeFunction.h"
#import "RTXDeviceCompiler.h"
#import <objc/runtime.h>
#import <objc/message.h>
#include <cstring>
static NSData *fail(NSError **error,NSInteger code,NSString *message){if(error)*error=[NSError errorWithDomain:@"RTXNativeFunction055" code:code userInfo:@{NSLocalizedDescriptionKey:message}];return nil;}
static uint32_t read32(const uint8_t *b){uint32_t v=0;std::memcpy(&v,b,4);return v;}
extern "C" NSData *RTXCopyNativeFunctionAIR055(id<MTLDevice> device,id<MTLFunction> function,NSError **error){
 if(error)*error=nil;
 if(!RTXDeviceCompilerOwnsDevice055(device)||!function||object_getClass(function)!=objc_getClass("_MTLFunctionInternal"))return fail(error,1,@"A native Metal function and an RTX application device are required");
 if(function.device!=device||function.functionType!=MTLFunctionTypeKernel)return fail(error,2,@"The compute function belongs to another device or shader stage");
 SEL selector=sel_registerName("bitcodeDataInternal");Method method=class_getInstanceMethod(object_getClass(function),selector);
 if(!method||std::strcmp(method_getTypeEncoding(method),"@16@0:8"))return fail(error,3,@"The native Metal function ABI is unsupported");
 dispatch_data_t raw=reinterpret_cast<dispatch_data_t(*)(id,SEL)>(objc_msgSend)(function,selector);
 if(!raw)return fail(error,4,@"The native Metal compiler did not supply function AIR");
 const size_t limit=1048576;const size_t rawSize=dispatch_data_get_size(raw);
 if(rawSize<24||rawSize>limit){dispatch_release(raw);return fail(error,5,@"Native function AIR exceeds the compiler input bounds");}
 const void *pointer=nullptr;size_t size=0;dispatch_data_t mapped=dispatch_data_create_map(raw,&pointer,&size);NSData *result=nil;
 if(mapped&&pointer&&size==rawSize){
  const auto *b=static_cast<const uint8_t *>(pointer);const uint32_t offset=read32(b+8),length=read32(b+12);
  bool valid=read32(b)==0x0b17c0de&&read32(b+4)==0&&offset==20&&read32(b+16)==UINT32_MAX&&length>=4&&length<=size-offset&&read32(b+offset)==0xdec04342;
  if(valid){const size_t end=offset+size_t(length);valid=size-end<16;for(size_t i=end;i<size&&valid;++i)valid=b[i]==0;}
  if(valid)result=[[NSData alloc]initWithBytes:pointer length:size];
 }
 if(mapped)dispatch_release(mapped);dispatch_release(raw);
 return result?result:fail(error,6,@"The native function AIR container is malformed or unavailable");
}

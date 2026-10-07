#import "RTXDeviceWrapper.h"
#import <objc/runtime.h>
#import <objc/message.h>
#include <mutex>
#include <cstring>

@interface RTXApplication209_RTXWeakDeviceWrapper047:NSObject {
    std::mutex _mutex;
    __weak id _wrapper;
    BOOL _closed;
}
- (BOOL)setWrapper:(id)wrapper;
- (id)copyWrapper;
- (void)close;
@end
@implementation RTXApplication209_RTXWeakDeviceWrapper047
- (BOOL)setWrapper:(id)wrapper {
    id candidate=[wrapper retain];BOOL accepted=NO;
    {std::lock_guard<std::mutex> lock(_mutex);if(!_closed||!candidate){_wrapper=candidate;accepted=YES;}}
    [candidate release];return accepted;
}
- (id)copyWrapper {std::lock_guard<std::mutex> lock(_mutex);return [_wrapper retain];}
- (void)close {std::lock_guard<std::mutex> lock(_mutex);_closed=YES;_wrapper=nil;}
- (void)dealloc {[self close];[super dealloc];}
@end

namespace {
char wrapperKey;
thread_local id activeDevices[16];
thread_local unsigned activeDepth;
RTXApplication209_RTXWeakDeviceWrapper047 *holder(id device,BOOL create){
    if(!device)return nil;
    @synchronized(device){
        RTXApplication209_RTXWeakDeviceWrapper047 *value=objc_getAssociatedObject(device,&wrapperKey);
        if(!value&&create){value=[RTXApplication209_RTXWeakDeviceWrapper047 new];objc_setAssociatedObject(device,&wrapperKey,value,OBJC_ASSOCIATION_RETAIN_NONATOMIC);[value release];}
        return value;
    }
}
id deviceWrapper(id device,SEL selector){
    // The measured MTLIOAccelDevice getter recursively resolves a weak wrapper,
    // falling back to the device itself when it expires. Keep a strong snapshot
    // during delegation, outside the holder lock, and bound cyclic callbacks.
    for(unsigned i=0;i<activeDepth;++i)if(activeDevices[i]==device)
        [NSException raise:NSInternalInconsistencyException format:@"RTX device wrapper cycle"];
    if(activeDepth==16)[NSException raise:NSInternalInconsistencyException format:@"RTX device wrapper chain exceeds limit"];
    activeDevices[activeDepth++]=device;
    @try {
        id wrapper=[holder(device,NO)copyWrapper];if(!wrapper)return device;
        @try {
            id result=reinterpret_cast<id(*)(id,SEL)>(objc_msgSend)(wrapper,selector);
            if(!result)[NSException raise:NSInternalInconsistencyException format:@"RTX device wrapper returned nil"];
            return [[result retain]autorelease];
        } @finally {[wrapper release];}
    } @finally {activeDevices[--activeDepth]=nil;}
}
void setDeviceWrapper(id device,SEL,id wrapper){
    if(wrapper==device)wrapper=nil;
    if(wrapper&&![wrapper respondsToSelector:sel_registerName("_deviceWrapper")])
        [NSException raise:NSInvalidArgumentException format:@"RTX wrapper must resolve _deviceWrapper"];
    if(![holder(device,YES)setWrapper:wrapper])
        [NSException raise:NSInternalInconsistencyException format:@"RTX device wrapper lifecycle is closed"];
}
}
void RTXCloseDeviceWrapper047(id device){[holder(device,YES)close];}
unsigned RTXDeviceWrapperMethodCount047(){return 2;}
BOOL RTXInstallDeviceWrapper047(Class cls){
    Class base=objc_getClass("_MTLDevice");if(!cls||!base||class_getSuperclass(cls)!=base||objc_getClass(class_getName(cls))||class_getInstanceSize(base)!=712)return NO;
    const char *names[]={"_deviceWrapper","_setDeviceWrapper:"};const char *types[]={"@16@0:8","v24@0:8@16"};
    for(unsigned i=0;i<2;++i){Method m=class_getInstanceMethod(base,sel_registerName(names[i]));if(!m||std::strcmp(method_getTypeEncoding(m),types[i]))return NO;}
    return class_addMethod(cls,sel_registerName(names[0]),reinterpret_cast<IMP>(deviceWrapper),types[0])&&
           class_addMethod(cls,sel_registerName(names[1]),reinterpret_cast<IMP>(setDeviceWrapper),types[1]);
}

#import "RTXDeviceConstruction.h"
#include <mutex>

@interface RTXApplication209_RTXAcceleratorAttachment038 : NSObject {
    std::mutex _mutex;
    id _service;
    BOOL _closed;
}
- (BOOL)attach:(id)service;
- (id)copyService;
- (void)close;
@end

@implementation RTXApplication209_RTXAcceleratorAttachment038
- (BOOL)attach:(id)service {
    if(!service)return NO;
    // Retain before publication, and run rejected-service release outside the
    // state lock. Service destruction may reenter close or query this holder.
    id candidate=[service retain];BOOL accepted=NO;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if(!_closed) {
            if(!_service){_service=candidate;candidate=nil;accepted=YES;}
            else accepted=_service==service;
        }
    }
    [candidate release];return accepted;
}
- (id)copyService {
    std::lock_guard<std::mutex> lock(_mutex);
    return [_service retain];
}
- (void)close {
    id previous=nil;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if(_closed)return;
        _closed=YES;previous=_service;_service=nil;
    }
    [previous release];
}
- (void)dealloc {[self close];[super dealloc];}
@end

static char attachmentKey;
static RTXApplication209_RTXAcceleratorAttachment038 *holder(id device,BOOL create) {
    if(!device)return nil;
    @synchronized(device) {
        RTXApplication209_RTXAcceleratorAttachment038 *value=objc_getAssociatedObject(device,&attachmentKey);
        if(!value&&create) {
            value=[[RTXApplication209_RTXAcceleratorAttachment038 alloc]init];
            if(!value)return nil;
            objc_setAssociatedObject(device,&attachmentKey,value,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [value release];
        }
        return value;
    }
}
BOOL RTXAttachAcceleratorService(id device,id service) {
    if(!device||!service)return NO;
    return [holder(device,YES)attach:service];
}
id RTXCopyAcceleratorService(id device) {return [holder(device,NO)copyService];}
void RTXCloseAcceleratorService(id device) {
    // Create a closed holder even before the first attachment so later calls
    // cannot silently reopen this terminal lifecycle.
    [holder(device,YES)close];
}

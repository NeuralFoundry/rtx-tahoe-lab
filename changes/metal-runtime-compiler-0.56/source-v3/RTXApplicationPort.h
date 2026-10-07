#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOKit/IOKitLib.h>

// Consumes an object that has already run _MTLDevice.init, including failure.
id<MTLDevice> RTXConsumeInitializedApplicationDevice044(id<MTLDevice> device,
 NSString *service,NSData *container,uint64_t generation,
 id<MTLLibrary> *library,NSError **error) NS_RETURNS_RETAINED;
id RTXApplicationInitializeWithPort044(id device,SEL selector,io_service_t port);
void RTXApplicationSetService044(id device,SEL selector,id service);
io_service_t RTXApplicationPort044(id device);
// Immutable framework identity survives closing the retained child port.
uint64_t RTXApplicationRegistry049(id device);
void RTXCloseApplicationPort044(id device);
void RTXApplicationDeviceWillDeallocate044(id device);
extern "C" NSDictionary *RTXApplicationPortInfo044(void) NS_RETURNS_RETAINED;
extern "C" id RTXApplicationCopyService044(id device) NS_RETURNS_RETAINED;

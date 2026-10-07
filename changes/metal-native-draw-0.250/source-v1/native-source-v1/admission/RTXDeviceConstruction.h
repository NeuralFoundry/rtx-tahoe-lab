#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// Registration-facing class construction, without opening or registering a device.
Class RTXNativeDeviceClass(void);
NSDictionary *RTXCopyNativeConstructionInfo(void) NS_RETURNS_RETAINED;

// One retained framework service per device. Repeating the same attachment is
// idempotent; replacement and attachment after terminal close are rejected.
BOOL RTXAttachAcceleratorService(id device, id service);
id RTXCopyAcceleratorService(id device) NS_RETURNS_RETAINED;
void RTXCloseAcceleratorService(id device);

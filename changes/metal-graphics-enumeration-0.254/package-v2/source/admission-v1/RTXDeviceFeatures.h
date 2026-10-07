#import <Foundation/Foundation.h>
#ifdef __cplusplus
extern "C" {
#endif
BOOL RTXDeviceFeaturesRuntimeCompatible(void);
BOOL RTXInstallDeviceFeatures(Class cls);
unsigned RTXDeviceFeatureMethodCount(void);
BOOL RTXInitializeDeviceFeatures(id device);
id RTXCopyDeviceFeatureQueries(id device) NS_RETURNS_RETAINED;
void RTXCloseDeviceFeatures(id device);
NSDictionary *RTXCopyDeviceFeatureInfo(void) NS_RETURNS_RETAINED;
#ifdef __cplusplus
}
#endif

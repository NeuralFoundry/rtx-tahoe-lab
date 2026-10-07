#import <Foundation/Foundation.h>
#include "RTXBrokerServer.h"
#include <dlfcn.h>
#include <cstdlib>
#include <unistd.h>
int main(int argc,const char **argv){if(argc!=7||geteuid()!=0)return 2;@autoreleasepool {
 NSBundle *bundle=[NSBundle bundleWithPath:@(argv[1])];NSError *error=nil;if(!bundle||![bundle loadAndReturnError:&error])return 3;
 if(bundle.principalClass!=NSClassFromString(@"RTXMetalBundleDevice0401"))return 4;
 void *image=dlopen(bundle.executablePath.UTF8String,RTLD_NOW|RTLD_NOLOAD);if(!image)return 5;
 void *claim=dlsym(image,"RTXClaimNativeBroker"),*info=dlsym(image,"rtx_native_info");if(!claim||!info)return 6;
 char *end=nullptr;unsigned long uid=strtoul(argv[3],&end,10);if(!end||*end||uid==0||uid>UINT32_MAX)return 7;
 unsigned long limit=strtoul(argv[5],&end,10);if(!end||*end||limit>UINT32_MAX)return 8;
 unsigned long timeout=strtoul(argv[6],&end,10);if(!end||*end||timeout>3600)return 9;
 return rtx_broker_serve(argv[2],uint32_t(uid),claim,info,argv[4],uint32_t(limit),uint32_t(timeout));
}}

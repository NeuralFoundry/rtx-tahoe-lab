#import "RTXNativeMetal.h"
#import "RTXNativeResident.h"
#import "RTXOwnedDispatch185.h"
#import "RTXStandardOwned187.h"
#import "RTXNativeOwnedBroker188.h"
#import <IOKit/IOKitLib.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#__VA_ARGS__);std::abort();}}while(0)
static NSArray *devices(){NSArray<id<MTLDevice>> *all=MTLCopyAllDevices();NSMutableArray *r=[NSMutableArray array];for(id<MTLDevice>d in all)[r addObject:@{@"name":d.name,@"registry":@(d.registryID)}];[all release];return r;}
int main(int argc,char **argv){if(argc!=2)return 2;@autoreleasepool {
 CHECK(geteuid()!=0);NSArray *before=[devices()copy];RTXNativeOwnerInfo info={};
 CHECK(rtx_native_info(&info,sizeof(info))==0&&info.magic==RTX_NATIVE_MAGIC&&info.abi==1&&info.bytes==192&&info.process_id==uint64_t(getpid())&&info.state==RTXNativeCold&&info.io_opens==0&&info.calls==0);
 CHECK(rtx_native_info(nullptr,sizeof(info))==RTX_NATIVE_ARGUMENT&&rtx_native_info(&info,sizeof(info)-1)==RTX_NATIVE_ARGUMENT);
 CHECK(!RTXCopyNativeCommandDevice()&&!RTXCopyNativeCommandLibrary());CHECK(rtx_native_arm(nullptr)==RTX_NATIVE_ARGUMENT&&rtx_native_arm("/tmp/unused-rtx-native-cold-test")==RTX_NATIVE_NOT_OPEN);
 CHECK(rtx_native_open(nullptr,5248)==RTX_NATIVE_ARGUMENT);
 uint64_t ownedCompletion=77;RTXOwnedBinding185 ownedBinding={0,0,0,4};RTXOwnedSize185 ownedSize={1,1,1};
 CHECK(rtx_native_owned_begin185(nullptr,4608)==RTX_NATIVE_PROCESS);
 CHECK(rtx_native_arm_owned187(nullptr,nullptr,5248)==RTX_NATIVE_PROCESS);
 CHECK(rtx_native_owned_submit185(0,&ownedBinding,1,ownedSize,ownedSize,&ownedCompletion)==RTX_NATIVE_PROCESS&&ownedCompletion==77);
 std::array<uint8_t,4097> input{},output{};std::array<uint64_t,5> scalars{};
 const size_t sizes[]={0,1,128,512,1024,2112,4096,4097};
 unsigned rejected=0,notOpen=0;
 for(unsigned selector=0;selector<88;++selector)for(unsigned count=0;count<6;++count)for(auto bytes:sizes)for(auto capacity:sizes){
  size_t actual=999;uint32_t result=rtx_native_call(selector,count?scalars.data():nullptr,count,bytes?input.data():nullptr,bytes,capacity?output.data():nullptr,capacity,&actual);
  CHECK(actual==0&&(result==RTX_NATIVE_ARGUMENT||result==RTX_NATIVE_NOT_OPEN));if(result==RTX_NATIVE_ARGUMENT)++rejected;else ++notOpen;
 }
 // ABI195 raw diagnostics cover the whole retained pool and optional handle/
 // digest parts; boundary rejection must occur before any connection or call.
 const uint64_t rootSizes[]={4096,131072,262144,256840,12288,45056,256,64};
 for(uint64_t part=0;part<8;++part)for(uint64_t off:{uint64_t(0),rootSizes[part]-1,rootSizes[part],UINT64_MAX}){
  uint64_t args[]={part,off,1};size_t actual=99;const bool valid=off<rootSizes[part];
  CHECK(rtx_native_call(86,args,3,nullptr,0,output.data(),1,&actual)==(valid?RTX_NATIVE_NOT_OPEN:RTX_NATIVE_ARGUMENT)&&actual==0);
 }
 for(uint64_t page:{0ULL,12ULL,13ULL,63ULL,64ULL,UINT64_MAX}){
  size_t actual=99;CHECK(rtx_native_call(87,&page,1,nullptr,0,output.data(),24,&actual)==(page<64?RTX_NATIVE_NOT_OPEN:RTX_NATIVE_ARGUMENT)&&actual==0);
 }
 {uint64_t args[]={8,0,1};size_t actual=99;CHECK(rtx_native_call(86,args,3,nullptr,0,output.data(),1,&actual)==RTX_NATIVE_ARGUMENT&&actual==0);}
 CHECK(rtx_native_info(&info,sizeof(info))==0&&info.calls==0&&info.io_opens==0&&info.open_attempts==0);
 NSData *container=[NSData dataWithContentsOfFile:@(argv[1])];CHECK(container.length==5248);
 uint64_t epoch=99;
 CHECK(rtx_native_resident_replace(nullptr,5248,1,0,&epoch)==RTX_NATIVE_ARGUMENT&&epoch==0);
 CHECK(rtx_native_resident_replace(container.bytes,container.length,0,0,&epoch)==RTX_NATIVE_ARGUMENT&&epoch==0);
 CHECK(rtx_native_resident_replace(container.bytes,container.length,UINT64_MAX,0,&epoch)==RTX_NATIVE_ARGUMENT&&epoch==0);
 CHECK(rtx_native_resident_replace(container.bytes,container.length,1,0,&epoch)==uint32_t(kIOReturnNotPrivileged)&&epoch==0);
 CHECK(rtx_native_open(container.bytes,container.length)==uint32_t(kIOReturnNotPrivileged));
 CHECK(rtx_native_info(&info,sizeof(info))==0&&info.state==RTXNativeFailed&&info.open_attempts==1&&info.io_opens==0&&info.calls==0);
 CHECK(rtx_native_open(container.bytes,container.length)==RTX_NATIVE_STATE);
 CHECK(rtx_native_close()==0&&rtx_native_close()==0&&rtx_native_info(&info,sizeof(info))==0&&info.state==RTXNativeClosed&&info.io_opens==0&&info.io_closes==0&&info.states_destroyed==0);
 CHECK([before isEqual:devices()]);[before release];
 std::printf("{\"passed\":true,\"checks\":%u,\"argument_rejections\":%u,\"closed_rejections\":%u,\"native_calls\":0,\"kernel_opens\":0,\"gpu_commands_submitted\":false,\"metal_registered\":false}\n",checks,rejected,notOpen);
}return 0;}

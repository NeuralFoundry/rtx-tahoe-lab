#import "RTXMappedBufferTransfer182.h"
#import "RTXMetalBuffer.h"
#import "RTXHostBuffer.h"
#import "RTXNativeOwner.h"
#import <objc/runtime.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
static unsigned checks=0,rejected=0;
#define CHECK(v) do{++checks;if(!(v)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#v);std::exit(2);}}while(0)
int main(int argc,char**argv){if(argc!=2||geteuid()!=501)return 1;@autoreleasepool{
 Class cls=objc_allocateClassPair([NSObject class],"RTXTransferFixtureDevice182",0);CHECK(cls&&RTXInstallBufferMethods(cls));objc_registerClassPair(cls);
 id<MTLDevice> device=(id<MTLDevice>)[[cls alloc]init],foreign=(id<MTLDevice>)[[cls alloc]init];
 id<MTLBuffer> buffer=[device newBufferWithLength:12289 options:MTLResourceStorageModeShared];CHECK(buffer&&buffer.device==device&&buffer.length==12289);
 std::vector<uint8_t> input(8193),newData(8193,0x71);for(size_t i=0;i<input.size();++i)input[i]=uint8_t(i*17);
 std::memcpy(static_cast<uint8_t*>(buffer.contents)+4095,input.data(),input.size());
 id upload=RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(4095,8193),YES);CHECK(upload);
 NSData*snapshot=RTXCopyMappedUpload182(upload);CHECK(snapshot.length==8193&&!std::memcmp(snapshot.bytes,input.data(),8193));
 CHECK(!RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(0,1),NO));++rejected;
 std::memset(static_cast<uint8_t*>(buffer.contents)+4095,0x33,8193);CHECK(!std::memcmp(snapshot.bytes,input.data(),8193));
 CHECK(RTXFinishMappedBufferTransfer182(upload,nil));CHECK(!RTXFinishMappedBufferTransfer182(upload,nil));++rejected;[snapshot release];[upload release];
 id download=RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(4095,8193),NO);CHECK(download&&!RTXCopyMappedUpload182(download));
 NSData*result=[NSData dataWithBytes:newData.data()length:newData.size()];CHECK(RTXFinishMappedBufferTransfer182(download,result));
 CHECK(!std::memcmp(static_cast<uint8_t*>(buffer.contents)+4095,newData.data(),newData.size()));[download release];
 for(unsigned mode=0;mode<3;++mode){id tx=RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(0,16),NO);CHECK(tx);
  if(mode==0)static_cast<uint8_t*>(buffer.contents)[0]^=1;
  else if(mode==1){uint8_t value=0x88;CHECK([(RTXHostBuffer*)buffer writeBytes:&value range:NSMakeRange(0,1)]);}
  else [buffer setPurgeableState:MTLPurgeableStateEmpty];
  std::vector<uint8_t> before(16);std::memcpy(before.data(),buffer.contents,16);
  CHECK(!RTXFinishMappedBufferTransfer182(tx,[NSData dataWithBytes:newData.data()length:16]));CHECK(!std::memcmp(before.data(),buffer.contents,16));++rejected;[tx release];
  if(mode==2)[buffer setPurgeableState:MTLPurgeableStateNonVolatile];
 }
 for(NSRange range:{NSMakeRange(0,0),NSMakeRange(12289,1),NSMakeRange(NSUIntegerMax,1)}){CHECK(!RTXNewMappedBufferTransfer182(device,buffer,range,NO));++rejected;}
 CHECK(!RTXNewMappedBufferTransfer182(foreign,buffer,NSMakeRange(0,1),NO));++rejected;
 id retained=RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(0,32),YES);CHECK(retained);[buffer release];buffer=nil;
 NSDictionary*arena=RTXCopyBufferArenaInfo(device);CHECK([arena[@"live_buffers"]unsignedIntValue]==1);[arena release];
 RTXCancelMappedBufferTransfer182(retained);CHECK(!RTXCopyMappedUpload182(retained));[retained release];
 arena=RTXCopyBufferArenaInfo(device);CHECK([arena[@"live_buffers"]unsignedIntValue]==0);[arena release];
 buffer=[device newBufferWithLength:64 options:MTLResourceStorageModeShared];CHECK(buffer);
 id closing=RTXNewMappedBufferTransfer182(device,buffer,NSMakeRange(0,64),NO);CHECK(closing);RTXCloseBufferArena(device);
 std::vector<uint8_t> zero(64);CHECK(!RTXFinishMappedBufferTransfer182(closing,[NSData dataWithBytes:zero.data()length:64]));++rejected;[closing release];
 CHECK(rtx_native_mapped_transfer182(buffer,0,0,0,1,YES)!=0);[buffer release]; // Actual unprivileged entry denies any native I/O.
 RTXNativeOwnerInfo native{};CHECK(rtx_native_info(&native,sizeof(native))==0&&native.io_opens==0&&native.calls==0);
 NSDictionary*report=@{@"passed":@YES,@"checks":@(checks),@"rejected":@(rejected),@"actual_metal_buffer_objects":@YES,@"synthetic_device":@YES,@"native_hardware_opens":@0,@"gpu_executed":@NO};
 CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[1])options:NSDataWritingWithoutOverwriting error:nil]);
 [foreign release];[device release];std::printf("buffer checks=%u rejected=%u\n",checks,rejected);
}return 0;}

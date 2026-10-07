#import "RTXNativeResident.h"
#include "RTXBrokerServer.h"
#include "RTXNativeOwner.h"
#include "NumericFixture063.hpp"
#include <unistd.h>
static RTXCPU063::Backend fixture;static bool claimed;static unsigned deaths;static NSString *directory;
@interface ResidentFixtureTransport059:NSObject<RTXResidentCommandTransport058>
@end
@implementation ResidentFixtureTransport059
- (void)dealloc{++deaths;[super dealloc];}
- (BOOL)replaceLibraryPayload:(NSData *)payload expectedEpoch:(uint64_t)epoch expectedCompleted:(uint64_t)completed newEpoch:(uint64_t *)next error:(NSError **)error{
 *next=0;*error=nil;if(payload.length!=4608)return NO;RTXCPU063::Payload p{};std::memcpy(p.data(),payload.bytes,p.size());bool ok=fixture.replace(p,epoch,completed,*next);
 if(ok){NSString *name=[NSString stringWithFormat:@"replacement-%llu-payload.bin",*next];ok=[payload writeToFile:[directory stringByAppendingPathComponent:name]options:NSDataWritingWithoutOverwriting error:nil];}return ok;
}
- (BOOL)executeRequest:(NSData *)request libraryPayload:(NSData *)payload result:(NSData **)result completion:(uint64_t *)completion error:(NSError **)error{
 *result=nil;*completion=0;*error=nil;if(request.length!=2112||payload.length!=4608)return NO;std::array<uint8_t,2112> wire{};RTXCPU063::Payload p{};std::array<uint8_t,2048> out{};
 std::memcpy(wire.data(),request.bytes,2112);std::memcpy(p.data(),payload.bytes,4608);if(!fixture.execute(wire,p,fixture.epoch,out,*completion))return NO;
 NSString *prefix=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"job-%llu",*completion]];
 if(![request writeToFile:[prefix stringByAppendingString:@"-request.bin"]options:NSDataWritingWithoutOverwriting error:nil]||![payload writeToFile:[prefix stringByAppendingString:@"-payload.bin"]options:NSDataWritingWithoutOverwriting error:nil])return NO;
 *result=[NSData dataWithBytes:out.data()length:out.size()];return [*result writeToFile:[prefix stringByAppendingString:@"-result.bin"]options:NSDataWritingWithoutOverwriting error:nil];
}
@end
extern "C" BOOL rtx_fixture_claim063(id<RTXCommandTransport> *transport,NSData **image,uint64_t *generation,uint64_t *completed){
 if(claimed)return NO;RTXCPU063::Image bytes{};uint64_t epoch=0;if(!fixture.claim(bytes,*generation,*completed,epoch)||epoch!=1)return NO;
 claimed=true;*transport=[ResidentFixtureTransport059 new];*image=[[NSData alloc]initWithBytes:bytes.data()length:bytes.size()];return YES;
}
extern "C" uint32_t rtx_fixture_info063(RTXNativeOwnerInfo *info,size_t bytes){
 if(!info||bytes!=sizeof(*info))return 1;*info={};info->magic=RTX_NATIVE_MAGIC;info->abi=1;info->bytes=sizeof(*info);info->process_id=getpid();info->state=RTXNativeCold;return 0;
}
extern "C" int rtx_fixture_prepare063(const char *firstPath,const char *secondPath,const char *evidencePath,const char *requestsPath,const char *expectedPath){
 if(geteuid()!=0||!firstPath||!secondPath||!evidencePath||!requestsPath||!expectedPath||directory||claimed)return 2;
 @autoreleasepool {NSData *first=[NSData dataWithContentsOfFile:@(firstPath)],*second=[NSData dataWithContentsOfFile:@(secondPath)];if(first.length!=5248||second.length!=5248)return 3;
 NSData *requests=[NSData dataWithContentsOfFile:@(requestsPath)],*expected=[NSData dataWithContentsOfFile:@(expectedPath)];if(requests.length!=65*2112||expected.length!=65*2048)return 3;const auto *r=static_cast<const uint8_t *>(requests.bytes),*e=static_cast<const uint8_t *>(expected.bytes);fixture.requestReference.assign(r,r+requests.length);fixture.expectedReference.assign(e,e+expected.length);
 std::memcpy(fixture.image.data(),first.bytes,5248);std::memcpy(fixture.second.data(),second.bytes,5248);directory=[@(evidencePath)copy];return 0;}
}
extern "C" int rtx_fixture_finish063(void){if(geteuid()!=0||!directory)return 2;@autoreleasepool{int code=0;
 NSDictionary *report=@{@"passed":@(code==0&&fixture.executions==65&&fixture.replacements==3&&fixture.admissions==0&&fixture.completed==65&&fixture.epoch==4&&deaths==1),@"result":@(code),@"cpu_fixture_only":@YES,@"jobs":@(fixture.executions),@"replacements":@(fixture.replacements),@"cpu_fixture_admissions":@(fixture.admissions),@"epoch":@(fixture.epoch),@"transport_destructions":@(deaths),@"native_iokit_connected":@NO,@"gpu_commands_submitted":@NO};
 BOOL saved=[[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:[directory stringByAppendingPathComponent:@"fixture-returned.json"]options:NSDataWritingWithoutOverwriting error:nil];[directory release];directory=nil;return saved&&[report[@"passed"]boolValue]?0:4;
}}

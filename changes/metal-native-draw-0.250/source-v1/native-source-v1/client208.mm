#import "owner/RTXOwnedBrokerTransport208.h"
#include "owner/OwnedBroker208.hpp"
#include <cstdio>
#include <string>
#include <cstdlib>
#include <unistd.h>
namespace O=RTXOwnedBroker208;namespace B=RTXBatch187;static unsigned checks=0;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#__VA_ARGS__);std::exit(2);}}while(0)
@interface Recording208:NSObject<RTXOwnedChannel208>{@public id<RTXOwnedChannel208>inner;NSString*folder;bool corrupt;unsigned messages;uint32_t serverUID;int64_t serverPID;}
@end
@implementation Recording208
- (BOOL)exchange:(NSData*)request reply:(NSData**)reply serverUID:(uint32_t*)uid serverPID:(int64_t*)pid{
 NSMutableData*sent=[request mutableCopy];if(corrupt&&messages==1){auto*p=static_cast<uint8_t*>(sent.mutableBytes);p[64]^=1;}
 unsigned n=messages++;CHECK([sent writeToFile:[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"%u-request.bin",n]]options:NSDataWritingWithoutOverwriting error:nil]);
 BOOL okay=[inner exchange:sent reply:reply serverUID:uid serverPID:pid];[sent release];serverUID=*uid;serverPID=*pid;
 if(*reply)CHECK([*reply writeToFile:[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"%u-reply.bin",n]]options:NSDataWritingWithoutOverwriting error:nil]);return okay;
}
- (void)cancel{[inner cancel];}
- (void)dealloc{[inner release];[folder release];[super dealloc];}
@end
static NSData*wire(NSData*container,uint64_t serial,bool alias){
 RTXCatalog187::Catalog c;CHECK(RTXCatalog187::decode(static_cast<const uint8_t*>(container.bytes),container.length,c));const auto&p=c.library.programs[0];
 B::Input bindings[3]={{17,4097,4,0},{19,8193,4092,1},{alias?17ULL:23ULL,alias?4097ULL:16385ULL,alias?4ULL:12284ULL,2}};
 B::Plan plan;CHECK(B::plan(p,0,bindings,3,{1,1,1},{p.localX,p.localY,p.localZ},0x208,serial,plan));std::array<B::Bytes,4>buffers;
 for(unsigned i=0;i<plan.resources;++i){buffers[i].resize(plan.resource[i].bytes);for(size_t j=0;j<buffers[i].size();++j)buffers[i][j]=uint8_t(j*17+i*23);}
 B::Bytes out;CHECK(B::assemble(plan,buffers,out));return[NSData dataWithBytes:out.data()length:out.size()];
}
int main(int argc,char**argv){@autoreleasepool{
 CHECK(argc==7&&geteuid()==501);NSString*service=@(argv[1]),*directory=@(argv[2]),*initial=@(argv[3]),*second=@(argv[4]);const std::string mode=argv[5];unsigned jobs=unsigned(std::strtoul(argv[6],nullptr,10));
 NSData*first=[NSData dataWithContentsOfFile:initial],*changed=[NSData dataWithContentsOfFile:second];CHECK(first.length==5248&&changed.length==5248);
 Recording208*record=[Recording208 new];record->inner=RTXNewOwnedXPC208(service,10000);record->folder=[directory copy];record->corrupt=mode=="bad-hash";CHECK(record->inner);
 NSError*error=nil;id<RTXCommandTransport>transport=RTXNewOwnedTransport208(record,first,0x208,&error);unsigned completed=0;bool rejected=false;
 if(mode=="claim-fail"){CHECK(!transport&&error);rejected=true;}
 else{
  CHECK(transport&&!error);NSDictionary*hello=RTXCopyOwnedTransportInfo208(transport);CHECK([hello[@"server_pid"]longLongValue]>0);[hello release];
  for(unsigned i=0;i<jobs;++i){NSData*selected=(mode=="denied"||mode=="bad-hash"||i%2)?changed:first;NSData*request=wire(selected,i+1,i%2);NSData*result=nil;uint64_t serial=99;error=nil;
   BOOL okay=[transport executeRequest:request libraryPayload:[selected subdataWithRange:NSMakeRange(640,4608)]result:&result completion:&serial error:&error];
   if(mode!="normal"){CHECK(!okay&&!result&&!serial&&error);rejected=true;break;}
   CHECK(okay&&result&&serial==i+1&&!error);RTXCatalog187::Catalog c;CHECK(RTXCatalog187::decode(static_cast<const uint8_t*>(selected.bytes),selected.length,c));B::Plan plan;CHECK(B::decode(static_cast<const uint8_t*>(request.bytes),request.length,c.library,0x208,serial,plan));CHECK(result.length==plan.payloadBytes);
   auto*raw=static_cast<const uint8_t*>(request.bytes);auto*out=static_cast<const uint8_t*>(result.bytes);
   for(unsigned j=0;j<plan.resources;++j){const auto&r=plan.resource[j];for(size_t k=0;k<r.bytes;++k){const auto before=raw[B::Header+r.payloadOffset+k];CHECK(out[r.payloadOffset+k]==uint8_t(before^((r.access&2)?uint8_t(0x5a+k%31):0)));}}
   CHECK([result writeToFile:[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%u-result.bin",i+1]]options:NSDataWritingWithoutOverwriting error:nil]);++completed;
  }
  if(mode=="normal")RTXCloseOwnedTransport208(transport);[transport release];
 }
 [record cancel];NSDictionary*report=@{@"passed":@YES,@"actual_xpc":@YES,@"objective_c_transport":@YES,@"cpu_backend":@YES,@"gpu_executed":@NO,@"pid":@(getpid()),@"uid":@(geteuid()),@"server_pid":@(record->serverPID),@"server_uid":@(record->serverUID),@"messages":@(record->messages),@"completed":@(completed),@"rejected":@(rejected),@"checks":@(checks),@"mode":@(mode.c_str())};
 CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:[directory stringByAppendingPathComponent:@"client-result208.json"]options:NSDataWritingWithoutOverwriting error:nil]);[record release];return 0;
}}

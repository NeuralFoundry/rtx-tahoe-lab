#import "RTXColdParent108.h"
#include "../probe/kernel/gpu242/GraphicsLayout242.hpp"
#import <Metal/Metal.h>
#include <map>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
using namespace RTXColdParent108;
static unsigned checks=0,accepted=0,rejected=0,typed=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(2);}}while(0)
static RTXMemory107::Evidence memory;
static NSArray *numbers(){return @[@"GSPOwnerPhase",@"GSPFirmwareStartMask",@"GSPProgramCompleted",@"GSPShaderUploadPhase",@"GSPShaderUploadBytes",@"GSPShaderUploadError",@"GSPResidentProgramEpoch",@"GSPResidentProgramPhase"];}
static NSArray *flags(){return @[@"GSPExecutionAttempted",@"GSPDmaResourcesHeld",@"GSPDmaProviderOpen",@"GSPDmaPinnedUntilRestart",@"FirmwareStartAttempted",@"FirmwareExecuted",@"GSPResidentProgramReplaced",@"GSPHostFencePassed",@"GSPInitDoneObserved",@"RTXMetalVerified",@"OwnedRootAcknowledged",@"OwnedRootRuntimeEnabled"];}
static NSMutableDictionary *properties(){
 unsigned char raw[128]={};CHECK(RTXMemory107::encode(memory,raw,sizeof(raw)));
 auto p=[NSMutableDictionary dictionaryWithDictionary:@{@"OwnedDataABI":@181,@"ProbeVersion":[NSString stringWithUTF8String:RTXAccelerator103::ParentVersion],@"ProbeComplete":@YES,@"ProbePassed":@YES,@"TargetIdentity":@0x252010deULL,@"TargetSubsystem":@0x104c1043ULL,@"ProbeMemoryEvidence107":[NSData dataWithBytes:raw length:sizeof(raw)]}];
 p[@"OwnedProgramABI"]=@205;p[@"OwnedProgramInputABI"]=@206;p[@"OwnedDispatchABI"]=@183;p[@"OwnedRootABI"]=@(RTXGraphicsLayout242::ABI);
 for(NSString *k in numbers())p[k]=@0;for(NSString *k in flags())p[k]=@NO;return p;
}
static Node decode(NSDictionary *p){++typed;Node n;RTXDecodeColdParent108(p,n);n.entry.registry=37;n.entry.isProbe=true;return n;}
struct Graph:Reader {
 std::map<uint32_t,Node> nodes;std::map<uint32_t,uint32_t> links;std::map<uint32_t,int> held;
 uint32_t failRead=0,throwRead=0,failParent=0;bool errorReference=false;unsigned reads=0,parents=0,releases=0;
 bool read(uint32_t h,Node &n)override{++reads;if(h==throwRead)throw std::runtime_error("CPU read exception");if(h==failRead||!nodes.count(h))return false;n=nodes.at(h);return true;}
 bool parent(uint32_t h,uint32_t &out)override{++parents;out=0;if(h==failParent&&!errorReference)return false;if(!links.count(h))return false;out=links.at(h);if(out)++held[out];return h!=failParent;}
 void release(uint32_t h)override{CHECK(h&&held[h]>0);--held[h];++releases;}
 void balanced(){for(const auto &v:held)CHECK(v.second==0);CHECK(parents<=15&&reads<=16);}
};
static Graph good(){
 Graph g;g.nodes[1]=decode(properties());Node pci;pci.entry.registry=73;pci.entry.isPCI=pci.entry.validPCI=true;pci.entry.vendor=0x10de;pci.entry.device=0x2520;pci.entry.subvendor=0x1043;pci.entry.subdevice=0x104c;g.nodes[2]=pci;g.links[1]=2;return g;
}
static void check(Graph g,bool expected,uint32_t port=1){uint64_t generation=99;CHECK(validate(g,port,generation)==expected);CHECK(generation==(expected?37:0));g.balanced();if(expected)++accepted;else ++rejected;}
int main(int argc,char **argv){if(argc!=3||geteuid()!=501)return 1;@autoreleasepool {
 NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];CHECK(data.length==128&&RTXMemory107::decode(static_cast<const unsigned char*>(data.bytes),128,memory)&&memory.generation==37);
 for(NSString*key in @[@"OwnedProgramABI",@"OwnedProgramInputABI"]){for(id bad in @[@YES,@205.0,@206.0,@0,@"205",[NSNull null]]){auto p=properties();p[key]=bad;CHECK(!eligible(decode(p)));}auto p=properties();[p removeObjectForKey:key];CHECK(!eligible(decode(p)));}
 check(good(),true);check(good(),false,0);check(good(),false,UINT32_MAX);
 for(NSString *key in numbers()){
  for(id bad in @[@NO,@YES,@0.0,@1,@(-1),@18446744073709551615ULL,@"0",[NSNull null]]){auto p=properties();p[key]=bad;Node n=decode(p);CHECK(!n.numbersZero&&!eligible(n));}
  auto p=properties();[p removeObjectForKey:key];CHECK(!eligible(decode(p)));
 }
 for(NSString *key in flags()){
  for(id bad in @[@YES,@0,@0.0,@"false",[NSNull null]]){auto p=properties();p[key]=bad;Node n=decode(p);CHECK(!n.flagsClear&&!eligible(n));}
  auto p=properties();[p removeObjectForKey:key];CHECK(!eligible(decode(p)));
 }
 for(NSString *key in @[@"ProbeComplete",@"ProbePassed"]){auto p=properties();p[key]=@1;CHECK(!eligible(decode(p)));p[key]=@NO;CHECK(!eligible(decode(p)));}
 for(id bad in @[@"0.81.0",@"0.80.0",@"0.75.0",@"0.75.1",@"0.107.0",@752,[NSNull null]]){auto p=properties();p[@"ProbeVersion"]=bad;CHECK(!eligible(decode(p)));}
 for(id bad in @[[NSData data],@128,@YES,@"bytes",[NSNull null]]){auto p=properties();p[@"ProbeMemoryEvidence107"]=bad;CHECK(!eligible(decode(p)));}
 for(id bad in @[@YES,@181.0,@180,@"181",[NSNull null]]){auto p=properties();p[@"OwnedDataABI"]=bad;CHECK(!eligible(decode(p)));}
 {auto p=properties();[p removeObjectForKey:@"OwnedDataABI"];CHECK(!eligible(decode(p)));}
 for(id bad in @[@YES,@183.0,@182,@"183",[NSNull null]]){auto p=properties();p[@"OwnedDispatchABI"]=bad;CHECK(!eligible(decode(p)));}
 {auto p=properties();[p removeObjectForKey:@"OwnedDispatchABI"];CHECK(!eligible(decode(p)));}
 for(id bad in @[@YES,@242.0,@195,@194,@"242",[NSNull null]]){auto p=properties();p[@"OwnedRootABI"]=bad;CHECK(!eligible(decode(p)));}
 {auto p=properties();[p removeObjectForKey:@"OwnedRootABI"];CHECK(!eligible(decode(p)));}
 CHECK(!eligible(decode(nil))&&!eligible(decode(@{})));
 for(unsigned kind=0;kind<11;++kind){auto g=good();auto &n=g.nodes[1];switch(kind){
  case 0:n.entry.registry=0;break;case 1:n.entry.registry=38;break;case 2:n.entry.memory.generation=38;break;
  case 3:n.entry.memory.reportedBytes=1;break;case 4:n.numbersZero=false;break;case 5:n.flagsClear=false;break;
  case 6:n.entry.isProbe=false;break;case 7:n.entry.isPCI=true;break;case 8:n.entry.isAccelerator=true;break;
  case 9:n.entry.isChild=true;break;case 10:n.entry.targetSubsystem=0;break;}check(g,false);}
 for(auto member:{&RTXAccelerator103::Node::vendor,&RTXAccelerator103::Node::device,&RTXAccelerator103::Node::subvendor,&RTXAccelerator103::Node::subdevice}){auto g=good();g.nodes[2].entry.*member=0;check(g,false);}
 {auto g=good();g.nodes[2].entry.validPCI=false;check(g,false);}
 {auto g=good();g.nodes[2].entry.registry=37;check(g,false);}
 {auto g=good();g.links[1]=1;check(g,false);}
 for(unsigned handle:{1U,2U}){auto g=good();g.failRead=handle;check(g,false);}
 for(bool returned:{false,true}){auto g=good();g.failParent=1;g.errorReference=returned;check(g,false);}
 {auto g=good();g.throwRead=2;uint64_t gen=99;bool threw=false;try{validate(g,1,gen);}catch(const std::runtime_error&){threw=true;}CHECK(threw&&gen==0);g.balanced();}
 for(unsigned length:{16U,17U}){auto g=good();Node pci=g.nodes[2];g.nodes.erase(2);g.links.clear();for(unsigned i=1;i<length;++i){Node n;n.entry.registry=100+i;g.nodes[i+1]=(i+1==length)?pci:n;g.links[i]=i+1;}check(g,length==16);}
 // Actual read-only rejection of the retained old0.79 parent, AMD, and invalid
 // handles. This test must never open either existing hardware service.
 io_service_t probe=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("RTXProbe"));CHECK(probe);{uint64_t gen=99;CHECK(!RTXReadColdParent108(probe,gen)&&gen==0);}IOObjectRelease(probe);
 io_service_t amd=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("IOAccelerator"));CHECK(amd);
 for(io_service_t handle:{0U,UINT32_MAX,amd}){uint64_t gen=99;CHECK(!RTXReadColdParent108(handle,gen)&&gen==0);}
 IOObjectRelease(amd);
 NSDictionary *report=@{@"passed":@YES,@"pid":@(getpid()),@"checks":@(checks),@"accepted_synthetic_graphs":@(accepted),@"rejected_synthetic_graphs":@(rejected),@"typed_cases":@(typed),@"actual_readonly_rejections":@4,@"retained_old_probe_rejected":@YES,@"native_opens":@0,@"firmware_started":@NO,@"positive_hardware_connection":@NO};
 CHECK([[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:nil]writeToFile:@(argv[2])options:NSDataWritingWithoutOverwriting error:nil]);std::printf("{\"passed\":true,\"checks\":%u}\n",checks);
}return 0;}

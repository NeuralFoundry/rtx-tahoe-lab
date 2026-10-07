#import <Foundation/Foundation.h>
#include "GraphicsBackend251.hpp"
#include "Lifecycle202.hpp"
#include "OwnedGraphicsBroker251.hpp"
#include <dlfcn.h>
#include <unistd.h>
namespace D=RTXDrawTransfer248;namespace G=RTXOwnedGraphicsBroker251;
struct Model{uint64_t serial=0;unsigned begin=0,retire=0,ready=0,withdraw=0;};
static int begin(void*p){auto&m=*static_cast<Model*>(p);return ++m.begin==1?0:1;}
static int draw(void*p,const void*input,size_t n,void*output,size_t bytes,uint64_t*serial){
 auto&m=*static_cast<Model*>(p);const auto*r=static_cast<const uint8_t*>(input);D::Bytes request(r,r+n);D::Plan plan;
 if(m.begin!=1||m.ready!=1||m.withdraw||m.retire||!G::decode(request,251,m.serial+1,plan)||bytes!=plan.payloadBytes)return 1;
 std::memcpy(output,r+D::Header,bytes);auto*out=static_cast<uint8_t*>(output);
 ++m.serial;for(unsigned y=0;y<plan.height;++y)for(unsigned x=0;x<plan.width*4;++x)out[size_t(plan.vertexBytes+plan.colorOffset+y*plan.pitch+x)]^=uint8_t(m.serial);
 *serial=m.serial;return 0;
}
static int retire(void*p){auto&m=*static_cast<Model*>(p);return ++m.retire==1?0:1;}
static int lifecycle(void*p,uint32_t event){auto&m=*static_cast<Model*>(p);if(event==1)return m.begin==1&&!m.serial&&!m.retire&&++m.ready==1?0:1;if(event==2)return m.ready==1&&!m.retire&&++m.withdraw==1?0:1;return 1;}
int main(int argc,char**argv){@autoreleasepool{
 if(argc!=4||geteuid()!=0)return 2;void*h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!h)return 3;
 using Serve=int(*)(const char*,uint32_t,const RTXGraphicsBackend251::Callbacks*,const char*,uint32_t,uint32_t,const RTXOwnedLifecycle202::Callbacks*);
 auto serve=reinterpret_cast<Serve>(dlsym(h,"rtx_owned_broker_serve251"));auto permitted=reinterpret_cast<int(*)()>(dlsym(h,"rtx_owned_broker_close_permitted251"));if(!serve||!permitted)return 4;
 Model m;RTXGraphicsBackend251::Callbacks cb;cb.context=&m;cb.generation=251;cb.begin=begin;cb.draw=draw;cb.retire=retire;
 RTXOwnedLifecycle202::Callbacks life;life.context=&m;life.function=lifecycle;
 int code=serve(argv[2],501,&cb,argv[3],5,30,&life);bool passed=code==0&&permitted()==1&&m.serial==3&&m.begin==1&&m.retire==1&&m.ready==1&&m.withdraw==1;
 NSDictionary*v=@{@"passed":@(passed),@"returncode":@(code),@"pid":@(getpid()),@"completed":@(m.serial),@"begin":@(m.begin),@"retire":@(m.retire),@"ready":@(m.ready),@"withdraw":@(m.withdraw),@"actual_xpc":@YES,@"gpu_executed":@NO};NSError*error=nil;NSData*data=[NSJSONSerialization dataWithJSONObject:v options:NSJSONWritingSortedKeys error:&error];if(!data||error||![data writeToFile:[@(argv[3])stringByAppendingPathComponent:@"cpu-server.json"]options:NSDataWritingWithoutOverwriting error:&error]||error)return 5;return passed?0:1;
}}

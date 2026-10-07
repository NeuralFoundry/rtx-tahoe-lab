#include "VectorProfile.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
namespace V=RtxVector030;
static unsigned long long checks=0;
static void check(bool ok){++checks;if(!ok){fprintf(stderr,"check %llu failed\n",checks);exit(1);}}
static void writeFile(const std::string &p,const void *data,size_t n){FILE *f=fopen(p.c_str(),"wb");check(f!=nullptr);check(fwrite(data,1,n,f)==n);check(fclose(f)==0);}
int main(int argc,char **argv){
 check(argc==3);uint8_t code[512];FILE *f=fopen(argv[1],"rb");check(f!=nullptr);check(fread(code,1,512,f)==512);check(fgetc(f)==EOF);check(fclose(f)==0);
 uint8_t image[V::ImageBytes],actual[V::ImageBytes],command[32];uint32_t a[64],b[64];
 constexpr unsigned counts[]={0,1,31,32,33,61,64};unsigned cases=0;
 for(unsigned seed=0;seed<128;++seed){V::inputs(seed,a,b);for(unsigned n:counts){
  check(V::build(image,sizeof(image),command,sizeof(command),code,sizeof(code),a,b,n));memcpy(actual,image,sizeof(image));
  for(unsigned i=0;i<n;++i){const auto sum=uint32_t(uint64_t(a[i])+uint64_t(b[i]));check(V::get32(image+16384+i*4)!=sum);V::Q::put32(actual+16384+i*4,sum);}
  V::Q::put32(actual+20480,V::Completion);check(V::validateCapture(actual,sizeof(actual),image,sizeof(image),n));
  check(!V::validateCapture(image,sizeof(image),image,sizeof(image),n));
  actual[20480]^=1;check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),n));actual[20480]^=1;
  if(n){actual[16384+(n-1)*4]^=1;check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),n));actual[16384+(n-1)*4]^=1;}
  if(n<64){actual[16384+n*4]^=1;check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),n));actual[16384+n*4]^=1;}
  ++cases;
 }}
 V::inputs(V::DefaultSeed,a,b);check(V::build(image,sizeof(image),command,sizeof(command),code,sizeof(code),a,b,V::DefaultCount));memcpy(actual,image,sizeof(image));
 for(unsigned i=0;i<V::DefaultCount;++i)V::Q::put32(actual+16384+i*4,uint32_t(uint64_t(a[i])+uint64_t(b[i])));
 V::Q::put32(actual+20480,V::Completion);
 constexpr unsigned immutable[]={0,511,512,8191,8192,8192+0x28,8192+0x160,8192+0x168,8192+0x170,8192+0x178,8192+0x200,8192+0x2ff,8192+0x300,8192+0x3ff,12287,12544,16383,16384+61*4,16639,16640,20479,20484,24575};
 for(unsigned at:immutable){actual[at]^=1;check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),61));actual[at]^=1;}
 for(unsigned i=0;i<256;++i)actual[12288+i]^=uint8_t(i+1);
 check(V::validateCapture(actual,sizeof(actual),image,sizeof(image),61));
 check(!V::validateCapture(actual,sizeof(actual)-1,image,sizeof(image),61));check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image)-1,61));
 check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),64));check(!V::validateCapture(actual,sizeof(actual),image,sizeof(image),65));
 check(!V::build(image,sizeof(image)-1,command,32,code,512,a,b,61));check(!V::build(image,sizeof(image),command,31,code,512,a,b,61));
 check(!V::build(image,sizeof(image),command,32,code,511,a,b,61));check(!V::build(image,sizeof(image),command,32,code,512,a,b,65));
 check(!V::build(image,sizeof(image),image+100,32,code,512,a,b,61));check(!V::build(image,sizeof(image),command,32,image+100,512,a,b,61));
 check(!V::build(image,sizeof(image),command,32,code,512,reinterpret_cast<const uint32_t*>(image),b,61));
 check(!V::build(nullptr,sizeof(image),command,32,code,512,a,b,61));check(!V::build(image,sizeof(image),nullptr,32,code,512,a,b,61));
 check(!V::build(image,sizeof(image),command,32,code,512,nullptr,b,61));
 // Invalid inputs were rejected before mutation; preserve canonical proposal bytes.
 std::string out=argv[2];writeFile(out+"/image.bin",image,sizeof(image));writeFile(out+"/command.bin",command,32);
 writeFile(out+"/qmd.bin",image+12288,256);writeFile(out+"/constant.bin",image+8192,4096);writeFile(out+"/expected-output.bin",actual+16384,256);
 printf("{\"passed\":true,\"checks\":%llu,\"cases\":%u,\"count\":61,\"threads_per_block\":32,\"blocks\":2,\"hardware_accessed\":false,\"compute_verified\":false,\"metal_verified\":false}\n",checks,cases);
}

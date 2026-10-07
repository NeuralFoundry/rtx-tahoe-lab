#include "GMMULeaves.hpp"
#include "../gsp-page-tables-0.22/reference/dev_mmu_tu102.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
using namespace GMMULeaves;
static uint64_t checks=0;
static void check(bool value) {++checks;if(!value){std::fprintf(stderr,"check failed: %llu\n",(unsigned long long)checks);std::exit(1);}}
static std::vector<uint8_t> load(const char *path) {
  std::ifstream in(path,std::ios::binary);check(bool(in));
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),{});
}
static_assert(NV_MMU_VER2_PTE_APERTURE_VIDEO_MEMORY==0,"VID PTE aperture");
static_assert(NV_MMU_VER2_DUAL_PDE_APERTURE_SMALL_VIDEO_MEMORY==1,"VID PDE aperture");
static_assert(NV_MMU_VER2_PTE_ADDRESS_SHIFT==12,"4K encoding");
static_assert(NV_MMU_CLIENT_KIND_GENERIC_MEMORY==6,"Generic memory kind");
int main(int argc,char **argv) {
  check(argc==3);const auto bytes=load(argv[1]);check(bytes.size()==10*24);
  std::vector<Range> ranges;
  for(size_t i=0;i<bytes.size();i+=24)ranges.push_back({read64(bytes.data()+i),read64(bytes.data()+i+8),read64(bytes.data()+i+16)});
  std::vector<uint8_t> old(OldBytes),out(MaxChildBytes+32,0xa5);
  write64(old.data(),0x100322);write64(old.data()+Page,0x100422);
  write64(old.data()+2*Page+128*8,0x1122334455667788ULL);
  for(size_t i=0;i<16;++i)old[old.size()-16+i]=uint8_t(i);
  const auto before=old;Result result;
  check(build(old.data(),old.size(),ranges.data(),ranges.size(),out.data(),MaxChildBytes,result));
  check(result.childBytes==9*Page&&result.leafTables==8&&result.mappedPages==3237&&result.parentValue==0x100522);
  check(old==before);for(size_t i=result.childBytes;i<out.size();++i)check(out[i]==0xa5);
  std::ofstream file(argv[2],std::ios::binary);file.write(reinterpret_cast<const char *>(out.data()),std::streamsize(result.childBytes));check(bool(file));file.close();
  auto published=old;write64(published.data()+ParentOffset,ParentValue);
  for(uint64_t va=VABase;va<VAEnd;va+=Page) {
    bool expectedMapped=false;uint64_t expected=0;
    for(const auto &r:ranges)if(va>=r.va&&va-r.va<r.bytes){expectedMapped=true;expected=r.pa+va-r.va;}
    for(uint64_t byte:{0ULL,17ULL,4095ULL}) {
      bool mapped=true;uint64_t pa=~0ULL;
      check(walk(published.data(),published.size(),out.data(),result.childBytes,va+byte,mapped,pa));
      check(mapped==expectedMapped);check(pa==(mapped?expected+byte:0));
    }
  }
  auto rejected=[&](const std::vector<uint8_t> &input,const std::vector<Range> &r,size_t capacity) {
    std::vector<uint8_t> dest(MaxChildBytes,0xa5);Result value;
    check(!build(input.data(),input.size(),r.data(),r.size(),dest.data(),capacity,value));
    check(value.childBytes==0);check(std::all_of(dest.begin(),dest.end(),[](uint8_t v){return v==0xa5;}));
  };
  for(size_t offset:{size_t(0),size_t(Page),ParentOffset})for(unsigned bit=0;bit<64;++bit) {
    auto input=old;input[offset+bit/8]^=uint8_t(1U<<(bit%8));rejected(input,ranges,MaxChildBytes);
  }
  for(size_t cap=0;cap<result.childBytes;++cap)rejected(old,ranges,cap);
  for(size_t index=0;index<ranges.size();++index)for(unsigned field=0;field<3;++field) {
    for(uint64_t bad:{0ULL,1ULL,~0ULL,1ULL<<49}) {
      auto r=ranges;if(field==0)r[index].va=bad;else if(field==1)r[index].pa=bad;else r[index].bytes=bad;
      rejected(old,r,MaxChildBytes);
    }
  }
  for(unsigned field=0;field<2;++field) {
    auto r=ranges;if(field==0)r[2].va=r[1].va;else r[2].pa=r[1].pa;rejected(old,r,MaxChildBytes);
  }
  auto alias=old;Result temp;check(!build(alias.data(),alias.size(),ranges.data(),ranges.size(),alias.data(),alias.size(),temp));check(alias==old);
  const std::vector<Range> exact={{VABase,0x1100000,Page},{VABase+0x401000,0x1200000,16ULL<<20}};
  check(build(old.data(),old.size(),exact.data(),exact.size(),out.data(),MaxChildBytes,temp));check(temp.childBytes==MaxChildBytes);
  auto tooMany=exact;tooMany.push_back({VABase+0x2000000,0x2200000,Page});rejected(old,tooMany,MaxChildBytes);
  check(build(old.data(),old.size(),ranges.data(),ranges.size(),out.data(),MaxChildBytes,result));
  for(size_t offset:{size_t(0),size_t(8),size_t(Page)})for(unsigned bit=0;bit<64;++bit) {
    if(offset!=0&&bit>=8&&bit<33)continue;
    auto corrupt=out;corrupt[offset+bit/8]^=uint8_t(1U<<(bit%8));bool mapped=false;uint64_t pa=0;
    check(!walk(published.data(),published.size(),corrupt.data(),result.childBytes,VABase,mapped,pa));check(!mapped&&pa==0);
  }
  for(uint64_t physical:{uint64_t(0),NewBase,LeaseEnd}) {
    auto corrupt=out;write64(corrupt.data()+8,(physical>>4)|2);bool mapped=false;uint64_t pa=0;
    check(!walk(published.data(),published.size(),corrupt.data(),result.childBytes,VABase,mapped,pa));
  }
  std::printf("{\"passed\":true,\"checks\":%llu,\"mapped_pages\":3237,\"leaf_tables\":8,\"hardware_accessed\":false}\n",(unsigned long long)checks);
}

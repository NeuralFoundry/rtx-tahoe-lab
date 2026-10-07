#include "driver/GSPContentSeal.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
namespace S=GSPContentSeal;
namespace D=GSPDmaProtocol;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static std::vector<unsigned char> readFile(const std::string &path,size_t expected) {
  std::ifstream f(path,std::ios::binary);CHECK(bool(f));
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),{});CHECK(b.size()==expected);return b;
}
struct IO {
  std::vector<unsigned char> buffers[D::ResourceCount];
  bool valid=true,changeAfter=false;
  unsigned validations=0,reads=0;
  int failResource=-1;
  bool sealInputsValid(D::U64 generation){++validations;return generation==17 && valid && !(changeAfter && validations>1);}
  bool readSeal(unsigned r,D::U64 off,unsigned char *out,unsigned n){
    ++reads;if(int(r)==failResource)return false;
    if(r>=D::ResourceCount || off>buffers[r].size() || n>buffers[r].size()-off)return false;
    std::memcpy(out,buffers[r].data()+off,n);return true;
  }
  void reset(){valid=true;changeAfter=false;validations=reads=0;failResource=-1;}
};
static void hashTest(const std::string &s,const char *wanted,unsigned chunk){
  GSPDigest::SHA256 h;unsigned char out[32];char text[65]={};
  for(size_t i=0;i<s.size();i+=chunk)h.update(reinterpret_cast<const unsigned char *>(s.data()+i),unsigned(std::min(size_t(chunk),s.size()-i)));
  h.finish(out);const char *hex="0123456789abcdef";
  for(unsigned i=0;i<32;++i){text[i*2]=hex[out[i]>>4];text[i*2+1]=hex[out[i]&15];}
  CHECK(std::strcmp(text,wanted)==0);
}
int main(int argc,char **argv){
  CHECK(argc==2);
  hashTest("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",1);
  hashTest("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",1);
  hashTest(std::string(1000000,'a'),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",4096);
  hashTest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",7);
  const std::string base=argv[1];IO io;
  for(unsigned r=0;r<D::ResourceCount;++r)io.buffers[r]=readFile(base+"/"+D::Names[r]+".bin",D::Sizes[r]);
  auto raw=readFile(base+"/pages.bin",D::TotalPages*8);
  std::vector<D::U64> pages(D::TotalPages),sort(D::TotalPages);
  for(unsigned i=0;i<D::TotalPages;++i)pages[i]=GSPLaunchOwnership::readLE64(raw.data()+i*8);
  CHECK(D::validatePages(pages.data(),D::TotalPages,sort.data(),D::TotalPages)==D::Error::Ok);
  S::Facts facts;facts.bar0=0xfb000000;facts.bar1=0x824000000;facts.bar3=0x820000000;
  facts.maxUserVa=0x7fffffe00000ULL;facts.revision=0xa1;facts.linkCap=0x12345678;
  std::vector<unsigned char> scratch(D::Page*2);S::Result result;
  auto run=[&](){return S::run(io,pages.data(),17,facts,scratch.data(),scratch.data()+D::Page,result);};
  CHECK(run());CHECK(result.seal.completeFor(17));CHECK(result.checkedBytes==D::TotalBytes);
  CHECK(result.comparedPages+result.hashedPages==D::TotalPages);CHECK(io.validations==2);
  CHECK(!S::run(io,pages.data(),17,facts,scratch.data(),scratch.data()+1,result));
  CHECK(!S::run(io,pages.data(),0,facts,scratch.data(),scratch.data()+D::Page,result));
  // One-byte corruption of every complete resource must invalidate the seal.
  for(unsigned r=0;r<D::ResourceCount;++r){
    io.reset();io.buffers[r].back()^=1;CHECK(!run());CHECK(result.resource==r);
    CHECK(!result.seal.completeFor(17));io.buffers[r].back()^=1;
  }
  // Current-pointer mismatches, including radix interior/leaf and queue table.
  const unsigned resources[]={D::Radix3,D::Bootloader,D::Signature,D::Queues,D::Rmargs,D::Logs};
  for(unsigned r:resources){
    io.reset();const unsigned p=GSPLaunchOwnership::firstPage(r);pages[p]+=4096;
    CHECK(!run());CHECK(!result.seal.completeFor(17));pages[p]-=4096;
  }
  for(unsigned page:{0U,1U,2U,32U}){
    io.reset();io.buffers[D::Radix3][page*D::Page]^=1;CHECK(!run());io.buffers[D::Radix3][page*D::Page]^=1;
  }
  io.reset();io.valid=false;CHECK(!run());CHECK(io.reads==0);
  io.reset();io.changeAfter=true;CHECK(!run());CHECK(std::strcmp(result.status,"seal-ownership-changed")==0);
  for(unsigned r=0;r<D::ResourceCount;++r){io.reset();io.failResource=int(r);CHECK(!run());CHECK(result.resource==r);}
  io.reset();facts.linkCap^=1;CHECK(!run());facts.linkCap^=1;
  facts.bar3=facts.bar1;CHECK(!run());CHECK(!facts.valid());facts.bar3=0x820000000;
  facts.maxUserVa=~D::U64(0);CHECK(!run());facts.maxUserVa=0x7fffffe00000ULL;
  io.reset();CHECK(run());
  std::printf("GSP native content seal: %u checks passed; Python oracle matched all %llu bytes\n",checks,D::TotalBytes);
}

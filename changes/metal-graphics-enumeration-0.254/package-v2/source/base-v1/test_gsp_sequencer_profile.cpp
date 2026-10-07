#include "driver/GSPSequencerProfile.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace GSPSequencer;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void put(std::vector<unsigned char>&p,unsigned off,unsigned v){for(unsigned i=0;i<4;++i)p[off+i]=static_cast<unsigned char>(v>>(i*8));}
struct Encoder {
  std::vector<unsigned char> p=std::vector<unsigned char>(PayloadBytes);unsigned cursor=40;
  Encoder(){put(p,0,CapacityWords);put(p,4,UsedWords);}
  bool word(unsigned v){CHECK(cursor+4<=p.size());put(p,cursor,v);cursor+=4;return true;}
  bool write(unsigned a,unsigned v){return word(Write)&&word(a)&&word(v);}
  bool poll(unsigned a,unsigned m,unsigned v,unsigned t,unsigned e){return word(Poll)&&word(a)&&word(m)&&word(v)&&word(t)&&word(e);}
  bool core(unsigned op){return word(op);}
};
int main(int argc,char**argv){
  Encoder e;CHECK(program(e));CHECK(e.cursor==PayloadBytes);
  Profile r;CHECK(profile(e.p.data(),PayloadBytes,r));CHECK(r.operations==420&&r.words==1564);
  CHECK(r.imemBlocks==64&&r.dmemBlocks==36);
  // Every word matters: opcode, address, value, size, index, timeout, saved
  // registers and advertised capacity mutations all fail before execution.
  for(unsigned off=0;off<PayloadBytes;off+=4){auto p=e.p;p[off]^=1;CHECK(!profile(p.data(),PayloadBytes,r));}
  CHECK(!profile(nullptr,PayloadBytes,r));CHECK(!profile(e.p.data(),PayloadBytes-1,r));
  unsigned cursor=0,index=0,writes=0,polls=0;Operation op;
  while(cursor<UsedWords){CHECK(next(e.p.data(),PayloadBytes,cursor,index++,op));writes+=op.op==Write;polls+=op.op==Poll;}
  CHECK(index==420&&writes==312&&polls==104);CHECK(!next(e.p.data(),PayloadBytes,cursor,index,op));
  if(argc==2){FILE *file=nullptr;
#ifdef _MSC_VER
    fopen_s(&file,argv[1],"rb");
#else
    file=std::fopen(argv[1],"rb");
#endif
    CHECK(file!=nullptr);std::vector<unsigned char> raw(8192);CHECK(std::fread(raw.data(),1,raw.size(),file)==raw.size());
    CHECK(std::fgetc(file)==EOF);std::fclose(file);
    CHECK(GSPContentSeal::get32(raw.data()+60)==0x1002);
    CHECK(profile(raw.data()+80,PayloadBytes,r));
    for(unsigned i=0;i<PayloadBytes;++i)CHECK(raw[80+i]==e.p[i]);
    std::printf("Real 0.16 sequencer payload matched canonical 420-operation profile.\n");
  }
  std::printf("Sequencer profile: %u checks passed; no hardware operations.\n",checks);
}

#include "FenceSimulation.hpp"
#include "ExecutionABI.hpp"
struct CaptureIO {
  FenceSim &sim;unsigned fail=0,reads=0,mode=0;unsigned long long time=1000;
  bool ready(){return sim.owned;}
  unsigned long long nowNs(){return time+=1000;}
  bool readMemory(unsigned address,unsigned char *out,unsigned bytes){
    ++reads;CHECK(reads<=3&&bytes==4096&&address==ExecutionCapture::Addresses[reads-1]);
    if(reads==fail)return false;std::memcpy(out,sim.memory.data()+address-H::Ring,bytes);
    if(mode==1&&reads==2)time=H::BudgetNs+100000;if(mode==2&&reads==2)time=0;if(mode==3&&reads==2)sim.owned=false;return true;
  }
};
static void saveWords(const std::string &path,const unsigned long long *words,unsigned count){
  Bytes data(count*8);for(unsigned i=0;i<count;++i)L::write64(data.data()+8*i,words[i]);save(path,data);
}

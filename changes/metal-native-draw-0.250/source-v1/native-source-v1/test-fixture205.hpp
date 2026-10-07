#pragma once
#include "cpu-dispatch-fixture185.hpp"
#include "cpu-access-fixture182.hpp"
#include "owner/NativeOwnedDispatch205.hpp"
namespace C=RTXNativeDispatch205;namespace R=RTXNativeRoot196;
struct Sink185 {
 std::map<std::string,Bytes>files;unsigned calls=0,failAt=0;
 bool save(const char*name,const uint8_t*p,size_t n){if(++calls==failAt)return false;check(!files.count(name));files[name]=Bytes(p,p+n);return true;}
 void dump(const std::filesystem::path&p){check(std::filesystem::create_directory(p));for(const auto&v:files)write(p/v.first,v.second.data(),v.second.size());}
};
struct Wire185 {
 Fake&f;IO dataIO;D::State data;unsigned count=0,failAt=0,corruptAt=0,part=999,timeoutAt=99999;bool postFailure=false,regress=false,timeout=false;
 explicit Wire185(Fake&x):f(x),dataIO(*x.root){check(data.activate(0x179));}
 uint64_t nowNs(){if(regress)return 0;if(timeout&&count>=timeoutAt)return f.clock+C::BudgetNs;return ++f.clock;}
 bool call(unsigned sel,const uint64_t*sc,unsigned n,const uint8_t*in,size_t bytes,uint8_t*out,size_t cap){
  ++count;bool pass=true;
  if(count==failAt&&!postFailure)return false;
  if(sel>=101){const GA::Call call{sc,n,in,bytes,out,cap};pass=f.call(sel,call)==G::Error::None;}
  else if(sel==97){check(n==0&&!bytes&&cap==64);uint64_t info[8]{};data.info(0x179,info);std::memcpy(out,info,64);}
  else{const ABI::Call call{sc,n,in,bytes,out,cap};pass=ABI::dispatch(data,f.root->owner,dataIO,sel,call)==D::Error::None;}
  if((count==corruptAt||(sel==106&&n==4&&sc[1]==part&&sc[2]==0))&&cap)out[0]^=1;
  if(count==failAt)return false;return pass;
 }
};
static R::Capture rootCapture(const std::filesystem::path&folder){
 R::Capture c;const auto before=read(folder/"owned-root-info-before.bin"),after=read(folder/"owned-root-info-after.bin");
 std::memcpy(c.before.data(),before.data(),512);std::memcpy(c.after.data(),after.data(),512);check(R::decode(c.before.data(),512,0x179,c.info));
 c.handles=read(folder/"owned-root-handles.bin");c.rows=read(folder/"owned-root-rows.bin");c.passed=true;return c;
}
struct Bootstrap185 {Fake&f;
 bool copyPristineDevice(std::array<uint8_t,36864>&out)const{
  if(!f.legacy->replacementBefore(0,{f.rootCopy.data(),f.childCopy.data(),f.memory.data(),40960}))return false;
  std::copy(f.memory.begin(),f.memory.end(),out.begin());return true;
 }
};
struct Test185 {
 Fake f;Wire185 io;RTXNativeData182::Session data;C::Session client;Bootstrap185 bootstrap;
 explicit Test185(Input&input,const R::Capture&root):f(input),io(f),bootstrap{f}{Sink185 sink;check(data.collect(io,sink,root));io.count=0;}
 bool begin(const R::Capture&root,const Bytes&image,Sink185&sink){return client.begin(io,sink,root,data,bootstrap,image.data()+640,4608);}
};
static std::array<C::Binding,3>bindings(){return {{{1,0,4,65532},{2,1,4092,131076},{3,2,65532,262148}}};}
static RTXGeometry164::Size local(const Bytes&image){const auto*p=image.data()+640+64;return {G::P::get32(p+16),G::P::get32(p+20),G::P::get32(p+24)};}

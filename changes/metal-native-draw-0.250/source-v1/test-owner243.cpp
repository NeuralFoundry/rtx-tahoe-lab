#define main inherited_root242_main
#include "test-root242.cpp"
#undef main
#undef check
namespace KernelFixture241 {
#define main inherited_submit241_main
#include "test-submit241.cpp"
#undef main
}
#undef check
#define check(v) checked(bool(v),__LINE__)
#include "native-source-v1/owner/NativeGraphicsSession243.hpp"
namespace NG=RTXNativeGraphics243;namespace GA=RTXGraphicsABI242;namespace GS=RTXGraphicsSubmit241;
namespace S=RTXSpans165;
struct Tree : Sink {
 unsigned saves=0,failSave=0;std::string prefix;std::map<std::string,Bytes>all;
 bool save(const char*name,const uint8_t*p,size_t n){++saves;if(saves==failSave)return false;check(all.emplace(prefix+name,Bytes(p,p+n)).second);return true;}
 template<class Action>bool part(const std::string&name,Action action){const auto old=prefix;prefix+=name+"/";const bool ok=action(*this);prefix=old;return ok;}
};
struct Kernel : KernelFixture241::Fake {
 R::Capture&root;IO dataIO;RTXSpans165::Owner<64,16>liveOwner;unsigned hostCalls=0,failCall=0,damage=0,completedInfos=0;bool badClock=false;
 Kernel(R::Capture&r,const Bytes&boot,const Bytes&oracle):Fake(boot,oracle),root(r),dataIO(r),liveOwner(r.info.w[2],L::Begin,L::End){
  for(unsigned slot=0;slot<L::Buffers;++slot){const auto&d=L::Description[slot];S::Mapping m{slot+1,slot+1,L::address(slot),d.bytes,L::mapped(slot),d.access};uint64_t h=0;check(liveOwner.admitMapping(m,h));
   if(slot>=4){maps[slot-4]=m;handles[slot-4]=h;buffers[slot-4].assign(size_t(d.bytes),0);}
  }
 }
 uint64_t nowNs(){return badClock?0:Fake::nowNs();}
 bool mapping(unsigned slot,uint64_t&scope,S::Mapping&out){check(slot>=4&&slot<9);scope=root.info.w[2];out=maps[slot-4];return go("mapping");}
 bool notify(){check(selected&&state->mayNotify());++bells;uint64_t info[32]{};state->info(root.info.w[2],info);
  const auto next=uint32_t(info[21]);const auto end=maps[4].gpuVA+4804;
  GS::P::Q::put32(device.data()+0x888,next);GS::P::Q::put32(device.data()+0x840,uint32_t(end));GS::P::Q::put32(device.data()+0x844,uint32_t(end));
  for(unsigned off:{0x84cu,0x860u})GS::P::Q::put32(device.data()+off,(GS::P::get32(device.data()+off)&~255u)|uint32_t(end>>32));
  GS::P::Q::put32(buffers[3].data(),uint32_t(info[19]));buffers[2]=expected;return go("notify");
 }
 bool call(unsigned sel,const uint64_t*sc,unsigned count,const uint8_t*in,size_t n,uint8_t*out,size_t cap){
  ++hostCalls;if(hostCalls==failCall)return false;
  if(sel>=97&&sel<=100){
   if(sel==99&&uint32_t(sc[1])==7)dataIO.data[6]=buffers[2];
   const bool ok=dataIO.call(sel,sc,count,in,n,out,cap);if(sel==98&&uint32_t(sc[1])==7)buffers[2]=dataIO.data[6];
   if(damage==14&&sel==99&&sc[2]==0&&cap)out[0]^=1;
   return ok;
  }
  check(sel>=109&&sel<=111);const auto rc=GA::dispatch(*state,liveOwner,*this,root.info.w[2],sel,{sc,count,in,n,out,cap});
  if(rc!=GS::Error::None)return false;
  if(state->completed()&&sel==109){if(damage==1)out[0]^=1;if(damage==2)out[88]=4;if(damage==3)out[80]^=1;if(damage==4)out[56]=0;}
  if(sel==111&&sc[2]==0&&((damage>=5&&damage<=13)&&sc[1]==damage-5))out[0]^=1;
  if(sel==109&&state->completed()){++completedInfos;if(damage==15&&completedInfos==2)out[0]^=1;}
  return true;
 }
};
struct Bootstrap {Kernel&kernel;bool valid=true;uint64_t jobs=0;bool ready()const{return valid;}uint64_t completed()const{return jobs;}
 bool copyPristineDevice(std::array<uint8_t,GS::B::DeviceBytes>&out)const{std::copy(kernel.initial.begin(),kernel.initial.end(),out.begin());return valid;}
};
struct Context {
 std::unique_ptr<R::Capture>root;std::unique_ptr<Kernel>kernel;RTXNativeData182::Session data;NG::Session session;Bootstrap bootstrap;Tree arm,begin;
 Context(const fs::path&fixturePath,const Bytes&boot,const Bytes&oracle):root(fixture(fixturePath)),kernel(std::make_unique<Kernel>(*root,boot,oracle)),bootstrap{*kernel}{check(data.collect(*kernel,arm,*root));}
 bool start(){return session.begin(*kernel,begin,*root,data,bootstrap);}
};
int inherited_owner243_main(int argc,char**argv)try{
 check(argc==5);const fs::path fixturePath(argv[1]),out(argv[4]);check(fs::create_directory(out));const auto boot=read(argv[2]),oracle=read(argv[3]);unsigned operationCalls=0,saves=0;
 auto good=std::make_unique<Context>(fixturePath,boot,oracle);check(good->start());
 for(unsigned frame=1;frame<=40;++frame){Tree tree;Bytes result{9,8,7};uint64_t completion=999;const auto before=good->kernel->hostCalls;
  check(good->session.draw(*good->kernel,tree,good->data,result,completion)&&result==oracle&&completion==frame&&good->session.completed()==frame);
  check(good->kernel->bells==frame&&!good->kernel->selected);for(unsigned i=0;i<5;++i){S::Mapping m;uint32_t refs=99;check(good->kernel->liveOwner.inspect(good->kernel->handles[i],m,refs)&&refs==0);}
  if(frame==1){operationCalls=good->kernel->hostCalls-before;saves=tree.saves;}
  if(frame==1||frame==32||frame==33||frame==40){const auto dir=out/("frame-"+std::to_string(frame));check(fs::create_directory(dir));for(const auto&row:tree.all){auto path=dir/row.first;fs::create_directories(path.parent_path());write(path,row.second.data(),row.second.size());}}
 }
 for(unsigned at=1;at<=operationCalls;++at){auto c=std::make_unique<Context>(fixturePath,boot,oracle);check(c->start());c->kernel->failCall=c->kernel->hostCalls+at;Tree tree;Bytes result{9,8,7};uint64_t completion=99;
  check(!c->session.draw(*c->kernel,tree,c->data,result,completion)&&c->session.failed()&&result==Bytes({9,8,7})&&!completion);const auto calls=c->kernel->hostCalls;
  check(!c->session.draw(*c->kernel,tree,c->data,result,completion)&&c->kernel->hostCalls==calls);++rejections;
 }
 for(unsigned at=1;at<=saves;++at){auto c=std::make_unique<Context>(fixturePath,boot,oracle);check(c->start());Tree tree;tree.failSave=at;Bytes result{9,8,7};uint64_t completion=99;
  check(!c->session.draw(*c->kernel,tree,c->data,result,completion)&&c->session.failed()&&result==Bytes({9,8,7})&&!completion);++rejections;
 }
 for(unsigned damage=1;damage<=15;++damage){auto c=std::make_unique<Context>(fixturePath,boot,oracle);check(c->start());c->kernel->damage=damage;Tree tree;Bytes result{9,8,7};uint64_t completion=99;
  check(!c->session.draw(*c->kernel,tree,c->data,result,completion)&&c->session.failed()&&result==Bytes({9,8,7})&&!completion);++rejections;
 }
 for(unsigned kind=0;kind<5;++kind){auto c=std::make_unique<Context>(fixturePath,boot,oracle);const auto calls=c->kernel->hostCalls;
  if(kind==0)c->bootstrap.jobs=1;if(kind==1)c->bootstrap.valid=false;if(kind==2)c->root->handles[4*64+56]=3;if(kind==3)c->root->before[1]^=1;if(kind==4)c->root->passed=false;
  check(!c->start()&&c->kernel->hostCalls==calls&&!c->session.active());++rejections;
 }
 for(unsigned kind=0;kind<3;++kind){auto c=std::make_unique<Context>(fixturePath,boot,oracle);if(kind==0)c->kernel->badClock=true;if(kind==1)c->kernel->failCall=c->kernel->hostCalls+1;if(kind==2)c->begin.failSave=1;
  check(!c->start()&&c->session.failed());++rejections;
 }
 {auto c=std::make_unique<Context>(fixturePath,boot,oracle);for(unsigned offset=0;offset<8;++offset){std::array<uint8_t,272>bytes;bytes.fill(0xa5);uint64_t words[32]{};GA::coldInfo(c->root->info.w[2],words);
   check(c->kernel->call(109,nullptr,0,nullptr,0,bytes.data()+offset,256));for(unsigned i=0;i<32;++i)check(R::u64(bytes.data()+offset+i*8)==words[i]);
   for(unsigned i=0;i<bytes.size();++i)if(i<offset||i>=offset+256)check(bytes[i]==0xa5);
  }}
 std::cout<<"{\"passed\":true,\"checks\":"<<checks+KernelFixture241::checks<<",\"rejections\":"<<rejections<<",\"synthetic_frames\":40,\"draw_calls\":"<<operationCalls<<",\"evidence_files\":"<<saves<<",\"root_pages\":1313,\"actual_native_io\":false,\"gpu_executed\":false}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}

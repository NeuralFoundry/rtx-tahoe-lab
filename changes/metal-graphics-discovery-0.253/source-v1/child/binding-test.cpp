#include "RTXAcceleratorIdentity103.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
using namespace RTXAccelerator103;
static unsigned checks,cases;static RTXMemory107::Evidence fixture;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Graph:Reader {
 std::map<uint32_t,Node> nodes;std::map<uint32_t,uint32_t> links;std::map<uint32_t,int> held;std::map<uint32_t,unsigned> reads;
 uint32_t failRead=0,throwRead=0,failParent=0;bool outputOnError=false;unsigned parents=0;
 unsigned finalMutation=0;
 bool read(uint32_t h,Node&n)override{
  if(h==throwRead)throw std::runtime_error("reader failure");if(h==failRead||!nodes.count(h))return false;n=nodes.at(h);
  if(++reads[h]==2){
   if(h==1){switch(finalMutation){case 1:n.published=false;break;case 2:++n.publicationSession;break;case 3:++n.publicationEpoch;break;case 4:n.pluginIdentity=false;break;case 5:++n.registry;break;case 6:++n.parentRegistry;break;}}
   if(h==2){switch(finalMutation){case 7:n.root.acknowledged=false;break;case 8:n.root.runtimeEnabled=false;break;case 9:n.root.dispatchActive=false;break;case 10:++n.programEpoch;break;case 11:n.programReady=false;break;case 12:++n.memory.generation;break;case 13:n.providerOpen=false;break;case 14:n.root.graphicsABI=0;break;case 15:n.root.graphicsActive=false;break;case 16:n.root.graphicsRetained=true;break;}}
  }return true;
 }
 bool parent(uint32_t h,uint32_t&owned)override{++parents;owned=0;if((h==failParent&&!outputOnError)||!links.count(h))return false;owned=links.at(h);if(owned)++held[owned];return h!=failParent;}
 void release(uint32_t h)override{CHECK(h&&held[h]>0);--held[h];}
 void balanced(){for(const auto&p:held)CHECK(p.second==0);CHECK(parents<=16);}
};
static Graph graph(){
 Graph g;Node child;child.registry=101;child.parentRegistry=fixture.generation;child.publicationABI=3;child.publicationEpoch=1;child.publicationSession=9;
 child.isChild=child.isAccelerator=child.childVersion=child.advertisedParentVersion=child.validParentRegistry=child.published=child.pluginIdentity=true;
 Node parent;parent.registry=fixture.generation;parent.memory=fixture;parent.validMemory=true;parent.targetIdentity=Identity;parent.targetSubsystem=Subsystem;
 parent.isProbe=parent.parentVersion=parent.complete=parent.passed=parent.validTargets=true;parent.root={242,2,181,183,true,true,true,242,true,false};
 parent.programEpoch=1;parent.providerOpen=parent.programReady=parent.hostFence=parent.initDone=true;
 Node pci;pci.registry=303;pci.isPCI=pci.validPCI=true;pci.vendor=0x10de;pci.device=0x2520;pci.subvendor=0x1043;pci.subdevice=0x104c;
 g.nodes={{1,child},{2,parent},{3,pci}};g.links={{1,2},{2,3}};return g;
}
static void check(Graph g,bool expected,uint32_t port=1){
 Binding b;b.childRegistry=b.parentGeneration=b.epoch=b.session=77;CHECK(binding(g,port,b)==expected);g.balanced();++cases;
 if(expected)CHECK(b.childRegistry==101&&b.parentGeneration==fixture.generation&&b.epoch==1&&b.session==9);
 else CHECK(!b.childRegistry&&!b.parentGeneration&&!b.epoch&&!b.session);
}
int main(int argc,char**argv){
 CHECK(argc==2);auto*f=std::fopen(argv[1],"rb");CHECK(f);unsigned char raw[128];CHECK(std::fread(raw,1,sizeof(raw),f)==sizeof(raw)&&std::fgetc(f)==EOF);std::fclose(f);
 CHECK(RTXMemory107::decode(raw,sizeof(raw),fixture));check(graph(),true);check(graph(),false,0);
 for(auto member:{&Node::published,&Node::pluginIdentity,&Node::isChild,&Node::isAccelerator,&Node::childVersion,&Node::advertisedParentVersion,&Node::validParentRegistry}){auto g=graph();g.nodes[1].*member=false;check(g,false);}
 for(auto member:{&Node::publicationABI,&Node::publicationEpoch,&Node::publicationSession,&Node::parentRegistry,&Node::registry}){auto g=graph();g.nodes[1].*member=0;check(g,false);}
 for(auto member:{&Node::providerOpen,&Node::programReady,&Node::hostFence,&Node::initDone,&Node::validMemory,&Node::isProbe,&Node::parentVersion,&Node::complete,&Node::passed,&Node::validTargets}){auto g=graph();g.nodes[2].*member=false;check(g,false);}
 for(auto member:{&RTXRootReady200::State::rootABI,&RTXRootReady200::State::hostABI,&RTXRootReady200::State::dataABI,&RTXRootReady200::State::dispatchABI,&RTXRootReady200::State::graphicsABI}){auto g=graph();++(g.nodes[2].root.*member);check(g,false);}
 for(auto member:{&RTXRootReady200::State::acknowledged,&RTXRootReady200::State::runtimeEnabled,&RTXRootReady200::State::dispatchActive,&RTXRootReady200::State::graphicsActive}){auto g=graph();g.nodes[2].root.*member=false;check(g,false);}
 {auto g=graph();g.nodes[2].programEpoch++;check(g,false);}
 {auto g=graph();g.nodes[2].root.graphicsRetained=true;check(g,false);}
 for(auto member:{&Node::vendor,&Node::device,&Node::subvendor,&Node::subdevice}){auto g=graph();++(g.nodes[3].*member);check(g,false);}
 for(unsigned i=1;i<=16;++i){auto g=graph();g.finalMutation=i;check(g,false);}
 for(uint32_t h:{1U,2U,3U}){auto g=graph();g.failRead=h;check(g,false);}
 for(uint32_t h:{1U,2U})for(bool output:{false,true}){auto g=graph();g.failParent=h;g.outputOnError=output;check(g,false);}
 for(uint32_t h:{2U,3U}){auto g=graph();g.throwRead=h;Binding b;bool caught=false;try{binding(g,1,b);}catch(const std::runtime_error&){caught=true;}CHECK(caught&&!b.childRegistry);g.balanced();++cases;}
 {auto g=graph();g.links[2]=2;check(g,false);}{auto g=graph();g.nodes[3].registry=101;check(g,false);}
 for(unsigned intermediates:{0U,1U,14U,15U,16U}){
  auto g=graph();uint32_t from=2;
  for(unsigned i=0;i<intermediates;++i){Node n;n.registry=1000+i;uint32_t handle=10+i;g.nodes[handle]=n;g.links[from]=handle;from=handle;}
  g.links[from]=3;check(g,intermediates<=14);
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"graph_cases\":%u,\"actual_iokit\":false,\"gpu_jobs\":0}\n",checks,cases);
}

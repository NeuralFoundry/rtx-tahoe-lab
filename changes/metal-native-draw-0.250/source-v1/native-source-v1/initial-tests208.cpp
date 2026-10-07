#define main transport208_prior_main
#include "transport-tests208.cpp"
#undef main
int main(int argc,char**argv){
 check(argc==3);const std::filesystem::path s=argv[1],out=argv[2];check(std::filesystem::create_directory(out));Input input(s);auto root=rootCapture(s/"root-captures");
 std::vector<Bytes>images;for(const char*name:{"first","changed"})images.push_back(read(s/"admission/programs"/name/"artifacts/container.rtxlib"));
 images.push_back(read(s/"programs/64x1x1/compiled.rtxlib"));
 Backend208 backend(input,root,images);backend.evidence=out/"captures";check(std::filesystem::create_directory(backend.evidence));O::Core core;Connection208 c(images[0]);connect(c,core,backend);
 check(backend.live->gpu.revision()==1&&backend.admissions==1&&core.ready());auto wireDir=out/"wire";check(std::filesystem::create_directory(wireDir));
 for(unsigned i=0;i<3;++i){auto frame=frameFor(c,images[i]);auto reply=core.receive(c.peer,frame.bytes.data(),frame.size(),true,backend);Bytes result{9};uint64_t done=0;check(c.client.accept(reply.bytes.data(),reply.size(),0,807,result,done)&&done==i+1&&core.completed()==i+1&&backend.live->gpu.revision()==i+1);
  auto dir=wireDir/std::to_string(i+1);check(std::filesystem::create_directory(dir));write(dir/"request.bin",frame.bytes.data(),frame.size());write(dir/"reply.bin",reply.bytes.data(),reply.size());write(dir/"result.bin",result.data(),result.size());}
 check(backend.selections==2&&backend.admissions==4);core.stop(backend);check(backend.retires==1);
 std::ofstream report(out/"result.json");report<<"{\"passed\":true,\"initial_abi\":1,\"jobs\":3,\"selections\":2,\"checks\":"<<checks<<",\"gpu_executed\":false}\n";check(bool(report));std::cout<<"Initial ABI1 passed\n";return 0;
}

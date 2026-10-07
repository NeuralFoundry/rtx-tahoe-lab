// Test-only native upload bridge. No IOKit linkage or GPU access. Never load
// this image in the production runner or ship it inside a KEXT.
#include "LibraryUploadABI.hpp"
#include <mutex>
#include <new>
#if defined(_WIN32)
#define API extern "C" __declspec(dllexport)
#else
#define API extern "C" __attribute__((visibility("default")))
#endif
namespace U=RtxLibraryUpload036;namespace A=RtxLibraryUploadABI036;
struct Fixture {U::State state;U::Scope scope;std::mutex lock;explicit Fixture(uint64_t generation):scope{generation,17,true}{}};
API void *rtx_upload_fixture_new(uint64_t generation){return generation?new(std::nothrow) Fixture(generation):nullptr;}
API unsigned rtx_upload_fixture_call(void *opaque,unsigned selector,const uint64_t *scalars,unsigned count,const uint8_t *input,size_t bytes,uint8_t *output,size_t capacity){
 if(!opaque)return unsigned(U::Error::Identity);auto &f=*static_cast<Fixture*>(opaque);std::lock_guard<std::mutex> held(f.lock);
 if(selector==0){if(count||bytes||capacity)return unsigned(U::Error::Shape);const auto e=f.state.consume(f.scope);if(e==U::Error::None)f.scope.preparationAllowed=false;return unsigned(e);}
 const A::Call call{scalars,count,input,bytes,output,capacity};return unsigned(A::dispatch(f.state,f.scope,selector,call));
}
API void rtx_upload_fixture_delete(void *opaque){delete static_cast<Fixture*>(opaque);}

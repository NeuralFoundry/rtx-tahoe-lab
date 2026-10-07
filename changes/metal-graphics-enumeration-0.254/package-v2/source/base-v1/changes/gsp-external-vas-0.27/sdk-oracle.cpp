#include <cstddef>
#include <cstdio>
#if defined(_MSC_VER) && !defined(__clang__)
// The Linux SDK omits its MSVC field-alignment macro. Match the existing oracle.
#define NV_DECLARE_ALIGNED(TYPE_VAR,ALIGN) __declspec(align(ALIGN)) TYPE_VAR
#endif
#include <nvos.h>
#include <class/cl0000.h>
#include <class/cl0080.h>
#include <class/cl2080.h>
#include "reference/set-page-directory-types.h"
static_assert(sizeof(NV0000_ALLOC_PARAMETERS)==120);
static_assert(sizeof(NV0080_ALLOC_PARAMETERS)==56);
static_assert(sizeof(NV2080_ALLOC_PARAMETERS)==4);
static_assert(sizeof(NV_VASPACE_ALLOCATION_PARAMETERS)==48);
static_assert(sizeof(NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_v1E_05)==32);
static_assert(sizeof(rpc_set_page_directory_v1E_05)==48);
static_assert(offsetof(rpc_set_page_directory_v1E_05,params)==16);
static_assert(offsetof(NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_v1E_05,pasid)==28);
static_assert(NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED==8);
static_assert(NV_VASPACE_ALLOCATION_FLAGS_ENABLE_PAGE_FAULTING==64);
template<class T>bool emit(FILE *f,const T &v){return fwrite(&v,1,sizeof(v),f)==sizeof(v);}
int main(int argc,char **argv){
 if(argc!=2)return 2;FILE *f=fopen(argv[1],"wb");if(!f)return 3;
 NV0000_ALLOC_PARAMETERS root={};NV0080_ALLOC_PARAMETERS device={};NV2080_ALLOC_PARAMETERS sub={};NV_VASPACE_ALLOCATION_PARAMETERS va={};
 device.hClientShare=0xc1000000U;device.vaMode=NV_DEVICE_ALLOCATION_VAMODE_OPTIONAL_MULTIPLE_VASPACES;
 va.flags=NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED|NV_VASPACE_ALLOCATION_FLAGS_ENABLE_PAGE_FAULTING;
 va.vaBase=0x1000;va.vaSize=0x1fffffb000000ULL;
 rpc_set_page_directory_v1E_05 pd={};pd.hClient=0xc1000000U;pd.hDevice=0xcf000011U;pd.pasid=~0U;
 pd.params.physAddress=0x1002000;pd.params.numEntries=4;pd.params.flags=8;pd.params.hVASpace=0xcf000013U;pd.params.subDeviceId=1;pd.params.pasid=~0U;
 bool ok=emit(f,root)&&emit(f,device)&&emit(f,sub)&&emit(f,va)&&emit(f,pd);if(fclose(f))ok=false;
 return ok?0:4;
}

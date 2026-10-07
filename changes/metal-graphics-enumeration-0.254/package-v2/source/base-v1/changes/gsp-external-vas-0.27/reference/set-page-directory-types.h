// Exact typedefs extracted from commit-pinned NVIDIA generated headers.
#include "../sdk/nvtypes.h"
typedef struct NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_v1E_05
{
    NvU64      physAddress NV_ALIGN_BYTES(8);
    NvU32      numEntries;
    NvU32      flags;
    NvHandle   hVASpace;
    NvU32      chId;
    NvU32      subDeviceId;
    NvU32      pasid;
} NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_v1E_05;
typedef struct rpc_set_page_directory_v1E_05
{
    NvHandle   hClient;
    NvHandle   hDevice;
    NvU32      pasid;
    NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS_v1E_05 params;
} rpc_set_page_directory_v1E_05;

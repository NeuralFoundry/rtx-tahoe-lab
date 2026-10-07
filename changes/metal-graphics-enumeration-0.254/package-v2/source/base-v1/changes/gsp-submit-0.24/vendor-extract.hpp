// Exact typedef and command extracts from the adjacent MIT-licensed NVIDIA 570.144 sources.
#pragma once
#include <nvtypes.h>
typedef struct NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS {
    NvBool bEnable;
    NvBool bSkipSubmit;
} NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS;
typedef struct NVA06F_CTRL_BIND_PARAMS {
    NvU32 engineType;
} NVA06F_CTRL_BIND_PARAMS;
typedef struct NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS {
    NvU32 workSubmitToken;
} NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS;
#define NVA06F_CTRL_CMD_GPFIFO_SCHEDULE (0xa06f0103)
#define NVA06F_CTRL_CMD_BIND (0xa06f0104)
#define NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN (0xc36f0108)

#include <metal_stdlib>
using namespace metal;
kernel void runtime_4017_same(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device float *out [[buffer(2)]], uint tid [[thread_position_in_grid]]) { out[tid]=(a[tid]+b[tid])+5.0f; }

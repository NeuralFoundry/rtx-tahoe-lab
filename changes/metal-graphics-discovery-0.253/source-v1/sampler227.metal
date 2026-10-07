#include <metal_stdlib>
using namespace metal;
kernel void sample_texture227(texture2d<float,access::sample> source [[texture(0)]],
 sampler sampling [[sampler(0)]],device const float* uv [[buffer(0)]],
 device float* output [[buffer(1)]],uint gid [[thread_position_in_grid]]) {
 float4 color=source.sample(sampling,float2(uv[gid*2],uv[gid*2+1]),level(0.0f));
 output[gid*4]=color.x;output[gid*4+1]=color.y;output[gid*4+2]=color.z;output[gid*4+3]=color.w;
}

#include <metal_stdlib>
using namespace metal;
kernel void rtx_clear_color231(texture2d<float,access::write> target [[texture(0)]],
    device const float *rgba [[buffer(0)]], uint2 position [[thread_position_in_grid]]) {
    target.write(float4(rgba[0],rgba[1],rgba[2],rgba[3]),position);
}

#include <metal_stdlib>
using namespace metal;
kernel void texture_copy(texture2d<float,access::read> source [[texture(0)]],
                         texture2d<float,access::write> dest [[texture(1)]],
                         uint3 gid [[thread_position_in_grid]]) {
    dest.write(source.read(gid.xy),gid.xy);
}

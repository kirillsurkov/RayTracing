#version 430

uniform mat4 u_viewInv;
uniform ivec2 u_screenSize;

layout(local_size_x = 8, local_size_y = 8) in;

layout(std430,  binding = 0)           buffer Counter   { uint counter; };
layout(std430,  binding = 1) writeonly buffer OutBuffer { vec4 rayBuffer[]; };

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= u_screenSize.x || pixelCoords.y >= u_screenSize.y) {
        return;
    }

    vec2 xy = vec2(2.0 * float(pixelCoords.x * 2.0 - u_screenSize.x) / float(u_screenSize.x), 2.0 * float(pixelCoords.y * 2.0 - u_screenSize.y) / float(u_screenSize.y));
    xy.x *= float(u_screenSize.x) / float(u_screenSize.y);

    vec4 origin = u_viewInv*vec4(0.0, 0.0, 0.0, 1.0);
    vec4 dir    = u_viewInv*vec4(normalize(vec3(xy, -5.0)), 0.0);

    origin.w = intBitsToFloat(pixelCoords.x);
    dir.w    = intBitsToFloat(pixelCoords.y);

    uint offset = atomicAdd(counter, 1);

    rayBuffer[offset * 2 + 0] = origin;
    rayBuffer[offset * 2 + 1] = dir;
}
